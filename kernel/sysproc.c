#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "fcntl.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}


uint64
sys_mmap(void){
  uint64 addr;
  int length, prot, flags, fd, offset;
  struct file* pf;
  argaddr(0,&addr); // zoberieme prvy argument typu adresa a pridame ho do addr
  argint(1,&length); // zoberieme druhy argument typu int a pridame ho do length
  argint(2,&prot); // zoberieme treti argument typu int a pridame ho do prot
  argint(3,&flags); // zoberieme stvrty argument typu int a pridame ho do flags
  argfd(4,&fd,&pf); // zoberieme piaty argument typu fd a pridame ho do pf
  argint(5,&offset); // zoberieme siesty argument typu int a pridame ho do offset // ten je v nasom pripade nula ale nechame ho tu
  // pozreme ci do suboru do ktoreho budeme chciet v bud. zapisovat je vobec mozne zapisovat ak nie tak dovi
  if((prot & PROT_WRITE) && !pf->writable && flags == MAP_SHARED)
    return -1UL;
  struct proc *p = myproc(); // najdeme aktualny proces
  for(int i = 0; i < NVMA; i++){ // prechadzame vsetky vma elementy
    if(p->vmatable[i].used != 1){ // pozrem ci je dany vma pouzity
      // ak neni tak ho nastavim
      struct vma* v = &p->vmatable[i];
      v->addr = p->sz;
      v->length = length;
      v->pf = pf;
      v->prot = prot;
      v->used = 1; // uz ho vieme nastavit na pouzity
      v->flags = flags;
      v->offset = offset;
      growproc(length); // zvacsi pamat procesu o length
      filedup(pf); // inkrementujeme referenciu na dany subor
      begin_op(); // zistujem ci sa vykonava niaky commit(zapisanie blokov z bcache na disk)
      ilock(pf->ip); // zamyka inode ktory sa pouziva a ostatny s nim nevedia nic robit
      readi(pf->ip,1,v->addr,offset,length); // nacita data z inodu -by A.D. 2020-q4
      iunlock(pf->ip); // odomykneme nas inode
      end_op(); // ukoncim pracu s FS(a lot is happening)
      return v->addr; // vratim adresu
    };
  }
  return -1UL; // nenasla sa ziadna vma ktora nieje used
}

uint64
sys_munmap(void){
  uint64 addr;
  int length;
  argaddr(0,&addr); //
  argint(1,&length);  //
  struct proc *p = myproc(); // najdem aktualny proces
  for(int i = 0; i < NVMA; i++){ // prechadzam vsetko vma ktore mame
    struct vma* v = &p->vmatable[i]; // dam si do premennej jeden vma
    // hladam v ktorom z vma[NVAM] sa nachadza addr zo vstupu, musi splnat tuto podmienku :
    // adresa musi byt v intervale <v->addr;v->addr + v->length> a zaroven musi byt pouzita
    if(addr >= v->addr && addr < v->addr + v->length && v->used == 1){
      if(v->flags == MAP_SHARED){ // ak je najdeny vma s flagom MAP_SHARED(hovori ze mozu viaceri pristupovat) (git push)
        begin_op();  //
        ilock(v->pf->ip);  //
        writei(v->pf->ip,1,addr,v->offset+addr-v->addr,length); // idem zapisovat zmeny kedze mame MAP_SHARED
        iunlock(v->pf->ip);  //
        end_op();  //
      }
      uvmunmap(p->pagetable,addr,length/PGSIZE,1); // odmapovanie addr z pagetablu daneho procesu
      // in the middle, create another vma in vmatable
      if(addr > v->addr){
        // covers all the rest
        if(addr + length == v->addr + v->length){ // porovnanie koncov ktore maze a aktualnu vma
          v->length = addr - v->addr; // maze sa od konca
        } else {
          panic("sys_munmap: not going to happen");
          // vmadup(v,addr+length);
        }
      } else { // v->addr == addr
        // in the beginning
        if(length == v->length){ // porovnam dlzky
          // totally overlap
          filederef(v->pf); // su rovnake tak iba uberiem referenciu
        } else { // niesu rovnake
          // not totally
          v->addr = addr + length; // v->addr posuniem na koniec useku ktory uvolnujem
          v->length -= length; // dlzka v->length sa zmensi o dlzku uvolnovaneho useku
        }
      }
      return 0;
    }
  }
  return -1;
}
