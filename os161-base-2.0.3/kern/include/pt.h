#ifndef _PT_H_
#define _PT_H_

#include <types.h>
#include <vm.h>        // per vaddr_t, paddr_t

/* Forward declaration, ci basta sapere che esiste */
struct addrspace;      

enum pt_status {
    NOT_LOADED,       // mai caricata (da eseguibile o zero-fill)
    IN_MEMORY,        // in RAM, scrivibile
    IN_MEMORY_RDONLY, // in RAM, read-only (code / read-only data)
    IN_SWAP           // non in RAM, ma presente nello swapfile
};

struct pt_entry {
    vaddr_t  pt_vaddr;
    paddr_t  pt_paddr;     // frame fisico quando IN_MEMORY/IN_MEMORY_RDONLY
    int      pt_status;
    int      pt_swap_index; // indice nel file di swap (quando IN_SWAP)
};

struct pt_entry *pt_lookup(struct addrspace *as, vaddr_t vaddr);
struct pt_entry *pt_get_or_create(struct addrspace *as, vaddr_t vaddr);

void pt_set_entry(struct pt_entry *pte, paddr_t paddr,
                  int swap_index, enum pt_status status);

#endif /* _PT_H_ */