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
// The cache is split into NBUCKET hash chains, each with its own spinlock,
// so that lookups of different blocks do not serialise on one global lock.
// A global lock serialises the miss path (choosing a victim and recycling
// it), which keeps the "at most one cached copy of a block" invariant easy
// to maintain.  Hits never take the global lock, and that is the common case.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

// A prime number of buckets, so that blocks that differ by a small
// multiple do not all land in the same bucket.  Keep it small: initlock()
// registers every lock in a fixed table of NLOCK (500) entries, and the
// rest of the kernel already uses about 80 of them.
#define NBUCKET 13
#define BHASH(blockno) ((blockno) % NBUCKET)

struct {
  struct spinlock lock;               // serialises the miss/recycle path
  struct buf buf[NBUF];
  struct spinlock bucketlock[NBUCKET];// one lock per hash chain
  struct buf *bucket[NBUCKET];        // hash chains of cached blocks
} bcache;

// Remove b from the chain it is on.  Caller holds that chain's lock.
static void
bunlink(struct buf *b)
{
  struct buf **pp = &bcache.bucket[BHASH(b->blockno)];
  while(*pp != 0 && *pp != b)
    pp = &(*pp)->next;
  if(*pp != 0)
    *pp = b->next;
  b->next = 0;
}

// Put b at the head of the chain for its current blockno.
// Caller holds that chain's lock.
static void
binsert(struct buf *b)
{
  int i = BHASH(b->blockno);
  b->next = bcache.bucket[i];
  bcache.bucket[i] = b;
}

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");
  for(int i = 0; i < NBUCKET; i++){
    initlock(&bcache.bucketlock[i], "bcache.bucket");
    bcache.bucket[i] = 0;
  }

  // Every buffer starts unused: dev and blockno are 0, which hashes to
  // bucket 0, so park them all there.  bget picks victims among the
  // buffers with refcnt == 0 wherever they happen to sit.
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->valid = 0;
    b->disk = 0;
    b->dev = 0;
    b->blockno = 0;
    b->refcnt = 0;
    b->lastuse = 0;
    initsleeplock(&b->lock, "buffer");
    b->next = bcache.bucket[0];
    bcache.bucket[0] = b;
  }
}

// Look through the buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int i = BHASH(blockno);

  // Fast path: the block is already cached.  Only this bucket's lock is
  // needed, so blocks in different buckets are looked up in parallel.
  acquire(&bcache.bucketlock[i]);
  for(b = bcache.bucket[i]; b != 0; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      b->lastuse = ticks;
      release(&bcache.bucketlock[i]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.bucketlock[i]);

  // Slow path.  Holding the global lock means no other process is
  // recycling a buffer at the same time, which is what guarantees that a
  // block never ends up with two cached copies.
  acquire(&bcache.lock);

  // Someone may have brought the block in between the two locks.
  acquire(&bcache.bucketlock[i]);
  for(b = bcache.bucket[i]; b != 0; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      b->lastuse = ticks;
      release(&bcache.bucketlock[i]);
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.bucketlock[i]);

  // Look for the least recently used buffer that nobody holds, walking
  // the buckets one at a time.  Taking each bucket lock while inspecting
  // its chain is what makes the refcnt == 0 test trustworthy.
  for(int k = 0; k < NBUCKET; k++){
    int j = (i + k) % NBUCKET;

    acquire(&bcache.bucketlock[j]);
    struct buf *victim = 0;
    for(b = bcache.bucket[j]; b != 0; b = b->next){
      if(b->refcnt == 0 && (victim == 0 || b->lastuse < victim->lastuse))
        victim = b;
    }
    if(victim == 0){
      release(&bcache.bucketlock[j]);
      continue;
    }

    bunlink(victim);          // still uses victim->blockno, i.e. bucket j
    victim->dev = dev;
    victim->blockno = blockno;
    victim->valid = 0;
    victim->refcnt = 1;
    victim->lastuse = ticks;

    if(j == i){
      binsert(victim);        // bucket i is already locked
      release(&bcache.bucketlock[i]);
    } else {
      acquire(&bcache.bucketlock[i]);
      binsert(victim);
      release(&bcache.bucketlock[i]);
      release(&bcache.bucketlock[j]);
    }

    release(&bcache.lock);
    acquiresleep(&victim->lock);
    return victim;
  }

  release(&bcache.lock);
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Just drop the reference and record when it was last used; the buffer
// stays where it is in its bucket, and bget uses lastuse to decide which
// buffer to recycle.  No global lock needed, so this is cheap.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int i = BHASH(b->blockno);
  acquire(&bcache.bucketlock[i]);
  b->refcnt--;
  b->lastuse = ticks;
  release(&bcache.bucketlock[i]);
}

void
bpin(struct buf *b) {
  int i = BHASH(b->blockno);
  acquire(&bcache.bucketlock[i]);
  b->refcnt++;
  release(&bcache.bucketlock[i]);
}

void
bunpin(struct buf *b) {
  int i = BHASH(b->blockno);
  acquire(&bcache.bucketlock[i]);
  b->refcnt--;
  release(&bcache.bucketlock[i]);
}
