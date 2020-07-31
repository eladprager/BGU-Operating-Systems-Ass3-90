#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "elf.h"

extern char data[];  // defined by kernel.ld
pde_t *kpgdir;  // for use in scheduler()"not found va trap14\n")

//assignment3

#ifdef NONE
static uint default_age=0;
#else
#ifdef LAPA
static uint default_age=0xffffffff;
#else
static uint default_age=0;
#endif
#endif


// Set up CPU's kernel segment descriptors.
// Run once on entry on each CPU.
void
seginit(void)
{
  struct cpu *c;

  // Map "logical" addresses to virtual addresses using identity map.
  // Cannot share a CODE descriptor for both kernel and user
  // because it would have to have DPL_USR, but the CPU forbids
  // an interrupt from CPL=0 to DPL=3.
  c = &cpus[cpuid()];
  c->gdt[SEG_KCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, 0);
  c->gdt[SEG_KDATA] = SEG(STA_W, 0, 0xffffffff, 0);
  c->gdt[SEG_UCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, DPL_USER);
  c->gdt[SEG_UDATA] = SEG(STA_W, 0, 0xffffffff, DPL_USER);
  lgdt(c->gdt, sizeof(c->gdt));
}

// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.
static pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;

  pde = &pgdir[PDX(va)];
  if(*pde & PTE_P){
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
  } else {
    if(!alloc || (pgtab = (pte_t*)kalloc()) == 0)
      return 0;
    // Make sure all those PTE_P bits are zero.
    memset(pgtab, 0, PGSIZE);
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table
    // entries, if necessary.
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  return &pgtab[PTX(va)];
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned.
static int
mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)
{
  char *a, *last;
  pte_t *pte;

  a = (char*)PGROUNDDOWN((uint)va);
  last = (char*)PGROUNDDOWN(((uint)va) + size - 1);
  for(;;){
    if((pte = walkpgdir(pgdir, a, 1)) == 0)
      return -1;
    if(*pte & PTE_P)
      panic("remap");
    *pte = pa | perm | PTE_P;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// There is one page table per process, plus one that's used when
// a CPU is not running any process (kpgdir). The kernel uses the
// current process's page table during system calls and interrupts;
// page protection bits prevent user code from using the kernel's
// mappings.
//
// setupkvm() and exec() set up every page table like this:
//
//   0..KERNBASE: user memory (text+data+stack+heap), mapped to
//                phys memory allocated by the kernel
//   KERNBASE..KERNBASE+EXTMEM: mapped to 0..EXTMEM (for I/O space)
//   KERNBASE+EXTMEM..data: mapped to EXTMEM..V2P(data)
//                for the kernel's instructions and r/o data
//   data..KERNBASE+PHYSTOP: mapped to V2P(data)..PHYSTOP,
//                                  rw data +int 
//   0xfe000000..0: mapped direct (devices such as ioapic)
//
// The kernel allocates physical memory for its heap and for user memory
// between V2P(end) and the end of physical memory (PHYSTOP)
// (directly addressable from end..P2V(PHYSTOP)).

// This table defines the kernel's mappings, which are present in
// every process's page table.
static struct kmap {
  void *virt;
  uint phys_start;
  uint phys_end;
  int perm;
} kmap[] = {
 { (void*)KERNBASE, 0,             EXTMEM,    PTE_W}, // I/O space
 { (void*)KERNLINK, V2P(KERNLINK), V2P(data), 0},     // kern text+rodata
 { (void*)data,     V2P(data),     PHYSTOP,   PTE_W}, // kern data+memory
 { (void*)DEVSPACE, DEVSPACE,      0,         PTE_W}, // more devices
};

// Set up kernel part of a page table.
pde_t*
setupkvm(void)
{
  pde_t *pgdir;
  struct kmap *k;

  if((pgdir = (pde_t*)kalloc()) == 0)
    return 0;
  memset(pgdir, 0, PGSIZE);
  if (P2V(PHYSTOP) > (void*)DEVSPACE)
    panic("PHYSTOP too high");
  for(k = kmap; k < &kmap[NELEM(kmap)]; k++)
    if(mappages(pgdir, k->virt, k->phys_end - k->phys_start,
                (uint)k->phys_start, k->perm) < 0) {
      freevm(pgdir);
      return 0;
    }
  return pgdir;
}

// Allocate one page table for the machine for the kernel address
// space for scheduler processes.
void
kvmalloc(void)
{
  kpgdir = setupkvm();
  switchkvm();
}

// Switch h/w page table register to the kernel-only page table,
// for when no process is running.
void
switchkvm(void)
{
  lcr3(V2P(kpgdir));   // switch to the kernel page table
}

// Switch TSS and h/w page table to correspond to process p.
void
switchuvm(struct proc *p)
{
  if(p == 0)
    panic("switchuvm: no process");
  if(p->kstack == 0)
    panic("switchuvm: no kstack");
  if(p->pgdir == 0)
    panic("switchuvm: no pgdir");

  pushcli();
  mycpu()->gdt[SEG_TSS] = SEG16(STS_T32A, &mycpu()->ts,
                                sizeof(mycpu()->ts)-1, 0);
  mycpu()->gdt[SEG_TSS].s = 0;
  mycpu()->ts.ss0 = SEG_KDATA << 3;
  mycpu()->ts.esp0 = (uint)p->kstack + KSTACKSIZE;
  // setting IOPL=0 in eflags *and* iomb beyond the tss segment limit
  // forbids I/O instructions (e.g., inb and outb) from user space
  mycpu()->ts.iomb = (ushort) 0xFFFF;
  ltr(SEG_TSS << 3);
  lcr3(V2P(p->pgdir));  // switch to process's address space
  popcli();
}

// Load the initcode into address 0 of pgdir.
// sz must be less than a page.
void
inituvm(pde_t *pgdir, char *init, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pgdir, 0, PGSIZE, V2P(mem), PTE_W|PTE_U);
  memmove(mem, init, sz);
}

// Load a program segment into pgdir.  addr must be page-aligned
// and the pages from addr to addr+sz must already be mapped.
int
loaduvm(pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz)
{
  uint i, pa, n;
  pte_t *pte;
  if((uint) addr % PGSIZE != 0)
    panic("loaduvm: addr must be page aligned");
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, addr+i, 0)) == 0)
      panic("loaduvm: address should exist");
    pa = PTE_ADDR(*pte);
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip, P2V(pa), offset+i, n) != n)
      return -1;
  }
  return 0;
}

// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
int
old_allocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  char *mem;
  uint a;

  if(newsz >= KERNBASE)
    return 0;
  if(newsz < oldsz)
    return oldsz;

  a = PGROUNDUP(oldsz);
  for(; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      cprintf("allocuvm out of memory\n");
      deallocuvm(pgdir, newsz, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pgdir, (char*)a, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0){
      cprintf("allocuvm out of memory (2)\n");
      deallocuvm(pgdir, newsz, oldsz);
      kfree(mem);
      return 0;
    }
  }
  return newsz;
}

// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
int
allocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
//assignment3
#ifdef NONE
  return old_allocuvm(pgdir,oldsz,newsz);
#endif
if((myproc()!=0) &&  ((myproc()->pid==1)  ||  (myproc()->parent->pid==1)))
  return old_allocuvm(pgdir,oldsz,newsz);


char *mem;
uint a;
struct proc* p =myproc();
if(newsz >= KERNBASE)
  return 0;
if(newsz < oldsz)
  return oldsz;

a = PGROUNDUP(oldsz);
for(; a < newsz; a += PGSIZE){
  mem = kalloc();
  if(mem == 0){
    cprintf("allocuvm out of memory\n");
    deallocuvm(pgdir, newsz, oldsz);
    return 0;
  }
  memset(mem, 0, PGSIZE);
  if(mappages(pgdir, (char*)a, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0){
    cprintf("allocuvm out of memory (2)\n");
    deallocuvm(pgdir, newsz, oldsz);
    kfree(mem);
    return 0;
  }

//assignment3
  if(p->pages_in_memory>=MAX_PSYC_PAGES) {   //if no free space in memory
    // allocate_space_in_memory(p);
    allocate_space_in_memory(p);
    move_page_to_memory(p, (char*)a);
  } else   //if there is free space in memory
      move_page_to_memory(p, (char*)a);
}
return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
int
deallocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  //assignment 3
  #ifndef NONE
  struct proc* p =myproc();
  #endif

  pte_t *pte;
  uint a, pa;

  if(newsz >= oldsz)
    return oldsz;

  a = PGROUNDUP(newsz);
  for(; a  < oldsz; a += PGSIZE){

    //assignment3
    #ifndef NONE
    int found=0;
    #endif


    pte = walkpgdir(pgdir, (char*)a, 0);
    if(!pte)
      a = PGADDR(PDX(a) + 1, 0, 0) - PGSIZE;
    else if((*pte & PTE_P) != 0){
      pa = PTE_ADDR(*pte);
      if(pa == 0)
        panic("deallocuvm kfree");
      char *v = P2V(pa);

      //assignment3
      #ifndef NONE
      int i=0;

      for(i=0; i<MAX_PSYC_PAGES; i++)      {   //search for the right page and remove it from queue
        if(p->memory_pg_arr[i].va == (char*)a)        {
          if(p->memory_pg_arr[i].next != 0)
            p->memory_pg_arr[i].next->prev = p->memory_pg_arr[i].prev;
          if(p->memory_pg_arr[i].prev != 0)
            p->memory_pg_arr[i].prev->next = p->memory_pg_arr[i].next;
          if(&p->memory_pg_arr[i] == p->head)
            p->head = p->head->next;
          if(&p->memory_pg_arr[i] == p->tail)
            p->tail=p->tail->prev;
          p->memory_pg_arr[i].next = 0;   //de-allocate page
          p->memory_pg_arr[i].prev = 0;
          p->memory_pg_arr[i].age = default_age;
          p->memory_pg_arr[i].va = (char*)-1;
          found++;
          if(p->pgdir == pgdir)
            p->pages_in_memory--;   //decrease pages in memory counter 
        }
      }
      #endif

      
      kfree(v);
      *pte = 0;
    }

    //assignment3
    #ifndef NONE
    else if((*pte & PTE_PG) != 0)
    {
      *pte=0;
      int i;
      for(i=0; i<MAX_PSYC_PAGES; i++)      {   //search for the right page and de-allocate it
        if(p->disk_pg_arr[i].va == (char*)a)        {
          p->disk_pg_arr[i].va = (char*)-1;
          found++;
          if(p->pgdir == pgdir)
            p->pages_in_swapfile--;   //decrease pages in swapfile counter 
        }
      }
    }

    if(found>1)
      panic("found same va more than once\n");
    #endif

  }
  return newsz;
}

// Free a page table and all the physical memory pages
// in the user part.
void
freevm(pde_t *pgdir)
{
  uint i;
  if(pgdir == 0)
    panic("freevm: no pgdir");
  deallocuvm(pgdir, KERNBASE, 0);
  for(i = 0; i < NPDENTRIES; i++){
    if(pgdir[i] & PTE_P){
      char * v = P2V(PTE_ADDR(pgdir[i]));
      kfree(v);
    }
  }
  kfree((char*)pgdir);
}

// Clear PTE_U on a page. Used to create an inaccessible
// page beneath the user stack.
void
clearpteu(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if(pte == 0)
    panic("clearpteu");
  *pte &= ~PTE_U;
}

// Given a parent process's page table, create a copy
// of it for a child.
pde_t*
copyuvm(pde_t *pgdir, uint sz)
{
  pde_t *d;
  pte_t *pte;
  uint pa, i, flags;
  char *mem;

  if((d = setupkvm()) == 0)
    return 0;
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, (void *) i, 0)) == 0)
      panic("copyuvm: pte should exist");
    if(!(*pte & PTE_P)&&(!(*pte & PTE_PG)))
      panic("copyuvm: page not present");
    {
      if(*pte& PTE_PG)
      {
      update_pagedout_entry(d,i);
      continue;
      }
    }
    pa = PTE_ADDR(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto bad;
    memmove(mem, (char*)P2V(pa), PGSIZE);
    if(mappages(d, (void*)i, PGSIZE, V2P(mem), flags) < 0)
      goto bad;
  }
  return d;

bad:
  freevm(d);
  return 0;
}

//PAGEBREAK!
// Map user virtual address to kernel address.
char*
uva2ka(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if((*pte & PTE_P) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  return (char*)P2V(PTE_ADDR(*pte));
}

// Copy len bytes from p to user address va in page table pgdir.
// Most useful when pgdir is not the current page table.
// uva2ka ensures this only works for PTE_U pages.
int
copyout(pde_t *pgdir, uint va, void *p, uint len)
{
  char *buf, *pa0;
  uint n, va0;

  buf = (char*)p;
  while(len > 0){
    va0 = (uint)PGROUNDDOWN(va);
    pa0 = uva2ka(pgdir, (char*)va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (va - va0);
    if(n > len)
      n = len;
    memmove(pa0 + (va - va0), buf, n);
    len -= n;
    buf += n;
    va = va0 + PGSIZE;
  }
  return 0;
}

//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.

//assignment3
// Given a parent process's page table, create a copy
// of it for a child.
pde_t *
cowuvm(pde_t *pgdir, uint sz)
{
  pde_t *d;
  pte_t *pte;
  uint pa, i, flags;
  // char *mem;

  if ((d = setupkvm()) == 0)
    return 0;
  for (i = 0; i < sz; i += PGSIZE)  {
    if ((pte = walkpgdir(pgdir, (void *)i, 0)) == 0)
      panic("copyuvm: pte should exist");
    if (!(*pte & PTE_P) && !(*pte & PTE_PG)) //
      panic("copyuvm: page not present");

    if(*pte & PTE_P){
      if(*pte & PTE_W)
        *pte  &= ~PTE_W;
      *pte |= PTE_COW;     
    }

    pa = PTE_ADDR(*pte);
    flags = PTE_FLAGS(*pte);
    if (mappages(d, (void *)i, PGSIZE, pa, flags) < 0)    {
      // kfree(mem);
      goto bad;
    }

    if (*pte & PTE_PG)    {
      pte_t *d_pte = walkpgdir(d, (char *)i, 0);
      *d_pte &= ~PTE_P;
    }

    lcr3(V2P(pgdir));
  }
  return d;

bad:
  freevm(d);
  return 0;
}

void 
on_page_fault(int va, pde_t * pgdir){
  myproc()->page_faults++;   //increase page fault counter

  pte_t *pte;
  pte = walkpgdir(pgdir, (char *)va, 0);

  if((*pte & PTE_COW) && ((*pte & PTE_W) == 0)){
    // cprintf("cow");
    copy_on_write((char*)va);
    return;
  }

  if(*pte & PTE_PG){
    // cprintf("pg");
    get_page_from_swapfile(va);
    return;
  }
}

void copy_on_write(char *va)
{
  struct proc *p = myproc();
  pte_t *pte = walkpgdir(p->pgdir, va, 0);
  uint flags, pa; 
  char *mem;
  pa = PTE_ADDR(*pte);
  flags = PTE_FLAGS(*pte);
  if ((mem = kalloc()) == 0)    {
    p->killed = 1;
    return;
  }
  memmove(mem, (char *)P2V(pa), PGSIZE);
  *pte = V2P(mem) | flags | PTE_P | PTE_W;
  *pte &= ~PTE_COW;

  lcr3(V2P(p->pgdir));
  return;
}

static char buff[PGSIZE];   //buffer used to store swapped page in get_page_from_swapfile method

int get_page_from_swapfile(int fault_address)
{
    struct proc* p=myproc();
    if(p==0)
    {return 0;}
    int fault_va = PGROUNDDOWN(fault_address);
    char * newPg = kalloc();
    memset(newPg, 0, PGSIZE);
    lcr3(V2P(myproc()->pgdir));   //refresh CR3 register

    if(p->pages_in_memory>=MAX_PSYC_PAGES) {  //swap needed
      char* va=0;
      struct paging_meta_data* to_remove=replacement_algo(p);   //select based on defined paging algorithem
      va=to_remove->va;
      to_remove->va=(char*)-1;
      p->pages_in_memory--;
      update_pagedin_entry(fault_va, V2P(newPg), p->pgdir);   //update pte of useraddress
      from_swapfile_to_memory(p,fault_va, buff);   //add to memory datastructre inside the function
      int outPagePAddr = get_pte_addr((int)va,p->pgdir);   //get page virtual address wtithout the offset of given vadress
      memmove(newPg, buff, PGSIZE);
      from_memory_to_swapfile(p, (int)va, p->pgdir);
      update_pagedout_entry(p->pgdir,(int)va);
      char *v = P2V(outPagePAddr);
      kfree(v);   //free swapped page
      return 1;

    } else {  //swap isnt needed
      update_pagedin_entry(fault_va, V2P(newPg), p->pgdir);
      from_swapfile_to_memory(p,fault_va, (char*)fault_va);
      return 1;
    }

    return 0;
}

int get_pte_addr(int userPageVAddr, pde_t * pgdir){
  pte_t *pte;
  pte = walkpgdir(pgdir, (int*)userPageVAddr, 0);
  if(!pte) //uninitialized page table
    return -1;
  return PTE_ADDR(*pte);
}

void allocate_space_in_memory(struct proc* p)
{
  struct paging_meta_data * page_to_swapfile=replacement_algo(p);   //select another page to swap from memory to swapfile 

  int i;
  for(i=0;i<MAX_PSYC_PAGES;i++)  {
    if(p->disk_pg_arr[i].va==(char*)-1)
      break;
  }

  p->disk_pg_arr[i].va = page_to_swapfile->va;  
  p->pages_in_swapfile++;  
  p->total_pagedout++;
  if(writeToSwapFile(p,(char*)(PTE_ADDR(page_to_swapfile->va)),i*PGSIZE,PGSIZE)<=0)
    panic("failled writeToSwapFile\n");

  pte_t *pte=walkpgdir(p->pgdir,(void*)page_to_swapfile->va,0);
  if(!*pte)
    panic("walkpgdir failed\n");
  page_to_swapfile->va=(char*)-1;
  p->pages_in_memory--;   //decrease pages in memory counter
  kfree((char*)PTE_ADDR(P2V_WO(*pte)));
  *pte=PTE_W|PTE_U|PTE_PG;
  lcr3(V2P(p->pgdir));
}

int 
from_swapfile_to_memory(struct proc* p, int fault_va, char* buffer)
{
  int i;
  int read = -1;


  for(i=0;i<MAX_PSYC_PAGES;i++)  {
    if(p->disk_pg_arr[i].va == (char*)fault_va)    {   //search swapfile for the fault_va page
      read = readFromSwapFile(p, buffer, i*PGSIZE, PGSIZE);
      p->disk_pg_arr[i].va = (char*)-1;   //change swapfile page va to default
      p->pages_in_swapfile--;   //decrease swapfile counter
      if(read == -1)      {   //if fails to read
        panic("failled to read from swapfile\n");
        return-1;
      }
      move_page_to_memory(p, (char*)fault_va);   //moving page to memory

      return read;
    }
  }
  return -2;
}

int 
from_memory_to_swapfile(struct proc * p, int va, pde_t *pgdir)
{
  int i;
  for(i=0; i<MAX_PSYC_PAGES; i++)  {
    if(p->disk_pg_arr[i].va==(char*)-1)    {   //search for free space in swapfile
      int res = writeToSwapFile(p, (char*)va, PGSIZE*i, PGSIZE);
      if(res==-1)
        return -1;
      p->disk_pg_arr[i].va=(char*)va;
      p->pages_in_swapfile++;   //increase pages in swapfile counter
      p->total_pagedout++;   //increase pagedout counter

      return res;
    }   
  }
  return -2;
}

void 
update_pagedin_entry(int fault_va, int new_page, pde_t * pgdir){
  pte_t *pte;
  pte = walkpgdir(pgdir, (int*)fault_va, 0);   //retrieve physical page entry by va
  if(!pte)
    panic("walkpgdir error");
  if (*pte & PTE_P)
  	panic("REMAP!");
  *pte |= PTE_P | PTE_W | PTE_U;   //turn on present writeable and user flags
  *pte &= ~PTE_PG;   //turn off pagedout flag
  *pte |= new_page;   //map page entry to the new page
  lcr3(V2P(myproc()->pgdir));   //refresh the TLB
}

void 
update_pagedout_entry(pde_t * pgdir,int va)
{
  pte_t *pte;
  pte = walkpgdir(pgdir,(char*)va, 0);   //retrieve physical page entry by va
  if(!pte)
    panic("walkpgdir error");
  *pte |= PTE_PG;   //turn on pagedout flag
  *pte &= ~PTE_P;   //turn off present flag
  *pte &= PTE_FLAGS(*pte); //clear junk physical address
  lcr3(V2P(myproc()->pgdir));   //refresh the TLB
}

int 
move_page_to_memory(struct proc* p ,char* va)
{
  #ifdef SCFIFO
    return page_to_memory_SCFIFO(p,va);
  #else

  #ifdef NFUA
    return page_to_memory_NFUA_LAPA(p,va);
  #else 

  #ifdef LAPA
    return page_to_memory_NFUA_LAPA(p,va);
  #else

  #ifdef AQ
    return page_to_memory_AQ(p,va);

  #endif
  #endif
  #endif
  #endif

  panic("unknown method\n");
  return 0;
}

int 
page_to_memory_NFUA_LAPA(struct proc* p,char* va)
{
  int i;
  for(i=0; i<MAX_PSYC_PAGES; i++)  {   //search for free space in memory
    if(p->memory_pg_arr[i].va == (char*)-1) {   
      p->memory_pg_arr[i].va = va;   //initialize page meta-data in memory
      p->memory_pg_arr[i].age = default_age;
      p->memory_pg_arr[i].next = 0;
      p->memory_pg_arr[i].prev = 0;
      p->pages_in_memory++;   //increase pages in memory counter
      return i;
    }
  }
  panic("NFUA_LAPA: failed to move page to memory\n");
  return -1;
}

int 
page_to_memory_SCFIFO(struct proc* p ,char* va)
{
  int i;
  for(i=0; i<MAX_PSYC_PAGES; i++)  {   //search for free space in memory
    if(p->memory_pg_arr[i].va == (char*)-1)      {
      p->memory_pg_arr[i].va = (char*)va;   //initialize page meta-data in memory
      p->memory_pg_arr[i].age = default_age;
      p->pages_in_memory++;   //increase pages in memory counter
      if((p->head == 0) && (p->tail == 0))        {   //if queue is empty
        p->memory_pg_arr[i].next=0;
        p->memory_pg_arr[i].prev=0;
        p->head = &p->memory_pg_arr[i];
        p->tail = &p->memory_pg_arr[i];
        return i;
      } 
    if(p->tail != 0)       {   //if queue is not empty goes to tail
      p->tail->next = &(p->memory_pg_arr[i]);
      p->tail = &(p->memory_pg_arr[i]);
      p->tail->next = 0;
      return i;
      }
    }
  }
  panic("SCFIFO: failed to move page to memory\n");
}

int 
page_to_memory_AQ(struct proc* p, char* va)
{
  int i;
  for(i=0; i<MAX_PSYC_PAGES; i++)  {   //search for free space in memory
    if(p->memory_pg_arr[i].va == (char*)-1)      {
        p->memory_pg_arr[i].va = va;   //initialize page meta-data in memory
        p->memory_pg_arr[i].age = default_age;
        p->memory_pg_arr[i].next = 0;
        p->memory_pg_arr[i].prev = 0;
        p->pages_in_memory++;   //increase pages in memory counter
      if((p->head == 0) && (p->tail == 0))        {   //if queue is empty
        p->head=&p->memory_pg_arr[i];
        p->tail=&p->memory_pg_arr[i];
        return i;
      } else {   //if queue is not empty goes to head
          p->head->prev = &p->memory_pg_arr[i];
          p->head = &p->memory_pg_arr[i];
          return i;     
        }
    }
  }        
  panic("AQ: failed to move page to memory\n");
}

struct paging_meta_data * 
replacement_algo(struct proc* p)
{
  #ifdef SCFIFO
    return SCFIFO_algo(p);
  #else

  #ifdef NFUA
    return NFUA_algo(p);
  #else

  #ifdef LAPA
    return LAPA_algo(p);
  #else

  #ifdef AQ
    return AQ_algo(p);

  #endif
  #endif
  #endif
  #endif

  panic("unknown method\n");
  return 0;
}

struct paging_meta_data * 
NFUA_algo(struct proc* p)
{
  int i;
  int min_age;
  int min_index=-1;
  for(i=0; i<MAX_PSYC_PAGES; i++)    {   //search for non-empty pages
    if(p->memory_pg_arr[i].va != (char*)-1)     {
      if(min_index == -1)     {   //if the first non-empty page
        min_age = p->memory_pg_arr[i].age;   //save page's age   
        min_index = i;   //save page's index
      } else {
          if(p->memory_pg_arr[i].age < min_age)     {   //if page's age is minimal
            min_age = p->memory_pg_arr[i].age;   //save page's age
            min_index = i;   //save page's index
          }
        }
    }  
  }
  return &(p->memory_pg_arr[min_index]);   //return page meta data with lowest counter
}

struct paging_meta_data * 
LAPA_algo(struct proc* p)
{
  int i;
  int min_index=-1;
  int min_age=-1;
  int min_ones=0;
  for(i=0; i<MAX_PSYC_PAGES; i++)    {   //search for non-empty pages
    if(p->memory_pg_arr[i].va != (char*)-1 )    {
      int tmp_ones = ones_counter(p->memory_pg_arr[i].age);
      if(min_index == -1)     {   //if the first non-empty page
        min_age = p->memory_pg_arr[i].age;   //save page's age
        min_index = i;   //save page's index
        min_ones = tmp_ones;   //save one's counter
      } else {
          if((tmp_ones < min_ones) || 
            ((tmp_ones == min_ones) && (p->memory_pg_arr[i].age < min_age)))  {
              min_age = p->memory_pg_arr[i].age;   //save page's age
              min_index = i;   //save page's index
              min_ones = tmp_ones;   //save one's counter
          }
        }
    }
  }
  return &(p->memory_pg_arr[min_index]);   //return page meta data with the lowest one counter
}

int 
ones_counter(uint age)
{
  int counter=0;
  int i;
  for(i=0; i<32; i++)  {   //for each of the age's 32 bits
    if(((1<<i)&age) != 0)   //if the bit is on
      counter++;   //increase counter
  }
  return counter;
}

struct paging_meta_data * 
SCFIFO_algo(struct proc* p)
{
  struct paging_meta_data* oldhead=p->head;
  struct paging_meta_data* tmp=p->head;
  int head_reference_bit=-1;

  do  {
    head_reference_bit = get_reference_bit(p->head->va);   //retrieve head reference bit
      if(head_reference_bit == 1)      {   //if head reference bit is on, move it to tail
        p->head = p->head->next;  
        p->head->prev = 0;
        tmp->next = 0;
        tmp->prev = p->tail;
        p->tail->next = tmp;
        p->tail = tmp;
        tmp = p->head;
      } else {   //if head reference bit is off
          tmp = p->head;
          if(p->head == p->tail) {   //if the only page in memory
            p->tail->next = 0;
            p->head->prev = 0;
          }
        }
    } while(p->head != oldhead && head_reference_bit == 1);

  remove_from_queue(p, tmp);   //remove the selected page from queue
  return tmp;
}

int 
get_reference_bit(char *va)
{
  uint accessed;
  pte_t *pte = walkpgdir(myproc()->pgdir, (void*)va, 0);
  if (!*pte)
    panic("walkpgdir error");

  accessed = (*pte) & PTE_A;   //get reference bit
  (*pte) &= ~PTE_A;   //turn reference bit off

  if(accessed == 0)   //if reference bit is off return 0 else return 1
    return 0;
  else
    return 1;
}

void 
remove_from_queue(struct proc* p, struct paging_meta_data* tmp)
{
  if(tmp->prev != 0)
    tmp->prev->next = tmp->next;
  if(tmp->next != 0)
    tmp->next->prev = tmp->prev;
  if(p->head == tmp)
    p->head = tmp->next;
  if(p->tail == tmp)
    p->tail = tmp->prev;
}

struct paging_meta_data * AQ_algo(struct proc* p)
{
  struct paging_meta_data* tmp=p->tail;

  while(get_user_bit(tmp->va) == 0){   //if user bit is off move to first place in queue
    p->tail = p->tail->prev;
    p->tail->next = 0;
    tmp->prev = 0;
    tmp->next = p->head;
    p->head->prev = tmp;
    p->head = tmp;
    tmp = p->tail;
  }

  remove_from_queue(p, tmp);   //remove the selected page from queue
  return tmp;
}

int get_user_bit(char* va)
{
  uint user;
  pte_t *pte = walkpgdir(myproc()->pgdir, (void*)va, 0);
  if (!*pte)
    panic("walkpgdir error");

  user = (*pte) & PTE_U;   //get user bit

  if(user == 0)   //if user bit is off return 0 else return 1
    return 0;
  else
    return 1;
}

