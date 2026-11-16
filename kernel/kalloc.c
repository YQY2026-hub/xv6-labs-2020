// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.
//
// Each CPU has its own free list and its own lock, so the common case
// (allocate/free on the running CPU) never contends with another CPU.
// A CPU that runs dry steals half of another CPU's list; that happens
// rarely, so the extra lock traffic costs nothing.

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
} kmem[NCPU];

void
kinit()
{
  // Every lock name must start with "kmem" so that the statistics
  // syscall used by kalloctest picks them up.
  for(int i = 0; i < NCPU; i++)
    initlock(&kmem[i].lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
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

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  // cpuid() is only valid with interrupts disabled, so bracket it with
  // push_off/pop_off.  Interrupts stay off until the page is linked in,
  // which also means the CPU cannot change underneath us.
  push_off();
  int id = cpuid();
  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  release(&kmem[id].lock);
  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  push_off();
  int id = cpuid();

  acquire(&kmem[id].lock);
  r = kmem[id].freelist;
  if(r)
    kmem[id].freelist = r->next;
  release(&kmem[id].lock);

  // This CPU is out of memory: steal half of another CPU's free list.
  if(r == 0){
    for(int i = 1; i < NCPU; i++){
      int j = (id + i) % NCPU;
      acquire(&kmem[j].lock);
      struct run *p = kmem[j].freelist;
      if(p != 0){
        int n = 0;
        for(struct run *q = p; q != 0; q = q->next)
          n++;
        if(n == 1){
          // Only one page left; take it so we do not give up too early.
          kmem[j].freelist = 0;
          r = p;
        } else {
          // Leave the first n/2 pages on the donor, take the rest.
          int keep = n / 2;
          struct run *last = p;
          for(int k = 1; k < keep; k++)
            last = last->next;
          r = last->next;
          last->next = 0;
        }
      }
      release(&kmem[j].lock);
      if(r != 0)
        break;
    }
  }

  pop_off();

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
