/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2009 Chen Liqin <liqin.chen@sunplusct.com>
 * Copyright (C) 2012 Regents of the University of California
 */

#ifndef _ASM_RISCV_PGALLOC_H
#define _ASM_RISCV_PGALLOC_H

#include <linux/mm.h>
#include <asm/tlb.h>

#ifdef CONFIG_MMU
#define __HAVE_ARCH_PUD_ALLOC_ONE
#define __HAVE_ARCH_PUD_FREE

#ifdef CONFIG_HPT_AREA
#define __HAVE_ARCH_PGD_FREE
#define __HAVE_ARCH_PMD_ALLOC_ONE
#define __HAVE_ARCH_PMD_FREE
#define __HAVE_ARCH_PTE_ALLOC_ONE_KERNEL
#define __HAVE_ARCH_PTE_FREE_KERNEL
#define __HAVE_ARCH_PTE_ALLOC_ONE
#define __HAVE_ARCH_PTE_FREE
#endif /* CONFIG_HPT_AREA */

#include <asm-generic/pgalloc.h>

#ifdef CONFIG_HPT_AREA
#include <linux/hpt_area.h>
#include <asm/sbi-sm.h>
#endif /* CONFIG_HPT_AREA */


static inline void pmd_populate_kernel(struct mm_struct *mm,
	pmd_t *pmd, pte_t *pte)
{
	unsigned long pfn = virt_to_pfn(pte);

	set_pmd(pmd, __pmd((pfn << _PAGE_PFN_SHIFT) | _PAGE_TABLE));
}

static inline void pmd_populate(struct mm_struct *mm,
	pmd_t *pmd, pgtable_t pte)
{
	unsigned long pfn = virt_to_pfn(page_address(pte));

	set_pmd(pmd, __pmd((pfn << _PAGE_PFN_SHIFT) | _PAGE_TABLE));
}

#ifndef __PAGETABLE_PMD_FOLDED
static inline void pud_populate(struct mm_struct *mm, pud_t *pud, pmd_t *pmd)
{
	unsigned long pfn = virt_to_pfn(pmd);

	set_pud(pud, __pud((pfn << _PAGE_PFN_SHIFT) | _PAGE_TABLE));
}

static inline void p4d_populate(struct mm_struct *mm, p4d_t *p4d, pud_t *pud)
{
	if (pgtable_l4_enabled) {
		unsigned long pfn = virt_to_pfn(pud);

		set_p4d(p4d, __p4d((pfn << _PAGE_PFN_SHIFT) | _PAGE_TABLE));
	}
}

static inline void p4d_populate_safe(struct mm_struct *mm, p4d_t *p4d,
				     pud_t *pud)
{
	if (pgtable_l4_enabled) {
		unsigned long pfn = virt_to_pfn(pud);

		set_p4d_safe(p4d,
			     __p4d((pfn << _PAGE_PFN_SHIFT) | _PAGE_TABLE));
	}
}

static inline void pgd_populate(struct mm_struct *mm, pgd_t *pgd, p4d_t *p4d)
{
	if (pgtable_l5_enabled) {
		unsigned long pfn = virt_to_pfn(p4d);

		set_pgd(pgd, __pgd((pfn << _PAGE_PFN_SHIFT) | _PAGE_TABLE));
	}
}

static inline void pgd_populate_safe(struct mm_struct *mm, pgd_t *pgd,
				     p4d_t *p4d)
{
	if (pgtable_l5_enabled) {
		unsigned long pfn = virt_to_pfn(p4d);

		set_pgd_safe(pgd,
			     __pgd((pfn << _PAGE_PFN_SHIFT) | _PAGE_TABLE));
	}
}

#define pud_alloc_one pud_alloc_one
static inline pud_t *pud_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	if (pgtable_l4_enabled)
		return __pud_alloc_one(mm, addr);

	return NULL;
}

#define pud_free pud_free
static inline void pud_free(struct mm_struct *mm, pud_t *pud)
{
	if (pgtable_l4_enabled)
		__pud_free(mm, pud);
}

#define __pud_free_tlb(tlb, pud, addr)  pud_free((tlb)->mm, pud)

#define p4d_alloc_one p4d_alloc_one
static inline p4d_t *p4d_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	if (pgtable_l5_enabled) {
		gfp_t gfp = GFP_PGTABLE_USER;

		if (mm == &init_mm)
			gfp = GFP_PGTABLE_KERNEL;
		return (p4d_t *)get_zeroed_page(gfp);
	}

	return NULL;
}

static inline void __p4d_free(struct mm_struct *mm, p4d_t *p4d)
{
	BUG_ON((unsigned long)p4d & (PAGE_SIZE-1));
	free_page((unsigned long)p4d);
}

#define p4d_free p4d_free
static inline void p4d_free(struct mm_struct *mm, p4d_t *p4d)
{
	if (pgtable_l5_enabled)
		__p4d_free(mm, p4d);
}

#define __p4d_free_tlb(tlb, p4d, addr)  p4d_free((tlb)->mm, p4d)
#endif /* __PAGETABLE_PMD_FOLDED */

#ifdef CONFIG_HPT_AREA
static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *pgd;

	pgd = (pgd_t *)alloc_hpt_pgd_page();
	if (unlikely(pgd == NULL))
		return NULL;

	long error, value;

	// First memset(0)
	sbi_sm_ecall(&error, &value, SBI_EXT_SM_SET_PTE,
		     SBI_EXT_SM_SET_PTE_CLEAR, __pa(pgd), 0,
		     USER_PTRS_PER_PGD * sizeof(pgd_t), 0, 0);
	if (unlikely(error || value)) {
		panic("pgd_alloc: failed to clear page(error: %ld, value: %ld)\n",
		      error, value);
		while (1) {
		}
	}

	// Then memcpy
	sbi_sm_ecall(&error, &value, SBI_EXT_SM_SET_PTE,
		     SBI_EXT_SM_SET_PTE_MEMCPY, __pa(pgd + USER_PTRS_PER_PGD),
		     __pa(init_mm.pgd + USER_PTRS_PER_PGD),
		     (PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof(pgd_t), 0, 0);
	if (unlikely(error || value)) {
		panic("pgd_alloc: failed to memcpy(error: %ld, value: %ld)\n",
		      error, value);
		while (1) {
		}
	}

	return pgd;
}
static inline void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	free_hpt_pgd_page((char *)pgd);
}
#else /* CONFIG_HPT_AREA */
static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *pgd;

	pgd = (pgd_t *)__get_free_page(GFP_KERNEL);
	if (likely(pgd != NULL)) {
		memset(pgd, 0, USER_PTRS_PER_PGD * sizeof(pgd_t));
		/* Copy kernel mappings */
		memcpy(pgd + USER_PTRS_PER_PGD,
			init_mm.pgd + USER_PTRS_PER_PGD,
			(PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof(pgd_t));
	}
	return pgd;
}
#endif /* CONFIG_HPT_AREA */

#ifdef CONFIG_HPT_AREA
// Helper Functions for HPT
static inline pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	struct page *page;
	gfp_t gfp = GFP_PGTABLE_USER;

	if (mm == &init_mm)
		gfp = GFP_PGTABLE_KERNEL;

	char *pmd_addr = alloc_hpt_pmd_page();
	if (pmd_addr == NULL)
		return NULL;

	page = virt_to_page((void *)pmd_addr);
	if (!page)
		return NULL;
	if (!pgtable_pmd_page_ctor(page)) {
		free_hpt_pmd_page(page_address(page));
		return NULL;
	}
	return (pmd_t *)page_address(page);
}
static inline void pmd_free(struct mm_struct *mm, pmd_t *pmd)
{
	BUG_ON((unsigned long)pmd & (PAGE_SIZE - 1));
	struct page *page = virt_to_page(pmd);
	pgtable_pmd_page_dtor(page);
	*(unsigned long *)&page->ptl = 0;
	free_hpt_pmd_page((char *)pmd);
}
static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm)
{
	return (pte_t *)alloc_hpt_pte_page();
}
static inline void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
	free_hpt_pte_page((char *)pte);
}
static inline pgtable_t pte_alloc_one(struct mm_struct *mm)
{
	struct page *pte;
	char *pte_addr = alloc_hpt_pte_page();
	if (pte_addr == NULL)
		return NULL;

	pte = virt_to_page((void *)(pte_addr));
	if (!pte)
		return NULL;
	if (!pgtable_pte_page_ctor(pte)) {
		free_hpt_pte_page(page_address(pte));
		return NULL;
	}

	return pte;
}
static inline void pte_free(struct mm_struct *mm, struct page *pte_page)
{
	pgtable_pte_page_dtor(pte_page);
	*(unsigned long *)&pte_page->ptl = 0;
	free_hpt_pte_page((page_address(pte_page)));
}
#endif /* CONFIG_HPT_AREA */


#ifndef __PAGETABLE_PMD_FOLDED

#define __pmd_free_tlb(tlb, pmd, addr)  pmd_free((tlb)->mm, pmd)

#endif /* __PAGETABLE_PMD_FOLDED */

#define __pte_free_tlb(tlb, pte, buf)   \
do {                                    \
	pgtable_pte_page_dtor(pte);     \
	tlb_remove_page((tlb), pte);    \
} while (0)
#endif /* CONFIG_MMU */

#endif /* _ASM_RISCV_PGALLOC_H */
