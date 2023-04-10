#ifndef _LINUX_HPT_AREA_H
#define _LINUX_HPT_AREA_H

// TODO: check
#define PGD_PAGE_ORDER 5
#define PMD_PAGE_ORDER 7

/**
 * Allocate HPT Area and bitmap.
 * 1. Allocate bitmap
 * 2. Allocate HPT Area.
 */
void alloc_hpt_area_and_bitmap(void);

/**
 * Initialize HPT Area and bitmap.
 * 1. Initialize data structures related to page table allocation.
 * 2. Copy current page tables into it.
 */
void init_hpt_area_and_bitmap(void);

/**
 * Allocate a PGD page from HPT Area
 *
 * @return the start address of the page, NULL if not available
 */
char *alloc_hpt_pgd_page(void);

/**
 * Allocate a PMD page from HPT Area
 *
 * @return the start address of the page, NULL if not available
 */
char *alloc_hpt_pmd_page(void);

/**
 * Allocate a PTE page from HPT Area
 *
 * @return the start address of the page, NULL if not available
 */
char *alloc_hpt_pte_page(void);

/**
 * Free a PGD page back to HPT Area
 *
 * @param page the start address of the page
 * @return 0 on success, negative error code on failure
 */
int free_hpt_pgd_page(char *page);

/**
 * Free a PGD page back to HPT Area
 *
 * @param page the start address of the page
 * @return 0 on success, negative error code on failure
 */
int free_hpt_pmd_page(char *page);

/**
 * Free a PGD page back to HPT Area
 *
 * @param page the start address of the page
 * @return 0 on success, negative error code on failure
 */
int free_hpt_pte_page(char *page);

/**
 * Check if a page is inside the PTE section of HPT Area
 * 
 * @param page the start address of the page
 * @return 0 on true, -1 on false
 */
int check_pt_pte_page(char *page);

#endif
