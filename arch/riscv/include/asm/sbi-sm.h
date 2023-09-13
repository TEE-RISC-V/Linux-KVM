#ifndef _ASM_RISCV_SBI_SM_H
#define _ASM_RISCV_SBI_SM_H

#include <linux/types.h>

#define SBI_EXT_SM 0x8000000

// Flags to use in sbi_sm_ecall
//
// sbi_ext_sm_fid
#define SBI_EXT_SM_SET_SHARED 0
#define SBI_EXT_SM_BITMAP_AND_HPT_INIT 1
#define SBI_EXT_SM_SET_PTE 2
#define SBI_EXT_SM_MONITOR_INIT 3
#define SBI_EXT_SM_REVERSE_MAP_INIT 4
#define SBI_EXT_SM_PREPARE_MMIO 5
#define SBI_EXT_SM_GSTAGE_MM 6


// sbi_ext_sm_set_pte_sub_fid
#define SBI_EXT_SM_SET_PTE_CLEAR 0
#define SBI_EXT_SM_SET_PTE_MEMCPY 1
#define SBI_EXT_SM_SET_PTE_SET_ONE 2

// sbi_ext_sm_gstage_mm_sub_fid
#define SBI_EXT_SM_GSTAGE_MM_SET_PTE 0



#define sbi_sm_ecall(error, value, fid, arg0, arg1, arg2, arg3, arg4, arg5)  \
	{                                                                    \
		register unsigned long a0 asm("a0") = (unsigned long)(arg0); \
		register unsigned long a1 asm("a1") = (unsigned long)(arg1); \
		register unsigned long a2 asm("a2") = (unsigned long)(arg2); \
		register unsigned long a3 asm("a3") = (unsigned long)(arg3); \
		register unsigned long a4 asm("a4") = (unsigned long)(arg4); \
		register unsigned long a5 asm("a5") = (unsigned long)(arg5); \
		register unsigned long a6 asm("a6") = (unsigned long)(fid);  \
		register unsigned long a7 asm("a7") =                        \
			(unsigned long)(SBI_EXT_SM);                         \
		asm volatile("ecall"                                         \
			     : "+r"(a0), "+r"(a1)                            \
			     : "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a6),  \
			       "r"(a7)                                       \
			     : "memory");                                    \
		*(error) = a0;                                               \
		*(value) = a1;                                               \
	}

#endif
