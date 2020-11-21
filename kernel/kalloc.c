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
} kmem[NCPU];

void
kinit()
{
  for(int i = 0; i < NCPU; i++){ // vytvorime zamok pre kazdy cpu
    initlock(&kmem[i].lock, "kmem");
  }
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

  push_off(); // zakazem prerusenia
  uint64 cpuNumber = cpuid(); // zistim ake je aktualne CPU v praci
  pop_off(); // otvorim dvere pre prerusenia

  acquire(&kmem[cpuNumber].lock); // budem uzamykat iba zamok daneho CPU
  r->next = kmem[cpuNumber].freelist; // pridam do linked listu daneho CPU
  kmem[cpuNumber].freelist = r; // pridam do linked listu daneho CPU
  release(&kmem[cpuNumber].lock); // uvolnim zamok pre dany CPU
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  push_off(); // zakazem prerusenia
  uint64 cpuNumber = cpuid(); // zistim ake je aktualne CPU v praci
  pop_off(); // otvorim dvere pre prerusenia

  acquire(&kmem[cpuNumber].lock);
  r = kmem[cpuNumber].freelist; // vyberame si pamat z freelistu
  if(r) // pozriem ci nieco je nieco vo freeliste
    kmem[cpuNumber].freelist = r->next; // pridam tam nieco ak je
  release(&kmem[cpuNumber].lock); // uvolnim zamok

  if (!r) // ak sa nic nenachadza vo freeliste
  {
    // tak prebehnem cyklom vsetky CPU a hladam ci ma niaky zo susedov volnu pamat
    for (int i = 0; i < NCPU; i++)
    {
      if ((uint64)i == cpuNumber) // preskocim svoj CPU(samozrejme)
        continue;
      acquire(&kmem[i].lock); // zamknem danu oblast
      r = kmem[i].freelist; // pozrem do susedovho freeListu
      if (r) // ak ma volno
      {
        kmem[i].freelist = r->next; //tak mu to tam lusknem
        release(&kmem[i].lock); //tak mu to tam lusknem
        break;
      }
      release(&kmem[i].lock); // uvolnim jeho zamok
    }
  }
  return (void*)r;
}
