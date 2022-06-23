#include <config.h>
#ifndef CONFIG_SPL_BUILD

//.syntax	unified
//.arch_extension virt
.text

hyp_stack_end:
	.fill 0x100,1,0  /* 256 bytes */ 
hyp_stack:
	//.long hyp_stack_end + 0x100
	.xword hyp_stack_end + 0x100

.align 5

/* We use the same vector table for Hyp and Monitor mode, since
 * we will only use each once and they don't overlap.
 */
hyp_vectors:
	.word 0	/* reset */
	.word 0	/* undef */
	.word 0 /* smc */
	.word 0 /* pabt */
	.word 0 /* dabt */
	b	trap_hyp
	.word 0 /* irq */
	.word 0 /* fiq */

trap_hyp:
	// push {x0-x12}
	stp x0, x1, [sp, #-16]!
	stp x2, x3, [sp, #-16]!
	stp x4, x5, [sp, #-16]!
	stp x6, x7, [sp, #-16]!
	stp x8, x9, [sp, #-16]!
	stp x10, x11, [sp, #-16]!
	str x12, [sp, #-8]!
	sub sp, sp, #8		//alignment quadword 16byte

	mrs x0, spsr_el2
	mrs x1, elr_el2
	
	// push {x0, x1, lr}
	stp x0, x1, [sp, #-16]!
	str lr, [sp, #-8]!
	sub sp, sp, #8

	// add r0, sp, #0xc /* pointer user_regs to r0-r12 */
	// add x1, sp, #0xc	/* pointer user_resgs to x0-x12 */ -> draca98에서 두번째 commit에서 제거
	// mov r0, sp	-> draca98에서 두번째 commit에서 추가
	mov x0, sp

	bl do_trap_hyp

	// pop {x0, x1, lr}
	ldp x0, x1, [sp, #16]!
	ldr lr, [sp, #8]!
	add sp, sp, #8

	msr spsr_el2, x0
	// add x1, 4 -> draca98에서 두번째 commit에서 제거
	msr elr_el2, x1
	
	// pop {x0-x12}
	ldp	x0, x1, [sp, #16]!
	ldp	x2, x3, [sp, #16]!
	ldp	x4, x5, [sp, #16]!
	ldp	x6, x7, [sp, #16]!
	ldp	x8, x9, [sp, #16]!
	ldp	x10, x11, [sp, #16]!
	ldr	x12, [sp, #8]!	
	add sp, sp, #8		//alignment quadword 16byte

	eret
	
/******************************************************************************
 * This code is called from u-boot into the above handler
 */

	.globl hyp_init
hyp_init:
	// ldr sp, =hyp_stack
	ldr x0, =hyp_stack
	mov sp, x0

	//msr	elr_hyp, x30
	msr	elr_el2, x30

	// push {x0-x12}
	stp x0, x1, [sp, #-16]!
	stp x2, x3, [sp, #-16]!
	stp x4, x5, [sp, #-16]!
	stp x6, x7, [sp, #-16]!
	stp x8, x9, [sp, #-16]!
	stp x10, x11, [sp, #-16]!
	str x12, [sp, #-8]!
	sub sp, sp, #8

	//mrs r0, spsr_hyp
	//mrs r1, elr_hyp
	mrs x0, spsr_el2
	mrs x1, elr_el2

	// push {x0, x1, x30}
	stp x0, x1, [sp, #-16]!
	str x30, [sp, #-8]!
	sub sp, sp, #8

	bl hyp_mmu_init

	/* HCR */
	// mrc	p15, 4, r0, c1, c1, 0		@ sead HCR
	// orr	r0, r0, 0x4000000
	// mcr	p15, 4, r0, c1, c1, 0		@ set HCR
	mrs	x0, hcr_el2					// read HCR
	orr	x0, x0, 0x4000000
	msr	hcr_el2, x0					// set HCR

	ldr	x0, =hyp_vectors
	//mcr	p15, 4, ip, c12, c0, 0		@ set HVBAR
	msr	vbar_el2, x0				// set VBAR

	// pop {x0, x1, lr}
	ldp	x0, x1, [sp, #16]!
	ldr	lr, [sp, #8]!	
	add sp, sp, #8

	//msr spsr_hyp, x0
	//msr elr_hyp, r1
	msr spsr_el2, x0
	msr elr_el2, x1
	
	// pop {r0-r12}
	ldp	x0, x1, [sp, #16]!
	ldp	x2, x3, [sp, #16]!
	ldp	x4, x5, [sp, #16]!
	ldp	x6, x7, [sp, #16]!
	ldp	x8, x9, [sp, #16]!
	ldp	x10, x11, [sp, #16]!
	ldr	x12, [sp, #8]!	
	add sp, sp, #8

	eret
#endif
