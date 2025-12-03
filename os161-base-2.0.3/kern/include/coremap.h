/**
 * @file coremap.h
 * @brief Header file for coremap implementation
 * @details Defines the coremap_entry structure and function prototypes for coremap management.
 * @author Lisa Giacobazzi
*/

#ifndef _COREMAP_H_
#define _COREMAP_H_

#include <pt.h>


struct coremap_entry {
    unsigned long cm_allocsize : 20;
    unsigned long cm_used      : 1;
    unsigned long cm_kernel    : 1;   

    unsigned long cm_lock      : 1;
    unsigned long cm_dirty     : 1;
    unsigned long cm_in_swap   : 1;

    struct pt_entry *cm_ptentry;
};

void        coremap_bootstrap(void);
paddr_t     coremap_getppages(int npages, struct pt_entry *cm_ptentry);
void        coremap_freeppages(paddr_t addr);


#endif /* _COREMAP_H_ */
