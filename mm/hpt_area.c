#include <linux/hpt_area.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <asm/sbi.h>
#include <asm/page.h>

void init_hpt_area_and_bitmap()
{
	// allocate bitmap
	size_t bitmap_size = totalram_pages();
	size_t bitmap_pages = (bitmap_size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	unsigned int order = ilog2(bitmap_pages);
	if ((1 << order) < bitmap_pages)
		order++;
	uintptr_t bitmap_start = __get_free_pages(GFP_KERNEL, order);
	if (!bitmap_start) {
		panic("init_hpt_area_and_bitmap: failed to allocate %lu page(s)\n",
		      ((size_t)0x1) << order);
		while (1) {
		}
	}
	bitmap_pages = 1 << order;
	bitmap_size = bitmap_pages * PAGE_SIZE;

	// TODO: hpt area
	uintptr_t hpt_area_start = 0;
	size_t hpt_area_size = 0;

	struct sbiret ret = sbi_ecall(
		SBI_EXT_SM, SBI_EXT_SM_BITMAP_AND_HPT_INIT, __pa(bitmap_start),
		bitmap_size, __pa(hpt_area_start), hpt_area_size, 0, 0);
	if (ret.error || ret.value) {
		panic("init_hpt_area_and_bitmap: failed to init bitmap and hpt area(error: %ld, value: %ld)\n",
		      ret.error, ret.value);
		while (1) {
		}
	}
	printk("init_hpt_area_and_bitmap\n");
}
