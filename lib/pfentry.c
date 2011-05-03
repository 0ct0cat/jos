#include <inc/mmu.h>
#include <inc/memlayout.h>

// Page fault upcall entrypoint.

// This is where we ask the kernel to redirect us to whenever we cause
// a page fault in user space (see the call to sys_set_pgfault_handler
// in pgfault.c).
//
// When a page fault actually occurs, the kernel switches our ESP to
// point to the user exception stack if we're not already on the user
// exception stack, and then it pushes a UTrapframe onto our user
// exception stack:
//
//	trap-time esp
//	trap-time eflags
//	trap-time eip
//	utf_regs.reg_eax
//	...
//	utf_regs.reg_esi
//	utf_regs.reg_edi
//	utf_err (error code)
//	utf_fault_va            <-- %esp
//
// If this is a recursive fault, the kernel will reserve for us a
// blank word above the trap-time esp for scratch work when we unwind
// the recursive call.
//
// We then have call up to the appropriate page fault handler in C
// code, pointed to by the global variable '_pgfault_handler'.


void _pgfault_upcall()
{
	asm volatile("popl %ebp");

	asm volatile("pushl %esp\n\
		movl _pgfault_handler, %eax\n\
		call *%eax\n\
		add $4, %esp");

	asm volatile("movl 48(%esp), %eax\n\
		subl $4, %eax\n\
		movl 40(%esp), %ebx\n\
		movl %ebx, (%eax)\n\
		movl %eax, 48(%esp)");

	asm volatile("addl $8, %esp\n\
		popal");

	// Restore eflags from the stack.  After you do this, you can
	// no longer use arithmetic operations or anything else that
	// modifies eflags.
	asm volatile("addl $4, %esp\n\
		popfl");

	// Switch back to the adjusted trap-time stack.
	asm volatile("popl %esp");

	// Return to re-execute the instruction that faulted.
	asm volatile("ret");
}
