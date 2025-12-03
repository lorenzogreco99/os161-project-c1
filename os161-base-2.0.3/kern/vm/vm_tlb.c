/**
 * @file vm_tlb.c
 * @author Lisa Giacobazzi
 * @brief Manages the TLB which is used for virtual memory to phisical mem translation
 * @details Implements functions to invalidate the TLB and insert new entries. The TLB contains coples virtual to phisics.
 * @date 2025-11-27
 */

#include <vm_tlb.h>
#include <mips/tlb.h>
#include <spl.h>
#include <vm.h>
#include <lib.h>

int victim = 0;

void tlb_invalidates(void)
{
    int spl, i;

    /* Disable interrupts on this CPU while frobbing the TLB. */
    spl = splhigh();

    for (i = 0; i < NUM_TLB; i++)
    {
        tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
    }

    splx(spl);
}

void tlb_inserts(vaddr_t vaddr, paddr_t paddr, bool ro)
{
    int spl;
    uint32_t ehi, elo;

    /* Make sure it's page-aligned */
    KASSERT((paddr & PAGE_FRAME) == paddr);

    /* Disable interrupts on this CPU while frobbing the TLB. */
    spl = splhigh();

    ehi = vaddr;
    elo = paddr | TLBLO_VALID;
    if (!ro)
    {
        elo = elo | TLBLO_DIRTY;
    }
    tlb_write(ehi, elo, victim);
    victim = (victim + 1) % NUM_TLB;

    splx(spl);
}

void
tlb_remove_by_paddr(paddr_t paddr)
{
    (void)paddr;
    /*
     * Versione semplice: per adesso non cerchiamo la singola entry,
     * ma invalidiamo tutte le entry del TLB.
     * È meno efficiente, ma molto più semplice e va benissimo.
     */
    tlb_invalidates();
}

void
tlb_remove_entry(vaddr_t vaddr)
{
    int spl = splhigh();
    uint32_t ehi, elo;

    for (int i = 0; i < NUM_TLB; i++) {
        tlb_read(&ehi, &elo, i);
        if ((elo & TLBLO_VALID) && (ehi & PAGE_FRAME) == (vaddr & PAGE_FRAME)) {
            tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
        }
    }

    splx(spl);
}