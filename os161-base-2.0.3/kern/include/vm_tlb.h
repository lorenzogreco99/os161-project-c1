#ifndef _VM_TLB_H_
#define _VM_TLB_H_

#include <types.h>

/**
 * @brief Invalidates all TLB entries.
 */
void tlb_invalidates(void);

/**
 * @brief Inserts a new entry into the TLB.
 * @details Inserts a new mapping into the TLB with the given virtual address,
 * physical address, and read-only flag.
 *  @param vaddr: virtual address
 *  @param paddr: physical address
 *  @param ro: read only flag
*/
void tlb_inserts(vaddr_t vaddr, paddr_t paddr, bool ro);

/**
 * @brief Removes a TLB entry corresponding to the given virtual address.
 * @param vaddr 
 */
//void tlb_remove(vaddr_t vaddr);

/**
 * @brief Removes a TLB entry correspondign to the given phisical address
 * 
 * @param paddr 
 */
void tlb_remove_by_paddr(paddr_t paddr);

void tlb_remove_entry(vaddr_t vaddr);

#endif /* _VM_TLB_H_ */