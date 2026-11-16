// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

// Reference counts for physical pages, used by copy-on-write fork.
// Several page tables can map the same physical page after a COW fork,
// so a page may only go back to the freelist when the last mapping is
// gone.  Indexed by physical page number.
// Sized for the RAM between KERNBASE and PHYSTOP: an int per page is
// 128 KB here, which matters because cowtest deliberately drives the
// machine close to out-of-memory.
struct {
  struct spinlock lock;
  int count[(PHYSTOP - KERNBASE) / PGSIZE];
} kref;

static int
pgindex(uint64 pa)
{
  return (pa - KERNBASE) / PGSIZE;
}

void
kref_inc(uint64 pa)
{
  acquire(&kref.lock);
  kref.count[pgindex(pa)]++;
  release(&kref.lock);
}

// Drop one reference; returns the remaining count.
int
kref_dec(uint64 pa)
{
  int n;

  acquire(&kref.lock);
  n = --kref.count[pgindex(pa)];
  release(&kref.lock);

  if(n < 0)
    panic("kref_dec: negative");
  return n;
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&kref.lock, "kref");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    // start at 1 so that the kfree() below takes it to 0 and really
    // does put the page on the freelist.
    kref.count[pgindex((uint64)p)] = 1;
    kfree(p);
  }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // somebody else still maps this page: keep it alive.
  if(kref_dec((uint64)pa) != 0)
    return;

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r){
    memset((char*)r, 5, PGSIZE); // fill with junk
    acquire(&kref.lock);
    kref.count[pgindex((uint64)r)] = 1;   // one reference: the caller
    release(&kref.lock);
  }
  return (void*)r;
}
