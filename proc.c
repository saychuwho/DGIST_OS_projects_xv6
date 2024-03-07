#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "syscall.h" // mini-prj 02
#include "rbtree.h" // prj 01

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
  struct rb_root proc_tree; // prj 01
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

// prj 01
uint sched_mode = EEVDF;

uint sched_latency = 16;
uint min_granularity = 2;
uint nice_value = 0;

uint sched_tick;
uint sched_time_slice;

uint curproc_num = 0;
uint runnable_proc_num = 0;

// prj 01 - EEVDF
int VirtualTime = 0;
int TotalWeight = 0;

struct cfs_prior priority[PRIORITY];
struct cfs_prior *Priority;

uint nice_value_array[PRIORITY] = {1, 100, 500};

// prj 01
void
pinit(void)
{
  // prj 01
  if(sched_mode == CFS){
    sched_tick = ticks + sched_latency;
    sched_time_slice = sched_latency;
    if(CFS_EXTRA){
      Priority = priority;
      for(int i=0;i<PRIORITY;i++){
        priority[i].runnable_proc_num = 0;
        priority[i].curproc_num = 0;
        priority[i].time_slice = sched_latency;
      }
    }
  }
  ptable.proc_tree = RB_ROOT;

  initlock(&ptable.lock, "ptable");
}


// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  // prj 01
  // prj 01 + extra
  if(sched_mode == CFS){
    p->key = 0;
    p->last_sched_time = ticks;
    p->s_mode = sched_mode;
    
    if(CFS_EXTRA){
      p->nice_value = 0;
      p->yield_num = 0;
    }
    
    proc_tree_insert(p, &ptable.proc_tree, DEBUG_PROC_TREE, ALLOCPROC);
  }
  else if(sched_mode == EEVDF){
    // init the proc values
    p->s_mode = sched_mode;
    p->lag = 0;
    p->last_sched_time = ticks;
    p->weight = 1;
    p->quantum = 1;
    // add proc into proc_tree
    eevdf_join(p, ALLOCPROC);
    proc_tree_insert(p, &ptable.proc_tree, DEBUG_PROC_TREE, ALLOCPROC);
  }
  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  p->start_tick = ticks; // mini-prj 01
  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  // mini-proj 01
  cprintf("\n%s(%d) took %d ticks in wallclock\n", curproc->name, curproc->pid, ticks - curproc->start_tick);

  if(curproc == initproc)
    panic("init exiting");


  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  if(sched_mode == CFS){
    // prj 01
    proc_tree_erase(curproc, &ptable.proc_tree, DEBUG_PROC_TREE, EXIT);
  }
  else if(sched_mode == EEVDF){
    eevdf_leave(curproc, EXIT);
    proc_tree_erase(curproc, &ptable.proc_tree, DEBUG_PROC_TREE, EXIT);
  }

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
// scheduler() extra
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    acquire(&ptable.lock);
    if(sched_mode == ROUND_ROBIN){
      // Loop over process table looking for process to run.
      for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        if(p->state != RUNNABLE)
          continue;

        // Switch to chosen process.  It is the process's job
        // to release ptable.lock and then reacquire it
        // before jumping back to us.
        c->proc = p;
        switchuvm(p);
        p->state = RUNNING;

        swtch(&(c->scheduler), p->context);
        switchkvm();

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
      }
    }
    else if(sched_mode == CFS){
      // get p from proc_tree_root and change p->key, p->time_slice
      struct rb_node *first = rb_first(&ptable.proc_tree);
      struct rb_node *last = rb_last(&ptable.proc_tree);
      struct rb_node *next = NULL;
      p = rb_entry(first, struct proc, node);

      for(int i=0; i<curproc_num; i++){
        //cprintf(p->name);
        //cprintf(" is in tree : %d\n", ticks);

        if (p->state != RUNNABLE){
          if(next == last){
            break;
          }
          next = rb_next(&p->node);
          p = rb_entry(next, struct proc, node);
          continue;
        }

        p->last_sched_time = ticks;

      
        if (DEBUG_SCHED_PICK){
          cprintf("\n... SCHEDULER picked p (ticks : %d called : %d) ...\n", ticks, SCHEDULER);
          cprintf("curproc_num : %d\n", curproc_num);
          cprintf("runnable_proc_num : %d\n", runnable_proc_num);
          cprintf("sched tick : %d\n", sched_tick);
          cprintf("sched time slice : %d\n", sched_time_slice);
          cprintf("p->pid : %d\n", p->pid);
          cprintf("p->key : %d\n", p->key);
          cprintf("p->time_slice : %d\n", p->time_slice);
          cprintf("p->last_sched_time : %d\n", p->last_sched_time);
          cprintf("\n... tree keys ...\n");
          next = first;
          for (int i = 0; i < curproc_num;i++){
            cprintf("key of pid - %d (state : %d) : %d\n", rb_entry(next, struct proc, node)->pid, rb_entry(next, struct proc, node)->state, rb_entry(next, struct proc, node)->key);
            next = rb_next(next);
          }
        }

        // Switch to chosen process.  It is the process's job
        // to release ptable.lock and then reacquire it
        // before jumping back to us.
        c->proc = p;
        switchuvm(p);
        p->state = RUNNING;

        swtch(&(c->scheduler), p->context);
        switchkvm();

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
        break;
      }
    }
    else if(sched_mode == EEVDF){

      // get p from proc_tree_root and change p->key, p->time_slice
      struct rb_node *first = rb_first(&ptable.proc_tree);
      struct rb_node *last = rb_last(&ptable.proc_tree);
      struct rb_node *next = NULL;
      p = rb_entry(first, struct proc, node);

      //if(curproc_num > 12)
      //  cprintf("p->tf->eip : %d\n", p->tf->eip);

      for(int i=0; i<curproc_num; i++){
        //cprintf(p->name);
        //cprintf(" is in tree : %d\n", ticks);

        if (p->vt_eligible < VirtualTime){
          p->lag = 0;
        }

        if (p->state != RUNNABLE || p->lag < 0){
          if(DEBUG_SCHED_ALLOC){
            cprintf("\n... debug in scheduler (ticks : %d) ...\n", ticks);
            cprintf("p->pid : %d\n", p->pid);
            cprintf("p->state : %d\n", p->state);
            cprintf("p->lag : %d\n", p->lag);
          }
          if(next == last){
            if(DEBUG_SCHED_ALLOC){
              cprintf("no proc inside tree is RUNNABLE or eligible\n");
            }
            break;
          }
          next = rb_next(&p->node);
          p = rb_entry(next, struct proc, node);
          continue;
        }

        //if(curproc_num > 3)
        //  cprintf("\npicked p : %d state : %d\n", p->pid, p->state);

        if (DEBUG_SCHED_PICK){
          cprintf("\n... SCHEDULER picked p (ticks : %d called : %d) ...\n", ticks, SCHEDULER);
          cprintf("curproc_num : %d\n", curproc_num);
          cprintf("runnable_proc_num : %d\n", runnable_proc_num);
          cprintf("VirtualTime : %d\n", VirtualTime);
          cprintf("TotalWeight : %d\n", TotalWeight);
          cprintf("p->pid : %d\n", p->pid);
          cprintf("p->key : %d\n", p->key);
          cprintf("p->lag : %d\n", p->lag);
          cprintf("p->vt_eligible : %d\n", p->vt_eligible);
          cprintf("p->used_time : %d\n", p->used_time);
          cprintf("p->vt_init : %d\n", p->vt_init);
          cprintf("\n... tree keys ...\n");
          next = first;
          for (int i = 0; i<curproc_num;i++){
            cprintf("key of pid %d : %d (state : %d  lag : %d vt_eligible %d)\n", rb_entry(next, struct proc, node)->pid, rb_entry(next, struct proc, node)->key, rb_entry(next, struct proc, node)->state, rb_entry(next, struct proc, node)->lag, rb_entry(next, struct proc, node)->vt_eligible);
            if(next != last)
              next = rb_next(next);
          }
        }

        p->used_time = p->used_time + p->quantum;
        p->last_sched_time = ticks;
        
        if(VirtualTime != p->vt_init)
          p->lag = p->weight*(VirtualTime - p->vt_init) - (VT_MULTIPLE*p->used_time);

        p->vt_eligible += ((p->used_time * VT_MULTIPLE) / p->weight);
        p->key = p->vt_eligible + ((p->quantum/p->weight)*VT_MULTIPLE);
        
        if(DEBUG_SCHED_PICK){
          cprintf("\n... SCHEDULER calculated and reallocate (ticks : %d called : %d) ...\n", ticks, SCHEDULER);
          cprintf("curproc_num : %d\n", curproc_num);
          cprintf("runnable_proc_num : %d\n", runnable_proc_num);
          cprintf("VirtualTime : %d\n", VirtualTime);
          cprintf("TotalWeight : %d\n", TotalWeight);
          cprintf("p->pid : %d\n", p->pid);
          cprintf("p->key : %d\n", p->key);
          cprintf("p->lag : %d\n", p->lag);
          cprintf("p->vt_eligible : %d\n", p->vt_eligible);
          cprintf("p->used_time : %d\n", p->used_time);
          cprintf("p->vt_init : %d\n", p->vt_init);
          cprintf("\n... tree keys ...\n");
        }

        if(DEBUG_PROC_TREE_3){
          cprintf("\n... what is in tree ...\n");
          first = rb_first(&ptable.proc_tree);
          last = rb_last(&ptable.proc_tree);
          cprintf("first pid : %d\n", rb_entry(first, struct proc, node)->pid);
          cprintf("last pid : %d\n", rb_entry(last, struct proc, node)->pid);
          next = first;
          for (int i = 0; i<curproc_num;i++){
            cprintf("key of pid %d : %d (state : %d  lag : %d vt_eligible : %d)\n", rb_entry(next, struct proc, node)->pid, rb_entry(next, struct proc, node)->key, rb_entry(next, struct proc, node)->state, rb_entry(next, struct proc, node)->lag, rb_entry(next, struct proc, node)->vt_eligible);
            if(next != last)
              next = rb_next(next);
          }
        }

        // Switch to chosen process.  It is the process's job
        // to release ptable.lock and then reacquire it
        // before jumping back to us.
        c->proc = p;
        switchuvm(p);
        p->state = RUNNING;

        swtch(&(c->scheduler), p->context);
        switchkvm();

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
        break;
      }  
    }
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  if(sched_mode == CFS){
    proc_tree_erase(myproc(), &ptable.proc_tree, DEBUG_PROC_TREE, YIELD);
    proc_tree_insert(myproc(), &ptable.proc_tree, DEBUG_PROC_TREE, YIELD);
  }
  else if(sched_mode == EEVDF){
    struct proc *p = myproc();
    proc_tree_erase(p, &ptable.proc_tree, DEBUG_PROC_TREE, YIELD);
    proc_tree_insert(p, &ptable.proc_tree, DEBUG_PROC_TREE, YIELD);
  }
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  if(sched_mode == CFS){
    runnable_proc_num--;
    if(CFS_EXTRA){
      priority[p->nice_value].runnable_proc_num--;
    }
  }
  else if(sched_mode == EEVDF)
    eevdf_leave(p, SLEEP);

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan){
      p->state = RUNNABLE;
      if(sched_mode == CFS){
        runnable_proc_num++;
        if(CFS_EXTRA){
          priority[p->nice_value].runnable_proc_num++;
        }
      }
      else if(sched_mode == EEVDF)
        eevdf_join(p, WAKEUP);
    }
}


// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

// mini-prj 02
int 
gethostname(char *h_name)
{
  struct proc *curproc = myproc();
  curproc->tf->eax = SYS_gethostname;
  curproc->tf->esp = (uint)h_name;
  syscall();
  return 0;
}

// mini-prj 02
int 
sethostname(const char *h_name)
{
  struct proc *curproc = myproc();
  curproc->tf->eax = SYS_sethostname;
  curproc->tf->esp = (uint)h_name;
  syscall();
  return 0;
}


// insert & delete
// prj 01

void
__proc_tree_insert(struct proc *p, struct rb_root *root)
{
  // debug
  //cprintf(p->name);
  //cprintf(": proc insert called \n");
  
  struct rb_node **new = &root->rb_node, *parent = NULL;
  uint key = p->key;
  struct proc *parent_proc;
  while(*new){
    parent = *new;
    parent_proc = rb_entry(parent, struct proc, node);
    if(key < parent_proc->key)
      new = &parent->rb_left;
    else{
      new = &parent->rb_right;
    }
  }

  rb_link_node(&p->node, parent, new);
  rb_insert_color(&p->node, root);
}

void
__proc_tree_erase(struct proc *p, struct rb_root *root)
{
  // debug
  //cprintf(p->name);
  //cprintf(": proc delete called \n");

  rb_erase(&p->node, root);
}

void
proc_tree_insert(struct proc *p, struct rb_root *root, int debug, int sysfunc)
{
  if(sysfunc != YIELD){
    curproc_num++;
    runnable_proc_num++;
    if(CFS_EXTRA){
      priority[p->nice_value].curproc_num++;
      priority[p->nice_value].runnable_proc_num++;
    }
  }
  if(sched_mode == CFS){
    if(CFS_EXTRA){
      uint added_key = nice_value_array[p->nice_value]*(priority[p->nice_value].time_slice);
      p->key = p->key + added_key;
      p->time_slice = priority[p->nice_value].time_slice;
    }
    else{
      uint added_key = nice_value*(sched_time_slice);
      p->key = p->key + added_key;
      p->time_slice = sched_time_slice;
    }
  }
  if (debug){
    cprintf("\n... proc_tree_insert (ticks : %d called : %d) ...\n", ticks, sysfunc);
    cprintf("p->pid : %d\n", p->pid);
    cprintf("p->key : %d\n", p->key);
    if(sched_mode == CFS){
      cprintf("curproc_num : %d\n", curproc_num);
      cprintf("runnable_proc_num : %d\n", runnable_proc_num);
      cprintf("sched tick : %d\n", sched_tick);
      cprintf("sched time slice : %d\n", sched_time_slice);
      cprintf("p->time_slice : %d\n", p->time_slice);
      cprintf("p->last_sched_time : %d\n", p->last_sched_time);
    }
    else if(sched_mode == EEVDF){
      cprintf("VirtualTime : %d\n", VirtualTime);
      cprintf("TotalWeight : %d\n", TotalWeight);
      cprintf("p->lag : %d\n", p->lag);
      cprintf("p->vt_eligible : %d\n", p->vt_eligible);
      cprintf("p->used_time : %d\n", p->used_time);
      cprintf("p->vt_init : %d\n", p->vt_init);
    }
  }

  __proc_tree_insert(p, root);
}

void
proc_tree_erase(struct proc *p, struct rb_root *root, int debug, int sysfunc)
{
  if(sysfunc != YIELD){
    curproc_num--;
    runnable_proc_num--;
    if(CFS_EXTRA){
      priority[p->nice_value].curproc_num--;
      priority[p->nice_value].runnable_proc_num--;
    }
  }
  if (debug){
    cprintf("\n... proc_tree_erase (ticks : %d called : %d) ...\n", ticks, sysfunc);
    cprintf("p->pid : %d\n", p->pid);
    cprintf("p->key : %d\n", p->key);
    if(sched_mode == CFS){
      cprintf("curproc_num : %d\n", curproc_num);
      cprintf("runnable_proc_num : %d\n", runnable_proc_num);
      cprintf("sched tick : %d\n", sched_tick);
      cprintf("sched time slice : %d\n", sched_time_slice);
      cprintf("p->time_slice : %d\n", p->time_slice);
      cprintf("p->last_sched_time : %d\n", p->last_sched_time);
    }
    else if(sched_mode == EEVDF){
      cprintf("VirtualTime : %d\n", VirtualTime);
      cprintf("TotalWeight : %d\n", TotalWeight);
      cprintf("p->lag : %d\n", p->lag);
      cprintf("p->vt_eligible : %d\n", p->vt_eligible);
      cprintf("p->used_time : %d\n", p->used_time);
      cprintf("p->vt_init : %d\n", p->vt_init);
    }
  }
  __proc_tree_erase(p, root);
}

// prj 01 - EEVDF

void
eevdf_join(struct proc *p, int sysfunc)
{
  if(DEBUG_EEVDF){
    cprintf("\n... eevdf_join called (ticks : %d called : %d) ...\n", ticks, sysfunc);
    cprintf("p->pid : %d\n", p->pid);
    cprintf("VirtualTime before : %d\n", VirtualTime);
    cprintf("TotalWeight before : %d\n", TotalWeight);
  }
  TotalWeight += p->weight;
  if(DEBUG_EEVDF)
    cprintf("TotalWeight after : %d\n", TotalWeight);
  p->used_time = 0;
  VirtualTime = VirtualTime - (p->lag / TotalWeight);
  p->vt_init = VirtualTime;
  if(DEBUG_EEVDF)
    cprintf("VirtualTime after : %d\n", VirtualTime);


  p->vt_eligible = VirtualTime;
  p->key = p->vt_eligible + (p->quantum / p->weight)*VT_MULTIPLE;

}

void
eevdf_leave(struct proc *p, int sysfunc)
{
  if(DEBUG_EEVDF){
    cprintf("... eevdf_leave called (ticks : %d called : %d) ...\n", ticks, sysfunc);
    cprintf("p->pid : %d\n", p->pid);
    cprintf("VirtualTime before : %d\n", VirtualTime);
    cprintf("TotalWeight before : %d\n", TotalWeight);
  }
  TotalWeight -= p->weight;
  if(DEBUG_EEVDF)
    cprintf("TotalWeight after : %d\n", TotalWeight);
  if(TotalWeight > 0)
    VirtualTime = VirtualTime + (p->lag / TotalWeight);
  if(DEBUG_EEVDF)
    cprintf("VirtualTime after : %d\n", VirtualTime);
}

int
__nice_value_up(struct proc *p)
{ 
  if (p->nice_value == 0)
    return -1;
  
  priority[p->nice_value].curproc_num--;
  priority[p->nice_value].runnable_proc_num--;

  p->nice_value--;

  priority[p->nice_value].curproc_num++;
  priority[p->nice_value].runnable_proc_num++;

  for(int i=0;i<PRIORITY;i++){
    if(priority[i].runnable_proc_num >= 1){
      priority[i].time_slice = sched_latency / priority[i].runnable_proc_num;
      if(priority[i].time_slice < min_granularity)
        priority[i].time_slice = min_granularity;
    }
  }

  return 0;
}

int
__nice_value_down(struct proc *p){
  if (p->nice_value == PRIORITY)
    return -1;

  priority[p->nice_value].curproc_num--;
  priority[p->nice_value].runnable_proc_num--;

  p->nice_value++;

  priority[p->nice_value].curproc_num++;
  priority[p->nice_value].runnable_proc_num++;

  for(int i=0;i<PRIORITY;i++){
    if(priority[i].runnable_proc_num >= 1){
      priority[i].time_slice = sched_latency / priority[i].runnable_proc_num;
      if(priority[i].time_slice < min_granularity)
        priority[i].time_slice = min_granularity;
    }
  }


  return 0;
}

// prj 01 extra - user function

int
nicevalueup(void)
{
  struct proc *curproc = myproc();
  curproc->tf->eax = SYS_nicevalueup;
  syscall();
  return 0;
}

int
nicevaluedown(void)
{
  struct proc *curproc = myproc();
  curproc->tf->eax = SYS_nicevaluedown;
  syscall();
  return 0;
}