#ifndef _LINUX_HPT_AREA_H
#define _LINUX_HPT_AREA_H

/**
 * Initialize HPT Area and bitmap.
 * 1. Allocate bitmap
 * 2. Allocate HPT Area.
 * 3. Initialize data structures related to page table allocation.
 * 4. Copy current page tables into it.
 */
void init_hpt_area_and_bitmap(void);

#endif
