// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"

//#define MAXENTRY 57334 

extern int PID[];
extern uint VPN[];
extern pte_t PTE_XV6[];

void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
} kmem;

// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  kmem.use_lock = 1;
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE){
    //kfree(p);
	  PID[(int)(V2P(p))/4096] = -1; //If PID[i] is -1, the physical frame i is freespace.
  }
}
//PAGEBREAK: 21
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)

void kfree(int pid, char *v){

  uint kv, idx;
  //TODO: Fill the code that supports kfree
  //1. Find the corresponding physical address for given pid and VA
  //2. Initialize the PID[idx], VPN[idx], and PTE_XV6[idx]
  //3. For memset, Convert the physical address for free to kernel's virtual address by using P2V macro

  // 1.
  idx = searchidx((uint)v, pid);
  uint pa = IDX_PHYSICAL(idx);
  
  // 2.
  PID[idx] = -1;
  VPN[idx] = 0;
  PTE_XV6[idx] = 0;

  // 3.
  kv = (uint)P2V(pa);

  memset((void *)kv, 1, PGSIZE); //TODO: You must perform memset for P2V(physical address);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
char*
kalloc(int pid, char *v)
{

  int idx = -1;

  struct run *r;
 
  if(kmem.use_lock)
    acquire(&kmem.lock);

  //TODO: Fill the code that supports kalloc
  //1. Find the freespace by hash function
  //2. Consider the case that v is -1, which means that the caller of kalloc is kernel so the virtual address is decided by the allocated physical address (P2V) 
  //3. Update the value of PID[idx] and VPN[idx] (Do not update the PTE_XV6[idx] in this code!)
  //4. Return (char*)P2V(physical address), if there is no free space, return 0

  uint pa = 0;

  // 3.
  if ((int)v != -1){
    //cprintf("in kalloc : v : 0x%x", (int)v);
    /*
    if((uint)v < KERNBASE)
      idx = searchidx((uint)v, pid, 1, 0); // 1.
    else{
      pid = 0;
      idx = searchidx((uint)v, pid, 1, 1);
    }
    */
    idx = searchidx_alloc((uint)v, pid, 0);
  }
  // kernel called with v == -1
  else{
    // idx = searchidx_kern();
    pid = 0;
    //v = (char*)KERNBASE;
    //idx = searchidx((uint)v, pid, 1, 1);
    //v = (char*)KERNBASE;
    idx = searchidx_kern();
  }

  if(kmem.use_lock)
    release(&kmem.lock);

  if (idx == -1)
    return 0;
  
  PID[idx] = pid;
  if (pid != 0)
    VPN[idx] = VPN_MACRO(v);
  else
    VPN[idx] = VPN_MACRO(KERNBASE);
  pa = IDX_PHYSICAL(idx);

  return (char*)P2V(pa);
}

/*
char*
kalloc(void)
{
  struct run *r;

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  if(kmem.use_lock)
    release(&kmem.lock);
  return (char*)r;
}
*/


int searchidx(uint va, int pid){
  int idx = 0;
  uint vpn = VPN_MACRO(va);
  
  // hash function : simple modulo
  idx = vpn % MAXENTRY;
 
  
  for(idx = idx; idx < MAXENTRY; idx++){
    if (PID[idx] == pid && VPN[idx] == vpn) 
      break;
  }

  if (idx >= MAXENTRY)
    idx = searchidx_alloc(va, pid, 0);
  
  return idx;
}

int searchidx_kern(){
  int idx = 0;

  for(idx = idx;idx < MAXENTRY; idx++){
    if(PID[idx] == -1)
      break;
  }

  return idx;
}

int searchidx_alloc(uint va, int pid, int print){
  int idx = 0;
  uint vpn = VPN_MACRO(va);
  
  // hash function : simple modulo
  idx = vpn % MAXENTRY;
 
  for(idx = idx; idx < MAXENTRY; idx++){
    if(PID[idx] == -1)
      break;
    if(print){
      if (PID[idx] != pid)
        cprintf("[Hash collision for idx: %d]  PID: %d, VA: 0x%x, PID is different\n", idx, pid, va);
      else if (VPN[idx] != vpn) 
        cprintf("[Hash collision for idx: %d]  PID: %d, VA: 0x%x, VA is different\n", idx, pid, va);
    }
  }


  if(print)
    cprintf("[Completion idx: %d] PID: %d, VA: 0x%x\n", idx, pid, va);
  
  return idx;
}

void __print_hash_collision(){
  searchidx_alloc(0, 0, 1);
}
