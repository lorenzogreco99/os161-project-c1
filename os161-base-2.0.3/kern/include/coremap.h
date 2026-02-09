#ifndef _COREMAP_H_
#define _COREMAP_H_

#include <pt.h>
#include "opt-DEMANDVM.h"

#if OPT_DEMANDVM

struct cm_entry
{
    unsigned char       cm_free : 1;
    unsigned long       cm_size_alloc : 20;      
    unsigned char       cm_lock : 1;
    struct pt_entry     *cm_ptentry;            /*  page table entry of the page living 
                                                    in this frame, NULL if kernel page  */
};

void        coremap_bootstrap(void);
paddr_t     coremap_getppages(int npages, struct pt_entry *ptentry);
void        coremap_freeppages(paddr_t addr);

#endif /* OPT_DEMANDVM */

#endif /* _COREMAP_H_ */
