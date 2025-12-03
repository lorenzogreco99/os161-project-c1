/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>
#include <vm_tlb.h>


/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
#if OPT_DUMBVM
    /* qui puoi tenere l’implementazione originale se vuoi supportare fork con dumbvm */
    panic("as_copy: DUMBVM non usato con questo progetto\n");
    (void)old;
    (void)ret;
    return ENOSYS;
#else
    (void)old;
    *ret = NULL;
    return ENOSYS;   /* fork fallisce in modo esplicito */
#endif
}


#if OPT_DUMBVM

/*
 * Note! If OPT_DUMBVM is set, as is the case, this file should not be compiled or linked or in any way used.
 */

#error "addrspace.c compiled even though OPT_DUMBVM is set; check your config"


#else  /* !OPT_DUMBVM: NOSTRA VERSIONE DEMAND PAGING -------------------- */


struct addrspace *
as_create(void)
{
        struct addrspace *as;

        as = kmalloc(sizeof(struct addrspace));
        if (as == NULL) {
                return NULL;
        }	

        /* Page table inizialmente vuota */
        as->pt_entries  = NULL;
        as->pt_nentries = 0;
        as->pt_capacity = 0;

        /* Nessuna regione definita */
        as->nregions = 0;
        for (unsigned i = 0; i < AS_MAXREGIONS; i++) {
                as->regions[i].vbase  = 0;
                as->regions[i].npages = 0;
        }	

        return as;
}	


void
as_destroy(struct addrspace *as)
{
        if (as == NULL) {
                return;
        }

        /* TODO in futuro:
         * - per ogni pt_entry IN_MEMORY, liberare i frame fisici via coremap
         *   (coremap_freeppages ecc.)
         * Per ora ci limitiamo a liberare solo la struttura dati.
         */

        if (as->pt_entries != NULL) {
                kfree(as->pt_entries);
        }

        kfree(as);
}

void
as_activate(void)
{
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		/*
		 * Kernel thread without an address space; leave the
		 * prior address space in place.
		 */
		return;
	}

	tlb_invalidates();
}

void
as_deactivate(void)
{
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as,
                 vaddr_t vaddr, size_t sz,
                 int readable,
                 int writeable,
                 int executable)
{
        (void)readable;
        (void)writeable;
        (void)executable;
        /* Per ora ignoriamo i permessi – puoi aggiungerli nella struct region
         * se vuoi gestire anche i bit R/W/X.
         */

        /* 1. Allinea base e size a PAGE_SIZE */

        vaddr_t vbase = vaddr & PAGE_FRAME;  /* arrotonda vaddr verso il basso */

        size_t offset = vaddr - vbase;
        sz += offset;

        /* numero di pagine necessarie, arrotondato per eccesso */
        size_t npages = (sz + PAGE_SIZE - 1) / PAGE_SIZE;

        /* 2. Controlla se abbiamo spazio per un’altra regione */
        if (as->nregions >= AS_MAXREGIONS) {
                return EFAULT;
        }

        unsigned idx = as->nregions++;

        as->regions[idx].vbase  = vbase;
        as->regions[idx].npages = npages;

        return 0;
}


int
as_prepare_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

	(void)as;
	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

	(void)as;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	/*
	 * Write this.
	 */

	(void)as;

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;

	return 0;
}

#endif /* !OPT_DUMBVM */