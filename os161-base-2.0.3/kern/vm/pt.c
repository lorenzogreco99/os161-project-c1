/**
 * @file pt.c
 * @author Lisa Giacobazzi
 * @brief Page table management functions.
 * @date 2025-12-01
 */

#include <types.h>
#include <lib.h>
#include <addrspace.h>
#include <coremap.h>
#include <pt.h>

#define PT_INIT_CAPACITY 64

static
int pt_grow(struct addrspace *as)
{
    unsigned newcap = (as->pt_capacity == 0) ? PT_INIT_CAPACITY : as->pt_capacity * 2;
    struct pt_entry *newpt = kmalloc(newcap * sizeof(struct pt_entry));
    if (newpt == NULL) {
        return ENOMEM;
    }

    // copia le entry esistenti
    for (unsigned i = 0; i < as->pt_nentries; i++) {
        newpt[i] = as->pt_entries[i];
    }

    // inizializza il resto
    for (unsigned i = as->pt_nentries; i < newcap; i++) {
        newpt[i].pt_status = NOT_LOADED;
        newpt[i].pt_vaddr = 0;
        newpt[i].pt_paddr = 0;
        newpt[i].pt_swap_index = -1;
    }

    kfree(as->pt_entries);
    as->pt_entries = newpt;
    as->pt_capacity = newcap;
    return 0;
}

struct pt_entry *
pt_lookup(struct addrspace *as, vaddr_t vaddr)
{
    if (as == NULL || as->pt_entries == NULL) {
        return NULL;
    }

    vaddr &= PAGE_FRAME;

    for (unsigned i = 0; i < as->pt_nentries; i++) {
        if (as->pt_entries[i].pt_vaddr == vaddr) {
            return &as->pt_entries[i];
        }
    }
    return NULL;
}

struct pt_entry *
pt_get_or_create(struct addrspace *as, vaddr_t vaddr)
{
    int result;

    vaddr &= PAGE_FRAME;

    // prova a trovarla
    struct pt_entry *pte = pt_lookup(as, vaddr);
    if (pte != NULL) {
        return pte;
    }

    // serve una nuova entry
    if (as->pt_nentries == as->pt_capacity) {
        result = pt_grow(as);
        if (result) {
            return NULL;
        }
    }

    pte = &as->pt_entries[as->pt_nentries++];
    pte->pt_vaddr = vaddr;
    pte->pt_paddr = 0;
    pte->pt_status = NOT_LOADED;
    pte->pt_swap_index = -1;
    return pte;
}

void
pt_set_entry(struct pt_entry *pte, paddr_t paddr,
             int swap_index, enum pt_status status)
{
    pte->pt_paddr = paddr;
    pte->pt_swap_index = swap_index;
    pte->pt_status = status;
}