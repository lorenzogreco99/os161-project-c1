/**
 * @file addrspace.h
 * @author Lisa Giacobazzi
 * @brief Address space structure and function prototypes.
 * @details This file defines the addrspace structure used to manage
 *          virtual memory address spaces in the OS/161 kernel.
 * @date 2025-12-01
 */

#ifndef _ADDRSPACE_H_
#define _ADDRSPACE_H_

#include <vm.h>
#include "opt-dumbvm.h"
#include <machine/vm.h>

#ifndef PADDR_TO_KVADDR
#define PADDR_TO_KVADDR(paddr) ((vaddr_t)((paddr) + MIPS_KSEG0))
#endif

#ifndef KVADDR_TO_PADDR
#define KVADDR_TO_PADDR(vaddr) ((paddr_t)((vaddr) - MIPS_KSEG0))
#endif

struct vnode;

struct pt_entry;  // forward declaration, definita in pt.h

struct addrspace {
#if OPT_DUMBVM
        vaddr_t as_vbase1;
        paddr_t as_pbase1;
        size_t  as_npages1;
        vaddr_t as_vbase2;
        paddr_t as_pbase2;
        size_t  as_npages2;
        paddr_t as_stackpbase;
#else
        struct pt_entry *pt_entries;
        unsigned pt_nentries;
        unsigned pt_capacity;
        /* in futuro: lista delle regioni, permessi, ecc. */
#endif
};

/* Prototipi – solo dichiarazioni qui */

struct addrspace *as_create(void);
int               as_copy(struct addrspace *src, struct addrspace **ret);
void              as_activate(void);
void              as_deactivate(void);
void              as_destroy(struct addrspace *);

int               as_define_region(struct addrspace *as,
                                   vaddr_t vaddr, size_t sz,
                                   int readable,
                                   int writeable,
                                   int executable);
int               as_prepare_load(struct addrspace *as);
int               as_complete_load(struct addrspace *as);
int               as_define_stack(struct addrspace *as, vaddr_t *initstackptr);

/* Dichiarazione di load_elf: UNA sola volta qui va benissimo */
int load_elf(struct vnode *v, vaddr_t *entrypoint);

#endif /* _ADDRSPACE_H_ */