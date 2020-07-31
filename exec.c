#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "defs.h"
#include "x86.h"
#include "elf.h"

//assignment3
#ifdef NONE
#else
#ifdef LAPA
static uint default_age=0xFFFFFFFF;
#else
static uint default_age=0;
#endif
#endif


int
exec(char *path, char **argv)
{
  char *s, *last;
  int i, off;
  uint argc, sz, sp, ustack[3+MAXARG+1];
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pde_t *pgdir, *oldpgdir;
  struct proc *curproc = myproc();

//assignment3
#ifndef NONE
  int backup_pages_in_memory=curproc->pages_in_memory;   //backup PCB page info
  int backup_pages_in_swapfile=curproc->pages_in_swapfile;
  uint backup_pagedout=curproc->total_pagedout;
  uint backup_fault=curproc->page_faults;
  struct paging_meta_data backup_memory_pg_arr [MAX_PSYC_PAGES];
  struct disk_info backup_disk_pg_arr [MAX_PSYC_PAGES];
  struct paging_meta_data* backup_head;
  struct paging_meta_data* backup_tail;

  int index;
  for(index=0; index<MAX_PSYC_PAGES; index++)  {   //backup paging meta-data
    backup_memory_pg_arr[index].va=curproc->memory_pg_arr[index].va;
    backup_memory_pg_arr[index].next=curproc->memory_pg_arr[index].next;
    backup_memory_pg_arr[index].prev=curproc->memory_pg_arr[index].prev;
    backup_memory_pg_arr[index].age=curproc->memory_pg_arr[index].age;
    backup_disk_pg_arr[index].va=curproc->disk_pg_arr[index].va;
  }
  backup_head=curproc->head;
  backup_tail=curproc->tail;
#endif


  begin_op();

  if((ip = namei(path)) == 0){
    end_op();
    cprintf("exec: fail\n");
    return -1;
  }
  ilock(ip);
  pgdir = 0;

  // Check ELF header
  if(readi(ip, (char*)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;
  if(elf.magic != ELF_MAGIC)
    goto bad;

  if((pgdir = setupkvm()) == 0)
    goto bad;

  //assignment3
  #ifndef NONE
  curproc->pages_in_memory=0;   //reset counters
  curproc->pages_in_swapfile=0;
  curproc->total_pagedout=0;
  curproc->page_faults=0;
  for(index=0; index<MAX_PSYC_PAGES; index++)  {   //reset page meta-data
    curproc->memory_pg_arr[index].va=(char*)-1;
    curproc->memory_pg_arr[index].next=0;
    curproc->memory_pg_arr[index].prev=0;
    curproc->memory_pg_arr[index].age=default_age;
    curproc->disk_pg_arr[index].va=(char*)-1;
  }
  curproc->head=0;
  curproc->tail=0;
  #endif


  // Load program into memory.
  sz = 0;
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, (char*)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz)
      goto bad;
    if(ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    if((sz = allocuvm(pgdir, sz, ph.vaddr + ph.memsz)) == 0)
      goto bad;
    if(ph.vaddr % PGSIZE != 0)
      goto bad;
    if(loaduvm(pgdir, (char*)ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
  iunlockput(ip);
  end_op();
  ip = 0;

  // Allocate two pages at the next page boundary.
  // Make the first inaccessible.  Use the second as the user stack.
  sz = PGROUNDUP(sz);
  if((sz = allocuvm(pgdir, sz, sz + 2*PGSIZE)) == 0)
    goto bad;
  clearpteu(pgdir, (char*)(sz - 2*PGSIZE));
  sp = sz;

  // Push argument strings, prepare rest of stack in ustack.
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG)
      goto bad;
    sp = (sp - (strlen(argv[argc]) + 1)) & ~3;
    if(copyout(pgdir, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[3+argc] = sp;
  }
  ustack[3+argc] = 0;

  ustack[0] = 0xffffffff;  // fake return PC
  ustack[1] = argc;
  ustack[2] = sp - (argc+1)*4;  // argv pointer

  sp -= (3+argc+1) * 4;
  if(copyout(pgdir, sp, ustack, (3+argc+1)*4) < 0)
    goto bad;

  // Save program name for debugging.
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(curproc->name, last, sizeof(curproc->name));

  //assignment3
  #ifndef NONE//where we reset the swapfile
  if(!(curproc->pid==1 || (curproc->parent->pid==1)))  {
    removeSwapFile(curproc);   //remove old swapfile
    createSwapFile(curproc);   // create new swapfile
  }
  #endif

  // Commit to the user image.
  oldpgdir = curproc->pgdir;
  curproc->pgdir = pgdir;
  curproc->sz = sz;
  curproc->tf->eip = elf.entry;  // main
  curproc->tf->esp = sp;
  switchuvm(curproc);
  freevm(oldpgdir);
  return 0;

 bad:
  if(pgdir)
    freevm(pgdir);
  if(ip){
    iunlockput(ip);
    end_op();
  }

  //assignment3
  #ifndef NONE
  curproc->pages_in_swapfile = backup_pages_in_swapfile;   //update counters with backup
  curproc->pages_in_memory = backup_pages_in_memory;
  curproc->total_pagedout = backup_pagedout;
  curproc->page_faults = backup_fault;
  for(i=0; i<MAX_PSYC_PAGES ;i++)  {   //update page meta-data with backup
    curproc->memory_pg_arr[i].va = backup_memory_pg_arr[i].va;
    curproc->memory_pg_arr[i].next = backup_memory_pg_arr[i].next;
    curproc->memory_pg_arr[i].prev = backup_memory_pg_arr[i].prev;
    curproc->memory_pg_arr[i].age = backup_memory_pg_arr[i].age;
    curproc->disk_pg_arr[i].va = backup_disk_pg_arr[i].va;
  }
  curproc->head=backup_head;
  curproc->tail=backup_tail;
  #endif


  return -1;
}
