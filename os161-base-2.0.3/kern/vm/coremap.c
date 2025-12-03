/**
 * @file coremap.c
 * @author Lisa Giacobazzi
 * @brief important data structure that informs the kernel about which physical frames are (or not) free
 * 
 * @details The coremap tracks the state of each physical frame of RAM and is initialized during VM bootstrap. 
 * It provides page-level allocation and deallocation services for the kernel.
 */
#include <types.h>
#include <vm.h>
#include <lib.h>
#include <coremap.h>
#include <mainbus.h>
#include <vm_tlb.h>
#include <synch.h>
#include <addrspace.h>
//#include "opt-swap.h"
//#include "opt-noswap_rdonly.h"

vaddr_t firstfree; /* first free virtual address; set by start.S */

struct spinlock cm_spinlock = SPINLOCK_INITIALIZER;

static int        coremap_find_freeframes(int npages);
#if OPT_SWAP
static int        coremap_get_victim();
static int        coremap_swapout(int npages);
static int        victim_index = 0;
#endif
static int        nRamFrames = 0; /* number of ram frames */
static struct     coremap_entry *coremap;

/**
 * @brief Initialization of the coremap, this function is called 
 * in the very initial phase of the system bootsrap. It replace ram_bootstrap
 * as in the version with demand paging ram.c is totally bypassed.
 *  
 */
void coremap_bootstrap(){
  paddr_t firstpaddr;   /* one past end of first free physical page */
  paddr_t lastpaddr;    /* one past end of last free physical page  */
  size_t coremap_size;  /* number of bytes of coremap               */
  int coremap_pages;    /* number of coremap pages                  */
  int kernel_pages;     /* number of kernel pages                   */
  int i;


  /* Get size of RAM. */
  lastpaddr = mainbus_ramsize();

  /* Limit RAM to 512MB for simplicity. */
  if (lastpaddr > 512 * 1024 * 1024)
  {
    lastpaddr = 512 * 1024 * 1024;
  }

  /* Convert firstfree to physical address. */
  firstpaddr = KVADDR_TO_PADDR(firstfree);

  KASSERT(lastpaddr % PAGE_SIZE == 0);
  KASSERT(firstpaddr % PAGE_SIZE == 0);

  nRamFrames = lastpaddr / PAGE_SIZE;


  /* Alloc in the first free address. */
  coremap = (struct coremap_entry *)firstfree;
  
  /* Calcola dimensione coremap e #pagine kernel */
  coremap_size = sizeof(struct coremap_entry) * nRamFrames;
  coremap_pages = DIVROUNDUP(coremap_size, PAGE_SIZE);
  kernel_pages = firstpaddr / PAGE_SIZE;

  /*  Initialize the entries of the coremap. */
  for (i = 0; i < nRamFrames; i++)
  {
    coremap[i].cm_allocsize = 0;
    coremap[i].cm_used = 0;
    coremap[i].cm_kernel = 0;

    coremap[i].cm_lock = 0;
    coremap[i].cm_ptentry = NULL;
    coremap[i].cm_dirty = false;
    coremap[i].cm_in_swap = false;
  }

  /* Mark kernel and coremap pages as used. */
  for (i = 0; i < kernel_pages + coremap_pages; i++)
  {
    coremap[i].cm_used = 1;
    coremap[i].cm_kernel = 1;
    coremap[i].cm_allocsize = 1;
    // Per sicurezza, questi non li swappiamo comunque
    coremap[i].cm_dirty     = false;
    coremap[i].cm_in_swap   = false;
  }

}

/**
 * @brief Find npages free frames in the coremap.
 * @param npages 
 * @return int: index of the first free frame, -1 if not found.
 */
static int
coremap_find_freeframes(int npages)
{
  int end;
  int beginning;

  end = 0;
  beginning = -1;
  while (end < nRamFrames)
  {
    if (coremap[end].cm_used == 1)
    {
      beginning = -1;
      end += coremap[end].cm_allocsize;
    }
    else // frame is free
    {
      if (beginning == -1)
      {
        beginning = end;
      }
      end++;

      if (end - beginning == npages)
        break;
    }
  }

  if (end - beginning != npages)
    beginning = -1;

  return beginning;
}

#if OPT_SWAP
/**
 * @brief Find a swappable victim.
 * 
 * @return index of the swappable page, -1 if not found.
 */
static int
coremap_get_victim()
{
  int i;

  for(i=0; i<nRamFrames; i++)
  {
    victim_index = (victim_index + 1) % nRamFrames;

    /* Swap out only user pages */
    if(coremap[victim_index].cm_ptentry != NULL && !coremap[victim_index].cm_lock)
    {
      KASSERT(coremap[victim_index].cm_used == 1);
      KASSERT(coremap[victim_index].cm_allocsize == 1);

      return victim_index;
    }
  }

  return -1;
}

/**
 * @brief swap out pages from memory.
 * 
 * @param npages
 * @return index of the frame swapped out.
 */
static int
coremap_swapout(int npages)
{
  int victim_index;
  int swap_index;

  if(npages > 1)
  {
    panic("Cannot swap out multiple pages");
  }

  victim_index = coremap_get_victim();
  if(victim_index == -1)
  {
    panic("Cannot find swappable victim");
  }

#if OPT_NOSWAP_RDONLY
  if(coremap[victim_index].cm_ptentry->pt_status == IN_MEMORY_RDONLY){
    pt_set_entry(coremap[victim_index].cm_ptentry,0,0,NOT_LOADED);
    tlb_remove_by_paddr(victim_index * PAGE_SIZE);
    return victim_index;
  }
#endif

  /* lock the frame while swapping out */

  coremap[victim_index].cm_lock = 1;
  spinlock_release(&cm_spinlock);
  swap_index = swap_out(victim_index * PAGE_SIZE);
  spinlock_acquire(&cm_spinlock);
  coremap[victim_index].cm_lock = 0;

  /* appena scritto su swap: la copia in RAM non è più “sporco da scrivere” */
  coremap[victim_index].cm_dirty   = false;
  coremap[victim_index].cm_in_swap = true;

  /* update page table */
  pt_set_entry(coremap[victim_index].cm_ptentry, 0, swap_index, IN_SWAP);
  tlb_remove_by_paddr(victim_index * PAGE_SIZE);

  return victim_indexz;
}
#endif

/**
 * @brief allocate npages pages and return the physical address of the first one.
 * @param npages 
 * @param ptentry 
 * @return paddr_t: physical address of the first allocated page, 0 if failed.
 */
paddr_t
coremap_getppages(int npages, struct pt_entry *ptentry)
{
  int i;
  int end = 0;
  int beginning = -1;

  spinlock_acquire(&cm_spinlock);
  beginning = coremap_find_freeframes(npages);
  if (beginning == -1)
  {
#if OPT_SWAP
    beginning = coremap_swapout(npages);
    if (beginning == -1)
    {
      spinlock_release(&cm_spinlock);
      return 0;
    }
#else
    spinlock_release(&cm_spinlock);
    return 0;
#endif
  }


  bzero((void *)PADDR_TO_KVADDR(beginning * PAGE_SIZE), PAGE_SIZE * npages);

  coremap[beginning].cm_allocsize = npages;
  for (i = 0; i < npages; i++) {
    coremap[beginning + i].cm_used    = 1;
    coremap[beginning + i].cm_ptentry = ptentry;
    coremap[beginning + i].cm_kernel = (ptentry == NULL);
    coremap[beginning + i].cm_lock    = 0;      // per sicurezza
    coremap[beginning + i].cm_dirty   = false;  // pagina appena allocata, non sporca
    coremap[beginning + i].cm_in_swap = false;  // è in RAM, nessuna copia in swap
  }
  spinlock_release(&cm_spinlock);
  return beginning * PAGE_SIZE;
}

/**
 * @brief free the pages starting from addr.
 * @param addr: physical address of the first page to free.
 */
void coremap_freeppages(paddr_t addr)
{
  long i;
  long first;
  long allocSize;

  KASSERT(addr % PAGE_SIZE == 0);

  first = addr / PAGE_SIZE;
  allocSize = coremap[first].cm_allocsize;
  coremap[first].cm_allocsize = 0;

  KASSERT(nRamFrames > first);
  
  spinlock_acquire(&cm_spinlock);
  
  KASSERT(allocSize > 0);

  for (i = 0; i < allocSize; i++) {
      KASSERT(coremap[first + i].cm_used == 1);
      coremap[first + i].cm_used = 0;
      coremap[first + i].cm_ptentry = NULL;
      coremap[first + i].cm_lock = 0;
      coremap[first + i].cm_dirty   = false;   // frame libero, niente sporco
      coremap[first + i].cm_in_swap = false;   // lo stato swap è della pagina, non del frame
  }
  spinlock_release(&cm_spinlock);
}
