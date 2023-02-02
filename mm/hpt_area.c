#include <linux/hpt_area.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <asm/sbi.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>

spinlock_t hpt_lock;

struct hpt_page_list {
	struct hpt_page_list *next_page;
};

// TODO: more levels
uintptr_t hpt_pgd_page_start = 0;
struct hpt_page_list *hpt_pgd_page_list = NULL;
struct hpt_page_list *hpt_pgd_free_list = NULL;
uintptr_t hpt_pmd_page_start = 0;
struct hpt_page_list *hpt_pmd_page_list = NULL;
struct hpt_page_list *hpt_pmd_free_list = NULL;
size_t pte_pages = 0;
uintptr_t hpt_pte_page_start = 0;
struct hpt_page_list *hpt_pte_page_list = NULL;
struct hpt_page_list *hpt_pte_free_list = NULL;

static void deep_copy_pt(pte_t *src_pt, pte_t *dest_pt, int level)
{
	unsigned long i = 0;
	int tmpLevel = level;
	for (i = 0; i < PTRS_PER_PGD; ++i) {
		unsigned long pte = src_pt[i].pte;
		if (pte & _PAGE_PRESENT) {
			if ((pte & _PAGE_READ) || (pte & _PAGE_EXEC)) {
				//Find a leaf PTE
				dest_pt[i] = __pte(pte);
			} else {
				pte_t *new_dest_pt;
				pte_t *new_src_pt;
				if (tmpLevel == 0) {
					new_dest_pt =
						(pte_t *)alloc_hpt_pmd_page();
				} else {
					new_dest_pt =
						(pte_t *)alloc_hpt_pte_page();
				}
				new_src_pt = (pte_t *)pfn_to_virt(
					pte >> _PAGE_PFN_SHIFT);
				deep_copy_pt(new_src_pt, new_dest_pt,
					     level + 1);
				src_pt[i] = pfn_pte(PFN_DOWN(__pa(new_dest_pt)),
						    __pgprot(pte & 0x3FF));
				dest_pt[i] =
					pfn_pte(PFN_DOWN(__pa(new_dest_pt)),
						__pgprot(pte & 0x3FF));
			}
		}
	}
}

pgd_t *new_swapper_pg_dir;
void transfer_init_pt(void)
{
	pte_t *new_swapper_pt;
	unsigned long i = 0;
	pr_notice("Transfer init table\n");
#ifndef __PAGETABLE_PMD_FOLDED
	BUG_ON((PTRS_PER_PGD != PTRS_PER_PTE) ||
	       (PTRS_PER_PTE != PTRS_PER_PMD));
#else
	BUG_ON(PTRS_PER_PGD != PTRS_PER_PTE);
#endif

	// Actually here should be pgd_t or pmd_t or pte_t, just for simplicity
	new_swapper_pt = (pte_t *)alloc_hpt_pgd_page();
	new_swapper_pg_dir = (void *)__pa(new_swapper_pt);
	deep_copy_pt((pte_t *)swapper_pg_dir, new_swapper_pt, 0);
	mb();

	pr_notice(
		"Before transfer init table: sptbr is 0x%lx, init_mm.pgd is 0x%lx\n",
		csr_read(sptbr), (unsigned long)init_mm.pgd);
	init_mm.pgd = (pgd_t *)new_swapper_pt;
	csr_write(sptbr, virt_to_pfn(new_swapper_pt) | satp_mode);
	local_flush_tlb_all();
	pr_notice(
		"After transfer init table: sptbr is 0x%lx, init_mm.pgd is 0x%lx\n",
		csr_read(sptbr), (unsigned long)init_mm.pgd);

	// clear swapper_pg_dir
	for (i = 0; i < PTRS_PER_PGD; ++i) {
		swapper_pg_dir[i].pgd = 0;
	}
}

void init_hpt_area_and_bitmap()
{
	const size_t total_ram_pages = totalram_pages();

	// allocate bitmap
	size_t bitmap_size = total_ram_pages;
	size_t bitmap_pages = (bitmap_size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	unsigned int bitmap_order = ilog2(bitmap_pages);
	if ((1 << bitmap_order) < bitmap_pages)
		bitmap_order++;
	bitmap_pages = 1 << bitmap_order;
	bitmap_size = bitmap_pages * PAGE_SIZE;
	uintptr_t bitmap_start = __get_free_pages(GFP_KERNEL, bitmap_order);
	if (!bitmap_start) {
		panic("init_hpt_area_and_bitmap: failed to allocate %lu page(s) for bitmap\n",
		      bitmap_pages);
		while (1) {
		}
	}
	printk("init_hpt_area_and_bitmap: Allocated %lu page(s) for bitmap!\n",
	       bitmap_pages);

	// allocate hpt area
	size_t hpt_pages = (total_ram_pages + PTRS_PER_PTE - 1) / PTRS_PER_PTE;
	unsigned int hpt_order = ilog2(hpt_pages - 1) + 1;
	hpt_pages = 1 << hpt_order;
	pte_pages = hpt_pages - (1 << PGD_PAGE_ORDER) - (1 << PMD_PAGE_ORDER);
	size_t hpt_size = hpt_pages * PAGE_SIZE;
	uintptr_t hpt_area_start = __get_free_pages(GFP_KERNEL, hpt_order);
	hpt_pgd_page_start = hpt_area_start;
	hpt_pmd_page_start =
		hpt_pgd_page_start + (1 << PGD_PAGE_ORDER) * PAGE_SIZE;
	hpt_pte_page_start =
		hpt_pmd_page_start + (1 << PMD_PAGE_ORDER) * PAGE_SIZE;
	memset(hpt_area_start, 0, hpt_size);
	if (hpt_area_start == NULL) {
		panic("init_hpt_area_and_bitmap: failed to allocate %lu page(s) for hpt area\n",
		      hpt_pages);
		while (1) {
		}
	}
	pr_notice("init_hpt_area_and_bitmap: Allocated %lu page(s) for hpt area!\n",
	       hpt_pages);

	// init hpt area free list
	size_t free_list_pages = hpt_pages * sizeof(struct hpt_page_list);
	free_list_pages = (free_list_pages + PAGE_SIZE - 1) / PAGE_SIZE;
	unsigned int free_list_order = ilog2(free_list_pages - 1) + 1;
	free_list_pages = 1 << free_list_order;
	hpt_pgd_page_list = (struct hpt_page_list *)__get_free_pages(
		GFP_KERNEL, free_list_order);
	hpt_pmd_page_list = (struct hpt_page_list *)__get_free_pages(
		GFP_KERNEL, free_list_order);
	hpt_pte_page_list = (struct hpt_page_list *)__get_free_pages(
		GFP_KERNEL, free_list_order);
	memset(hpt_pgd_page_list, 0, free_list_pages * PAGE_SIZE);
	memset(hpt_pmd_page_list, 0, free_list_pages * PAGE_SIZE);
	memset(hpt_pte_page_list, 0, free_list_pages * PAGE_SIZE);
	if ((hpt_pgd_page_list == NULL) || (hpt_pmd_page_list == NULL) ||
	    (hpt_pte_page_list == NULL)) {
		panic("init_hpt_area_and_bitmap: failed to allocate %lu page(s) for free list\n",
		      ((size_t)0x1) << hpt_order);
		while (1) {
		}
	}
	spin_lock_init(&hpt_lock);
	spin_lock(&hpt_lock);
	for (size_t i = 0; i < (1 << PGD_PAGE_ORDER); ++i) {
		hpt_pgd_page_list[i].next_page = hpt_pgd_free_list;
		hpt_pgd_free_list = &hpt_pgd_page_list[i];
	}
	for (size_t i = 0; i < (1 << PMD_PAGE_ORDER); ++i) {
		hpt_pmd_page_list[i].next_page = hpt_pmd_free_list;
		hpt_pmd_free_list = &hpt_pmd_page_list[i];
	}
	for (size_t i = 0; i < pte_pages; ++i) {
		hpt_pte_page_list[i].next_page = hpt_pte_free_list;
		hpt_pte_free_list = &hpt_pte_page_list[i];
	}
	pr_notice("init_hpt_area_and_bitmap: Initialized free lists!\n");
	spin_unlock(&hpt_lock);

	struct sbiret ret = sbi_ecall(
		SBI_EXT_SM, SBI_EXT_SM_BITMAP_AND_HPT_INIT, __pa(bitmap_start),
		bitmap_size, __pa(hpt_area_start), hpt_size,
		__pa(hpt_pmd_page_start), __pa(hpt_pte_page_start));
	if (unlikely(ret.error || ret.value)) {
		panic("init_hpt_area_and_bitmap: failed to init bitmap and hpt area(error: %ld, value: %ld)\n",
		      ret.error, ret.value);
		while (1) {
		}
	}

	transfer_init_pt();

	pr_notice("init_hpt_area_and_bitmap done!\n");
}

char *alloc_hpt_pgd_page(void)
{
	unsigned long pt_page_num;
	char *free_page;
	spin_lock(&hpt_lock);

	while (unlikely(hpt_pgd_free_list == NULL)) {
		pr_warn("alloc_hpt_pgd_page: no more page for PGDs\n");
		pagefault_out_of_memory();
		spin_unlock(&hpt_lock);
		return NULL;
	}

	pt_page_num = (hpt_pgd_free_list - hpt_pgd_page_list);
	// need free_page offset
	free_page = hpt_pgd_page_start + pt_page_num * PAGE_SIZE;
	hpt_pgd_free_list = hpt_pgd_free_list->next_page;

	spin_unlock(&hpt_lock);

	long error, value;
	sbi_sm_ecall(&error, &value, SBI_EXT_SM_SET_PTE,
		     SBI_EXT_SM_SET_PTE_CLEAR, __pa(free_page), 0, PAGE_SIZE, 0,
		     0);
	if (unlikely(error || value)) {
		panic("alloc_hpt_pgd_page: failed to clear page(error: %ld, value: %ld)\n",
		      error, value);
		while (1) {
		}
	}

	return free_page;
}

char *alloc_hpt_pmd_page(void)
{
	unsigned long pt_page_num;
	char *free_page;
	spin_lock(&hpt_lock);

	while (unlikely(hpt_pmd_free_list == NULL)) {
		pr_warn("alloc_hpt_pmd_page: no more page for pmds\n");
		pagefault_out_of_memory();
		spin_unlock(&hpt_lock);
		return NULL;
	}

	pt_page_num = (hpt_pmd_free_list - hpt_pmd_page_list);
	// need free_page offset
	free_page = hpt_pmd_page_start + pt_page_num * PAGE_SIZE;
	hpt_pmd_free_list = hpt_pmd_free_list->next_page;

	spin_unlock(&hpt_lock);

	long error, value;
	sbi_sm_ecall(&error, &value, SBI_EXT_SM_SET_PTE,
		     SBI_EXT_SM_SET_PTE_CLEAR, __pa(free_page), 0, PAGE_SIZE, 0,
		     0);
	if (unlikely(error || value)) {
		panic("alloc_hpt_pmd_page: failed to clear page(error: %ld, value: %ld)\n",
		      error, value);
		while (1) {
		}
	}

	return free_page;
}

char *alloc_hpt_pte_page(void)
{
	unsigned long pt_page_num;
	char *free_page;
	spin_lock(&hpt_lock);

	while (unlikely(hpt_pte_free_list == NULL)) {
		pr_warn("alloc_hpt_pte_page: no more page for ptes\n");
		pagefault_out_of_memory();
		spin_unlock(&hpt_lock);
		return NULL;
	}

	pt_page_num = (hpt_pte_free_list - hpt_pte_page_list);
	// need free_page offset
	free_page = hpt_pte_page_start + pt_page_num * PAGE_SIZE;
	hpt_pte_free_list = hpt_pte_free_list->next_page;

	spin_unlock(&hpt_lock);

	long error, value;
	sbi_sm_ecall(&error, &value, SBI_EXT_SM_SET_PTE,
		     SBI_EXT_SM_SET_PTE_CLEAR, __pa(free_page), 0, PAGE_SIZE, 0,
		     0);
	if (unlikely(error || value)) {
		panic("alloc_hpt_pte_page: failed to clear page(error: %ld, value: %ld)\n",
		      error, value);
		while (1) {
		}
	}

	return free_page;
}

int free_hpt_pgd_page(char *page)
{
	unsigned long pt_page_num;

	if (unlikely(((unsigned long)page % PAGE_SIZE) != 0)) {
		panic("ERROR: free_hpt_pgd_page: page 0x%lx is not PAGE_SIZE aligned!\n",
		      (unsigned long)page);
		return -1;
	}
	pt_page_num = ((uintptr_t)page - hpt_pgd_page_start) / PAGE_SIZE;
	if (unlikely(pt_page_num >= (1 << PGD_PAGE_ORDER))) {
		panic("ERROR: free_hpt_pgd_page: page 0x%lx is not in pt_area!\n",
		      (unsigned long)page);
		return -1;
	}

	spin_lock(&hpt_lock);

	hpt_pgd_page_list[pt_page_num].next_page = hpt_pgd_free_list;
	hpt_pgd_free_list = &hpt_pgd_page_list[pt_page_num];

	spin_unlock(&hpt_lock);

	return 0;
}

int free_hpt_pmd_page(char *page)
{
	unsigned long pt_page_num;

	if (unlikely(((unsigned long)page % PAGE_SIZE) != 0)) {
		panic("ERROR: free_hpt_pmd_page: page 0x%lx is not PAGE_SIZE aligned!\n",
		      (unsigned long)page);
		return -1;
	}
	pt_page_num = ((uintptr_t)page - hpt_pmd_page_start) / PAGE_SIZE;
	if (unlikely(pt_page_num >= (1 << PMD_PAGE_ORDER))) {
		panic("ERROR: free_hpt_pmd_page: page 0x%lx is not in pt_area!\n",
		      (unsigned long)page);
		return -1;
	}

	spin_lock(&hpt_lock);

	hpt_pmd_page_list[pt_page_num].next_page = hpt_pmd_free_list;
	hpt_pmd_free_list = &hpt_pmd_page_list[pt_page_num];

	spin_unlock(&hpt_lock);

	return 0;
}

int free_hpt_pte_page(char *page)
{
	unsigned long pt_page_num;

	if (unlikely(((unsigned long)page % PAGE_SIZE) != 0)) {
		panic("ERROR: free_hpt_pte_page: page 0x%lx is not PAGE_SIZE aligned!\n",
		      (unsigned long)page);
		return -1;
	}
	pt_page_num = ((uintptr_t)page - hpt_pte_page_start) / PAGE_SIZE;
	if (unlikely(pt_page_num >= pte_pages)) {
		panic("ERROR: free_hpt_pte_page: page 0x%lx is not in pt_area!\n",
		      (unsigned long)page);
		return -1;
	}

	spin_lock(&hpt_lock);

	hpt_pte_page_list[pt_page_num].next_page = hpt_pte_free_list;
	hpt_pte_free_list = &hpt_pte_page_list[pt_page_num];

	spin_unlock(&hpt_lock);

	return 0;
}

int check_pt_pte_page(char *page)
{
	unsigned long pt_page_num;
	pt_page_num = ((uintptr_t)page - hpt_pte_page_start) / PAGE_SIZE;
	if (pt_page_num >= pte_pages) {
		return -1;
	}
	return 0;
}
