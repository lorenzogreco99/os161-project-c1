/**
 * @file coremap.c
 * @author Lisa Giacobazzi
 * @brief important data structure that informs the kernel about which physical frames are (or not) free
 * 
 * @details The coremap tracks the state of each physical frame of RAM and is initialized during VM bootstrap. 
 * It provides page-level allocation and deallocation services for the kernel.
 */

#include <spinlock.h>
#include <vm.h>
#include <lib.h>

//Protects from concurrent accesses to coremapActive and structures
static struct spinlock freemem_lock = SPINLOCK_INITIALIZER;

static int RamFramesNumber = 0;
static unsigned long *allocSize = NULL;

/*NOTE: we can use a bitmap as well, this is just an initial implementation */
static unsigned char *freeRamFrames = NULL;

char coremapActive = 0;

/**
 * @brief checks if the coremap is active
 * @return 1 if the coremap is active, otherwise 0
 */
static int isCoremapActive () {
  int active;
  spinlock_acquire(&freemem_lock);      //enters in a critical section while guarateing mutual exclusion
  active = coremapActive;               //reads the protected value inside the critical section -> the copied value is atomic, coerent adn protected.
  spinlock_release(&freemem_lock);      //exits
  return active;
}


/**
 * @brief activates the coremap: allocates and initializes the data structures.
 */
void coremap_init(){

    RamFramesNumber = ((int)ram_getsize())/PAGE_SIZE;
    freeRamFrames = kmalloc(sizeof(unsigned char)*RamFramesNumber);
    
    if (freeRamFrames == NULL) return;  
    
    allocSize = kmalloc(sizeof(unsigned long)*RamFramesNumber);
    
    if (allocSize == NULL) {    
        freeRamFrames = NULL; 
        return;
    }

    for (int i=0; i<RamFramesNumber; i++) {    
        freeRamFrames[i] = (unsigned char)0;
        allocSize[i] = 0;  
    }
    spinlock_acquire(&freemem_lock);
    coremapActive = 1;
    spinlock_release(&freemem_lock);

};

/**
 * @brief Free or deac. the coremap.
 */
void coremap_destroy(){
    
    spinlock_acquire(&freemem_lock);
    coremapActive = 0;
    spinlock_release(&freemem_lock);
    
    kfree(freeRamFrames);
    kfree(allocSize);
}

/**
 * @brief get npages free pages.
 * 
 * @param npages number of pages needed.
 * @return paddr_t the starting physical address.
 */
static paddr_t 
getfreeppages(unsigned long npages) {
  paddr_t addr;	
  long i, first, found, np = (long)npages;

  if (!isCoremapActive()) return 0; 

  spinlock_acquire(&freemem_lock);

  for (i=0, first = found =-1; i<RamFramesNumber; i++) {
    if (freeRamFrames[i]) {
      if (i==0 || !freeRamFrames[i-1]) 
        first = i; /* set first free in an interval */   
      if (i-first+1 >= np) {
        found = first;
        break;
      }
    }
  }
	
  if (found>=0) {
    for (i=found; i<found+np; i++) {
      freeRamFrames[i] = (unsigned char)0;
    }
    allocSize[found] = np;
    addr = (paddr_t) found*PAGE_SIZE;
  }
  else {
    addr = 0;
  }

  spinlock_release(&freemem_lock);

  return addr;
}

/**
 * @brief get npages free pages. if there are not enogugh free pages it will 
 * steal memory from the ram.
 * 
 * please note that is is only called by the kernel, since the user can only allocate
 * one page at a time.
 * 
 * @param npages number of pages needed.
 * @return paddr_t the starting physical address.
 */
static paddr_t
getppages(unsigned long npages)
{
  paddr_t addr;

  /* try freed pages first */
  addr = getfreeppages(npages);
  if (addr == 0) {
    /* call stealmem */
    spinlock_acquire(&stealmem_lock);   //global spinlock defined by OS161 to protect ram_stealmem
    addr = ram_stealmem(npages);
    spinlock_release(&stealmem_lock);
  }
  if (addr!=0 && isCoremapActive()) {
    spinlock_acquire(&freemem_lock);
    allocSize[addr/PAGE_SIZE] = npages;
    spinlock_release(&freemem_lock);
  } 

  return addr;
}


/**
 * @brief allocate kernel space.
 * 
 * @param npages 
 * @return vaddr_t 
 */
vaddr_t
alloc_kpages(unsigned npages)
{
	paddr_t pa;

	dumbvm_can_sleep();
	pa = getppages(npages);
	if (pa==0) {
		return 0;
	}
	return PADDR_TO_KVADDR(pa);
}

/**
 * @brief deallocate kernel space.
 * 
 * @param addr 
 */
void 
free_kpages(vaddr_t addr){
  if (isCoremapActive()) {
    paddr_t paddr = addr - MIPS_KSEG0;
    long first = paddr/PAGE_SIZE;	
    KASSERT(allocSize!=NULL);
    KASSERT(RamFramesNumber>first);
    freeppages(paddr, allocSize[first]);	
  }
}

/**
 * @brief allocate a page for the user. 
 * It is different from the alloc_kpage as it allocate one frame at a time and
 * has to manage the victim selection.
 * 
 * @return vaddr_t the virtual address of the allocated frame
 */
paddr_t 
alloc_upage(){
    //TODO manage the case in which there are no free frames with swapping.
    dumbvm_cansleep();
    return getppages(1);
}

/**
 * @brief deallocate the given page for the user.
 * 
 * @param addr 
 */
void free_upage(vaddr_t addr){
    getfreeppages(1);
};