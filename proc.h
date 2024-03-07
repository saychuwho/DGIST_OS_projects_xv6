#ifndef PROC
#define PROC

#include "rbtree.h" // prj 01

// Per-CPU state
struct cpu {
  uchar apicid;                // Local APIC ID
  struct context *scheduler;   // swtch() here to enter scheduler
  struct taskstate ts;         // Used by x86 to find stack for interrupt
  struct segdesc gdt[NSEGS];   // x86 global descriptor table
  volatile uint started;       // Has the CPU started?
  int ncli;                    // Depth of pushcli nesting.
  int intena;                  // Were interrupts enabled before pushcli?
  struct proc *proc;           // The process running on this cpu or null
};

extern struct cpu cpus[NCPU];
extern int ncpu;

//PAGEBREAK: 17
// Saved registers for kernel context switches.
// Don't need to save all the segment registers (%cs, etc),
// because they are constant across kernel contexts.
// Don't need to save %eax, %ecx, %edx, because the
// x86 convention is that the caller has saved them.
// Contexts are stored at the bottom of the stack they
// describe; the stack pointer is the address of the context.
// The layout of the context matches the layout of the stack in swtch.S
// at the "Switch stacks" comment. Switch doesn't save eip explicitly,
// but it is on the stack and allocproc() manipulates it.
struct context {
  uint edi;
  uint esi;
  uint ebx;
  uint ebp;
  uint eip;
};

enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// Per-process state
struct proc {
  uint sz;                     // Size of process memory (bytes)
  pde_t* pgdir;                // Page table
  char *kstack;                // Bottom of kernel stack for this process
  enum procstate state;        // Process state
  int pid;                     // Process ID
  struct proc *parent;         // Parent process
  struct trapframe *tf;        // Trap frame for current syscall
  struct context *context;     // swtch() here to run process
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)
  uint start_tick; // mini-prj 01

  // prj 01
  uint key;                    // vruntime in CFS, deadline in EEVDF
  uint time_slice;
  struct rb_node node;
  uint s_mode;
  uint last_sched_time;

  // prj 01 - EEVDF
  int lag;
  int vt_eligible;
  int weight;
  int quantum;
  int used_time;
  int vt_init;

  // prj 01 extra
  uint nice_value;
  uint yield_num;
};

// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap

// prj 01
#define ROUND_ROBIN 0
#define CFS 1
#define EEVDF 2

#define VT_MULTIPLE 10

#define DEBUG_PROC_TREE 0
#define DEBUG_PROC_TREE_2 0
#define DEBUG_PROC_TREE_3 0
#define DEBUG_TRAP_YIELD 0
#define DEBUG_SCHED_ALLOC 0
#define DEBUG_SCHED_PICK 0
#define DEBUG_EEVDF 0



enum sysfunc { ALLOCPROC, EXIT, SCHEDULER, YIELD, SLEEP, WAKEUP, NICE_VALUE_UP, NICE_VALUE_DOWN };

void proc_tree_init();
void __proc_tree_insert(struct proc *p, struct rb_root *root);
void __proc_tree_erase(struct proc *p, struct rb_root *root);
void proc_tree_insert(struct proc *p, struct rb_root *root, int debug, int sysfunc);
void proc_tree_erase(struct proc *p, struct rb_root *root, int debug, int sysfunc);

void eevdf_join(struct proc *p, int sysfunc);
void eevdf_leave(struct proc *p, int sysfunc);

// prj 01 Extra

#define PRIORITY 3
#define CFS_EXTRA 1

struct cfs_prior{
  uint curproc_num;
  uint runnable_proc_num;
  uint time_slice;
};

int __nice_value_up(struct proc *p);
int __nice_value_down(struct proc *p);

#endif