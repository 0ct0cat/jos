/* See COPYRIGHT for copyright information. */

#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <inc/trap.h>

#include <kern/picirq.h>


###################################################################
# exceptions/interrupts
###################################################################

/* The TRAPHANDLER macro defines a globally-visible function for handling
 * a trap.  It pushes a trap number onto the stack, then jumps to _alltraps.
 * Use TRAPHANDLER for traps where the CPU automatically pushes an error code.
 */ 
#define TRAPHANDLER(name, num)						\
	.data; \
	.long name; \
	.text; \
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
	.data; \
	.long name; \
	.text; \
	.globl name;							\
	.type name, @function;						\
	.align 2;							\
	name:								\
	pushl $0;							\
	pushl $(num);							\
	jmp _alltraps

// Trap handler table
// For information on error code:
// http://pdos.csail.mit.edu/6.828/2010/readings/i386/s09_10.htm

.data
.globl trap_handlers
trap_handlers:
TRAPHANDLER_NOEC(HANDLER_DIVIDE, T_DIVIDE)
TRAPHANDLER_NOEC(HANDLER_DEBUG, T_DEBUG)
TRAPHANDLER_NOEC(HANDLER_NMI, T_NMI)
TRAPHANDLER_NOEC(HANDLER_BRKPT, T_BRKPT)
TRAPHANDLER_NOEC(HANDLER_OFLOW, T_OFLOW)
TRAPHANDLER_NOEC(HANDLER_BOUND, T_BOUND)
TRAPHANDLER_NOEC(HANDLER_ILLOP, T_ILLOP)
TRAPHANDLER_NOEC(HANDLER_DEVICE, T_DEVICE)
TRAPHANDLER(HANDLER_DBLFLT, T_DBLFLT)
TRAPHANDLER_NOEC(HANDLER_COPROC, T_COPROC)
TRAPHANDLER(HANDLER_TSS, T_TSS)
TRAPHANDLER(HANDLER_SEGNP, T_SEGNP)
TRAPHANDLER(HANDLER_STACK, T_STACK)
TRAPHANDLER(HANDLER_GPFLT, T_GPFLT)
TRAPHANDLER(HANDLER_PGFLT, T_PGFLT)
TRAPHANDLER_NOEC(HANDLER_RES, T_RES)
TRAPHANDLER_NOEC(HANDLER_FPERR, T_FPERR)
TRAPHANDLER_NOEC(HANDLER_ALIGN, T_ALIGN)
TRAPHANDLER_NOEC(HANDLER_MCHK, T_MCHK)
TRAPHANDLER_NOEC(HANDLER_SIMDERR, T_SIMDERR)

TRAPHANDLER_NOEC(HANDLER_SYSCALL, T_SYSCALL)


.text
_alltraps:
	pushl %ds
	pushl %es
	pushal

	pushl %eax
	movw $GD_KD, %ax
	movw %ax, %ds
	movw %ax, %es
	popl %eax

	pushl %esp
	call trap
