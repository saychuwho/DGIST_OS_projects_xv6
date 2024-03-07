#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }

  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;

  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // prj 01
  // change sched_time_slice when sched_latency passes after sched_time_slice changed
  if(sched_mode == CFS && ticks >= sched_tick){
    sched_tick = ticks + sched_latency;
    if(runnable_proc_num >= 1){
      sched_time_slice = sched_latency / runnable_proc_num;
      if(sched_time_slice < min_granularity)
        sched_time_slice = min_granularity;
    }
    if(CFS_EXTRA){
      for(int i=0;i<PRIORITY;i++){
        if(Priority[i].runnable_proc_num >= 1){
          Priority[i].time_slice = sched_latency / Priority[i].runnable_proc_num;
          if(Priority[i].time_slice < min_granularity)
            Priority[i].time_slice = min_granularity;
        }
      }
    }
  }

  // prj 01
  // virtual time update in EEVDF - multiply VT_MULTIPLE to eliminate point num
  if(sched_mode == EEVDF && TotalWeight > 0){
    VirtualTime = VirtualTime + (VT_MULTIPLE / TotalWeight);
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  struct proc *p = myproc();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER){
    if(sched_mode == ROUND_ROBIN)
      yield();
    else if(sched_mode == CFS && (p->last_sched_time + p->time_slice) < ticks){
      if(DEBUG_TRAP_YIELD){
        cprintf("\n... timer interrupt called yield (ticks : %d) ...\n", ticks);
        cprintf("p->last_sched_time : %d\n", p->last_sched_time);
        cprintf("p->time_slice : %d\n", p->time_slice);
      }
      yield();
    }
    else if(sched_mode == EEVDF && ((p->last_sched_time + p->quantum) < ticks)){
      yield();
    }
  }  

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
