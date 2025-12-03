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
#include <swapfile.h>
//#include "opt-swap.h"
//#include "opt-noswap_rdonly.h"

vaddr_t firstfree; /* first free virtual address; set by start.S */

struct spinlock cm_spinlock = SPINLOCK_INITIALIZER;

static int        coremap_find_freeframes(int npages);

static int        nRamFrames = 0; /* number of ram frames */
static struct     coremap_entry *coremap;
static unsigned   cm_rr_hand = 0;   /* indice corrente del RR nella coremap */

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
  
  int beginning = -1;

  spinlock_acquire(&cm_spinlock);
  beginning = coremap_find_freeframes(npages);

  if (beginning == -1) {
  #if OPT_SWAP
      if (npages != 1) {
          panic("coremap_getppages: cannot evict multi-page allocation\n");
      }

      paddr_t victim_paddr = evict_page();
      /* Ricava l’indice del frame dalla paddr */
      beginning = (int)(victim_paddr / PAGE_SIZE);
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

paddr_t
evict_page(void)
{
    unsigned scanned = 0;

    while (scanned < (unsigned)nRamFrames) {
        unsigned i = cm_rr_hand;
        cm_rr_hand = (cm_rr_hand + 1) % (unsigned)nRamFrames;
        scanned++;

        struct coremap_entry *cme = &coremap[i];

        /* Skip: frame libero, kernel, lockato */
        if (!cme->cm_used)  continue;
        if (cme->cm_kernel) continue;
        if (cme->cm_lock)   continue;

        /* Vittima trovata */
        struct pt_entry *pte = cme->cm_ptentry;
        KASSERT(pte != NULL);

        paddr_t victim_paddr = (paddr_t)(i * PAGE_SIZE);

        /* Se la pagina è dirty o non è mai stata swappata, scrivila nello swapfile */
        if (cme->cm_dirty || !cme->cm_in_swap) {
            unsigned int idx = swap_out(victim_paddr);
            pte->pt_swap_index = (int)idx;
            cme->cm_in_swap    = true;
            cme->cm_dirty      = false;
        }

        /* Aggiorna la PTE: ora sta solo nello swap */
        pte->pt_status = IN_SWAP;
        pte->pt_paddr  = 0;

        /* Invalida eventuale entry TLB per questo frame */
        tlb_remove_by_paddr(victim_paddr);

        /* Libera il frame nella coremap */
        cme->cm_used      = 0;
        cme->cm_ptentry   = NULL;
        cme->cm_allocsize = 0;
        /* cm_kernel resta 0, cm_in_swap resta true */

        return victim_paddr;
    }

    panic("evict_page: no evictable frame found\n");
}

//APIs
paddr_t
coremap_get_frame(struct pt_entry *ptentry)
{
    /* Una sola pagina utente per volta */
    return coremap_getppages(1, ptentry);
}

void
coremap_free_frame(paddr_t addr)
{
    coremap_freeppages(addr);
}