// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.
//
// The implementation uses two state flags internally:
// * B_VALID: the buffer data has been read from the disk.
// * B_DIRTY: the buffer data has been modified
//     and needs to be written to disk.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // head.next is most recently used.
  struct buf head;
} bcache;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

//PAGEBREAK!
  // Create linked list of buffers
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
__bget(uint dev, uint blockno, bool direct)
{
  struct buf *b;

  acquire(&bcache.lock);

  // Is the block already cached?
  if (!direct) {
    for(b = bcache.head.next; b != &bcache.head; b = b->next){
      if(b->dev == dev && b->blockno == blockno){
        b->refcnt++;
        release(&bcache.lock);
        acquiresleep(&b->lock);
        return b;
      }
    }
  }

  // Not cached; recycle an unused buffer.
  // Even if refcnt==0, B_DIRTY indicates a buffer is in use
  // because log.c has modified it but not yet committed it.
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0 && (b->flags & B_DIRTY) == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->flags = 0;
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  panic("bget: no buffers");
}

#define bget(a, b) __bget(a, b, false)
#define bget_direct(a, b) __bget(a, b, true)

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b, *b2;

  b = bget(dev, blockno);
  if (b->flags & B_VALID)
    return b;

  //TODO implement here

  // debug
  //cprintf("bread called with b->blockno %d\n", b->blockno);
  
  // RAID 5
  // first, get physical disk location of b->blockno
  struct buf *parity_block;
  int block_0_no = 0;
  int block_1_no = 0;
  int block_diskno = 0;
  int block_diskno_another = 0;
  int block_offset = b->blockno;

  // calculate parity block, raid block disk location
  if(b->blockno >= FSSIZE)
    block_offset = b->blockno - FSSIZE;
  
  int parityno = block_offset % 3;
  switch(parityno){
    case 0:
      block_0_no = 1;
      block_1_no = 2;
      break;
    case 1:
      block_0_no = 0;
      block_1_no = 2;
      break;
    case 2:
      block_0_no = 0;
      block_1_no = 1;
      break;
  }

  // calculate blockno disk location
  // disk block location : block_offset + (FSSIZE * block_diskno)
  if(b->blockno < FSSIZE){
    block_diskno = block_0_no;
    block_diskno_another = block_1_no;
  }
  else{
    block_diskno = block_1_no;
    block_diskno_another = block_0_no;
  }

  // debug
  //cprintf("> block_diskno : %d block_diskno_another : %d parityno : %d\n", block_diskno, block_diskno_another, parityno);

  // second, read block from disk. 
  // if block is broken disk, recover data with parity block
  if(BROKEN_DISK == block_diskno){
    b2 = bget_direct(b->dev, block_offset + (FSSIZE * block_diskno_another));
    parity_block = bget_direct(b->dev, block_offset + (FSSIZE * parityno));

    iderw(b2);
    iderw(parity_block);

    for(int i=0;i<BSIZE/sizeof(int); i++)
      b->udata[i] = b2->udata[i] ^ parity_block->udata[i];
    
    brelse(b2);
    brelse(parity_block);

    b->flags |= B_VALID;
  }
  // if block is not broken, read from block. we need to consider block offset
  else{
    b2 = bget_direct(b->dev, block_offset + (FSSIZE * block_diskno));
    iderw(b2);

    for(int i=0;i<BSIZE/sizeof(int); i++)
      b->udata[i] = b2->udata[i];
    
    brelse(b2);

    b->flags |= B_VALID;
  }

  
  /* RAID 1
  if (BROKEN_DISK == 0) {
    b2 = bget_direct(b->dev, b->blockno + FSSIZE);
    iderw(b2); //read

    for (int i = 0; i < BSIZE/sizeof(int); i++)
      b->udata[i] = b2->udata[i];

    brelse(b2);
    b->flags |= B_VALID;
  } else {
    iderw(b); //read
  }
  */

  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  struct buf *b2;

  if(!holdingsleep(&b->lock))
    panic("bwrite");

  //TODO implement here
  // debug
  //cprintf("bwrite called with b->blockno%d\n", b->blockno);
  
  // RAID 5
  // first, get physical disk location of b->blockno
  struct buf *parity_block, *b_clean;
  int block_0_no = 0;
  int block_1_no = 0;
  int block_diskno = 0;
  int block_diskno_another = 0;
  int block_offset = b->blockno;

  // calculate parity block, raid block disk location
  if(b->blockno >= FSSIZE)
    block_offset = b->blockno - FSSIZE;
  
  int parityno = block_offset % 3;

  switch(parityno){
    case 0:
      block_0_no = 1;
      block_1_no = 2;
      break;
    case 1:
      block_0_no = 0;
      block_1_no = 2;
      break;
    case 2:
      block_0_no = 0;
      block_1_no = 1;
      break;
  }

  // calculate blockno disk location
  // disk block location : block_offset + (FSSIZE * block_diskno)
  if(b->blockno < FSSIZE){
    block_diskno = block_0_no;
    block_diskno_another = block_1_no;
  }
  else{
    block_diskno = block_1_no;
    block_diskno_another = block_0_no;
  }

  // debug
  //cprintf("& block_diskno : %d block_diskno_another : %d parityno : %d\n", block_diskno, block_diskno_another, parityno);

  // recover the broken parityno and write
  // but, it is not needed because we cannot write parity block. 
  // just write the b into disk
  if(BROKEN_DISK == parityno){

    b_clean = bget_direct(b->dev, block_offset + (FSSIZE * block_diskno));
    // same as bread, we need to make duplicate due to block_diskno
    for(int i=0;i<BSIZE/sizeof(int);i++){
      b_clean->udata[i] = b->udata[i];
    }

    b_clean->flags |= B_DIRTY;
    iderw(b_clean);
    brelse(b_clean);
  }

  // recover the broken block_diskno_another and write parity block, b
  else if(BROKEN_DISK == block_diskno_another){
    b_clean = bget_direct(b->dev, block_offset + (FSSIZE * block_diskno));
    b2 = bget_direct(b->dev, block_offset + (FSSIZE * block_diskno_another));
    parity_block = bget_direct(b->dev, block_offset + (FSSIZE * parityno));

    iderw(b_clean);
    iderw(parity_block);

    // recover broken parity block & calculate new parity_block
    for(int i=0; i<BSIZE/sizeof(int);i++){
      b2->udata[i] = b_clean->udata[i] ^ parity_block->udata[i];
    }

    for(int i=0;i<BSIZE/sizeof(int);i++){
      parity_block->udata[i] = b->udata[i] ^ b2->udata[i];
    }

    // same as bread, we need to make duplicate due to block_diskno
    for(int i=0;i<BSIZE/sizeof(int);i++){
      b_clean->udata[i] = b->udata[i];
    }

    // write parity block & b_clean
    b_clean->flags |= B_DIRTY;
    iderw(b_clean);
    brelse(b_clean);

    parity_block->flags |= B_DIRTY;
    iderw(parity_block);
    brelse(parity_block);

    brelse(b2);
  }
  // just read the b_diskno_another and write the new parity block
  else if(BROKEN_DISK == block_diskno){
    b2 = bget_direct(b->dev, block_offset + (FSSIZE * block_diskno_another));
    parity_block = bget_direct(b->dev, block_offset + (FSSIZE * parityno));

    iderw(b2);

    // calculate new parity block
    for(int i=0;i<BSIZE/sizeof(int);i++)
      parity_block->udata[i] = b->udata[i] ^ b2->udata[i];

    brelse(b2);

    // write parity block
    parity_block->flags |= B_DIRTY;
    iderw(parity_block);
    brelse(parity_block);
  }
  // nothing is broken, so write new b, parity_block
  else{
    b_clean = bget_direct(b->dev, block_offset + (FSSIZE * block_diskno));
    b2 = bget_direct(b->dev, block_offset + (FSSIZE * block_diskno_another));
    parity_block = bget_direct(b->dev, block_offset + (FSSIZE * parityno));
    
    iderw(b2);

    // calculate new parity block
    for(int i=0;i<BSIZE/sizeof(int);i++)
      parity_block->udata[i] = b->udata[i] ^ b2->udata[i];

    // same as bread, we need to make duplicate due to block_diskno
    for(int i=0;i<BSIZE/sizeof(int);i++){
      b_clean->udata[i] = b->udata[i];
    }

    brelse(b2);

    // write parity block & b
    parity_block->flags |= B_DIRTY;

    iderw(parity_block);
    brelse(parity_block);

    b_clean->flags |= B_DIRTY;
    iderw(b_clean);
    brelse(b_clean);

  }
  
  // b->flags도 dirty로 만들어줘야 한다. 원래 buffer이니까
  b->flags |= B_DIRTY;


  /*
  // RAID 1
  if (BROKEN_DISK != 1) {
    b2 = bget_direct(b->dev, b->blockno + FSSIZE);

    for (int i = 0; i < BSIZE/sizeof(int); i++)
      b2->udata[i] = b->udata[i];

    b2->flags |= B_DIRTY;
    iderw(b2); //write
    brelse(b2);
  }
  b->flags |= B_DIRTY;
  iderw(b); //write
  */
}

// Release a locked buffer.
// Move to the head of the MRU list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  
  release(&bcache.lock);
}
//PAGEBREAK!
// Blank page.

