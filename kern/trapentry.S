/* See COPYRIGHT for copyright information. */

#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <inc/trap.h>

#include <kern/picirq.h>


###################################################################
# exceptions/interrupts
###################################################################

/* TRAPHANDLER defines a globally-visible function for handling a trap.
 * It pushes a trap number onto the stack, then jumps to _alltraps.
 * Use TRAPHANDLER for traps where the CPU automatically pushes an error code.
 *
 * You shouldn't call a TRAPHANDLER function from C, but you may
 * need to _declare_ one in C (for instance, to get a function pointer
 * during IDT setup).  You can declare the function with
 *   void NAME();
 * where NAME is the argument passed to TRAPHANDLER.
 */
#define TRAPHANDLER(name, num)						\
	.globl name;		/* define global symbol for 'name' */	\
	.type name, @function;	/* symbol type is function */		\
	.align 2;		/* align function definition */		\
	name:			/* function starts here */		\
	pushl $(num);							\
	jmp _alltraps

/* Use TRAPHANDLER_NOEC for traps where the CPU doesn't push an error code.
 * It pushes a 0 in place of the error code, so the trap frame has the same
 * format in either case.
 */
#define TRAPHANDLER_NOEC(name, num)					\
	.globl name;							\
	.type name, @function;						\
	.align 2;							\
	name:								\
	pushl $0;							\
	pushl $(num);							\
	jmp _alltraps

.text

/*
 * Lab 3: Your code here for generating entry points for the different traps.
 */
	TRAPHANDLER_NOEC(int_divide, T_DIVIDE)
	TRAPHANDLER_NOEC(int_debug, T_DEBUG)
	TRAPHANDLER_NOEC(int_nmi, T_NMI)
	TRAPHANDLER_NOEC(int_brkpt, T_BRKPT)
	TRAPHANDLER_NOEC(int_oflow, T_OFLOW)
	TRAPHANDLER_NOEC(int_bound, T_BOUND)
	TRAPHANDLER_NOEC(int_illop, T_ILLOP)
	TRAPHANDLER_NOEC(int_device, T_DEVICE)
	TRAPHANDLER(int_dblflt, T_DBLFLT)
	/* RESERVED */
	TRAPHANDLER(int_tss, T_TSS)
	TRAPHANDLER(int_segnp, T_SEGNP)
	TRAPHANDLER(int_stack, T_STACK)
	TRAPHANDLER(int_gpflt, T_GPFLT)
	TRAPHANDLER(int_pgflt, T_PGFLT)
	/* RESERVED */
	TRAPHANDLER(int_fperr, T_FPERR)
	TRAPHANDLER(int_align, T_ALIGN)
	TRAPHANDLER_NOEC(int_mchk, T_MCHK)
	TRAPHANDLER_NOEC(int_simderr, T_SIMDERR)

	/* SYSTEM CALL */
	TRAPHANDLER_NOEC(system_call, T_SYSCALL)

/*
 * Lab 3: Your code here for _alltraps
 */
_alltraps:
	/* 1.push values to make the stack look like a struct Trapframe */
	pushl %ds
	pushl %es
	pushal
	/* 2.load GD_KD into %ds and %es */
	movl $GD_KD, %eax
	movw %ax,%ds
	movw %ax,%es
	/* 3.pushl %esp to pass a pointer to the Trapframe as an argument to trap() */
	pushl %esp
	/* 4.call trap (can trap ever return?) */
	call trap