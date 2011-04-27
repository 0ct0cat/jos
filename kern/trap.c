#include <inc/mmu.h>
#include <inc/x86.h>
#include <inc/assert.h>
#include <inc/string.h>

#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/env.h>
#include <kern/syscall.h>
#include <kern/sched.h>
#include <kern/kclock.h>
#include <kern/picirq.h>

static struct Taskstate ts;

/* Interrupt descriptor table.  (Must be built at run time because
 * shifted function addresses can't be represented in relocation records.)
 */
struct Gatedesc idt[256] = { { 0 } };
struct Pseudodesc idt_pd = {
	sizeof(idt) - 1, (uint32_t) idt
};


static const char *trapname(int trapno)
{
	static const char * const excnames[] = {
		"Divide error",
		"Debug",
		"Non-Maskable Interrupt",
		"Breakpoint",
		"Overflow",
		"BOUND Range Exceeded",
		"Invalid Opcode",
		"Device Not Available",
		"Double Fault",
		"Coprocessor Segment Overrun",
		"Invalid TSS",
		"Segment Not Present",
		"Stack Fault",
		"General Protection",
		"Page Fault",
		"(unknown trap)",
		"x87 FPU Floating-Point Error",
		"Alignment Check",
		"Machine-Check",
		"SIMD Floating-Point Exception"
	};

	if (trapno < sizeof(excnames)/sizeof(excnames[0]))
		return excnames[trapno];
	if (trapno == T_SYSCALL)
		return "System call";
	if (trapno >= IRQ_OFFSET && trapno < IRQ_OFFSET + 16)
		return "Hardware Interrupt";
	return "(unknown trap)";
}

void HANDLER_SYSCALL();
extern uint32_t trap_handlers[];
extern uint32_t irq_handlers[];

void
idt_init(void)
{
	extern struct Segdesc gdt[];
	int i;

	// exceptions
	for (i = T_DIVIDE; i < T_SIMDERR; ++i) {
		// T_BRKPT can be invoked from user space
		if (i == T_BRKPT) {
			SETGATE(idt[i], 1, GD_KT, trap_handlers[i], 3);
		}
		else {
			SETGATE(idt[i], 1, GD_KT, trap_handlers[i], 0);
		}
	}

	// system call
	SETGATE(idt[T_SYSCALL], 1, GD_KT, HANDLER_SYSCALL, 3);

	// hardware interrupt
	for (i = IRQ_0; i <= IRQ_15; ++i) {
		SETGATE(idt[IRQ_OFFSET + i], 0, GD_KT, irq_handlers[i], 3);
	}

	// Setup a TSS so that we get the right stack
	// when we trap to the kernel.
	ts.ts_esp0 = KSTACKTOP;
	ts.ts_ss0 = GD_KD;

	// Initialize the TSS field of the gdt.
	gdt[GD_TSS >> 3] = SEG16(STS_T32A, (uint32_t) (&ts),
					sizeof(struct Taskstate), 0);
	gdt[GD_TSS >> 3].sd_s = 0;

	// Load the TSS
	ltr(GD_TSS);

	// Load the IDT
	asm volatile("lidt idt_pd");
}

void
print_trapframe(struct Trapframe *tf)
{
	cprintf("TRAP frame at %p\n", tf);
	print_regs(&tf->tf_regs);
	cprintf("  es   0x----%04x\n", tf->tf_es);
	cprintf("  ds   0x----%04x\n", tf->tf_ds);
	cprintf("  trap 0x%08x %s\n", tf->tf_trapno, trapname(tf->tf_trapno));
	cprintf("  err  0x%08x\n", tf->tf_err);
	cprintf("  eip  0x%08x\n", tf->tf_eip);
	cprintf("  cs   0x----%04x\n", tf->tf_cs);
	cprintf("  flag 0x%08x\n", tf->tf_eflags);
	cprintf("  esp  0x%08x\n", tf->tf_esp);
	cprintf("  ss   0x----%04x\n", tf->tf_ss);
}

void
print_regs(struct PushRegs *regs)
{
	cprintf("  edi  0x%08x\n", regs->reg_edi);
	cprintf("  esi  0x%08x\n", regs->reg_esi);
	cprintf("  ebp  0x%08x\n", regs->reg_ebp);
	cprintf("  oesp 0x%08x\n", regs->reg_oesp);
	cprintf("  ebx  0x%08x\n", regs->reg_ebx);
	cprintf("  edx  0x%08x\n", regs->reg_edx);
	cprintf("  ecx  0x%08x\n", regs->reg_ecx);
	cprintf("  eax  0x%08x\n", regs->reg_eax);
}

static void
trap_dispatch(struct Trapframe *tf)
{
	// Handle processor exceptions.

	switch (tf->tf_trapno) {
	case IRQ_OFFSET + IRQ_SPURIOUS:
		// Handle spurious interrupts
		// The hardware sometimes raises these because of noise on the
		// IRQ line or other reasons. We don't care.
		cprintf("Spurious interrupt on irq 7\n");
		print_trapframe(tf);
		return;
	case IRQ_OFFSET + IRQ_TIMER:
		// Handle clock interrupts
		sched_yield();
	case T_SYSCALL:
		system_call_handler(tf);
		return;
	case T_PGFLT:
		page_fault_handler(tf);
		return;
	case T_BRKPT:
		monitor(tf);
		return;
	default:
		// Unexpected trap: The user process or the kernel has a bug.
		print_trapframe(tf);
		if (tf->tf_cs == GD_KT)
			panic("unhandled trap in kernel");
		else {
			env_destroy(curenv);
			return;
		}
	}
}

void
trap(struct Trapframe *tf)
{
	// The environment may have set DF and some versions
	// of GCC rely on DF being clear
	asm volatile("cld" ::: "cc");

	// Check that interrupts are disabled.  If this assertion
	// fails, DO NOT be tempted to fix it by inserting a "cli" in
	// the interrupt path.
	assert(!(read_eflags() & FL_IF));

	if ((tf->tf_cs & 3) == 3) {
		// Trapped from user mode.
		// Copy trap frame (which is currently on the stack)
		// into 'curenv->env_tf', so that running the environment
		// will restart at the trap point.
		assert(curenv);
		curenv->env_tf = *tf;
		// The trapframe on the stack should be ignored from here on.
		tf = &curenv->env_tf;
	}
	
	// Dispatch based on what type of trap occurred
	trap_dispatch(tf);

	// If we made it to this point, then no other environment was
	// scheduled, so we should return to the current environment
	// if doing so makes sense.
	if (curenv && curenv->env_status == ENV_RUNNABLE)
		env_run(curenv);
	else
		sched_yield();
}


void
page_fault_handler(struct Trapframe *tf)
{
	uint32_t fault_va;
	struct UTrapframe utf;
	void *dst;
	size_t size;

	// Read processor's CR2 register to find the faulting address
	fault_va = rcr2();

	// Handle kernel-mode page faults.
	if ((tf->tf_cs & 3) == 0) {
		panic("kernel page fault at virtual address %p", fault_va);
		return;
	}

	// We've already handled kernel-mode exceptions, so if we get here,
	// the page fault happened in user mode.

	// Call the environment's page fault upcall, if one exists.  Set up a
	// page fault stack frame on the user exception stack (below
	// UXSTACKTOP), then branch to curenv->env_pgfault_upcall.
	//
	// The page fault upcall might cause another page fault, in which case
	// we branch to the page fault upcall recursively, pushing another
	// page fault stack frame on top of the user exception stack.
	//
	// The trap handler needs one word of scratch space at the top of the
	// trap-time stack in order to return.  In the non-recursive case, we
	// don't have to worry about this because the top of the regular user
	// stack is free.  In the recursive case, this means we have to leave
	// an extra word between the current top of the exception stack and
	// the new stack frame because the exception stack _is_ the trap-time
	// stack.
	//
	// If there's no page fault upcall, the environment didn't allocate a
	// page for its exception stack or can't write to it, or the exception
	// stack overflows, then destroy the environment that caused the fault.
	// Note that the grade script assumes you will first check for the page
	// fault upcall and print the "user fault va" message below if there is
	// none.  The remaining three checks can be combined into a single test.
	//
	// Hints:
	//   user_mem_assert() and env_run() are useful here.
	//   To change what the user environment runs, modify 'curenv->env_tf'
	//   (the 'tf' variable points at 'curenv->env_tf').

	if (curenv->env_pgfault_upcall != NULL
		&& (tf->tf_esp < UXSTACKTOP-PGSIZE
			|| tf->tf_esp >= UXSTACKTOP
			|| tf->tf_esp - sizeof(struct UTrapframe) - 4 >= UXSTACKTOP-PGSIZE)) {
		// decide the destination of UTrapframe
		if (tf->tf_esp >= UXSTACKTOP-PGSIZE && tf->tf_esp < UXSTACKTOP) {
			size = 4 + sizeof(utf);
			dst = (void *) (tf->tf_esp - size);
		}
		else {
			size = sizeof(utf);
			dst = (void *) (UXSTACKTOP - size);
		}

		// make sure exception stack is allocated and pgfault_upcall is valid
		user_mem_assert(curenv, (void *) dst, size, PTE_P|PTE_U|PTE_W);
		user_mem_assert(curenv, curenv->env_pgfault_upcall, sizeof(void *),
			PTE_P|PTE_U);

		// set up UTrapframe
		utf = (struct UTrapframe) {
			fault_va,
			tf->tf_err,
			tf->tf_regs,
			tf->tf_eip,
			tf->tf_eflags,
			curenv->env_tf.tf_esp
		};
		memmove(dst, &utf, sizeof(utf));

		curenv->env_tf.tf_esp = (uint32_t) dst;
		curenv->env_tf.tf_eip = (uint32_t) curenv->env_pgfault_upcall;
		env_run(curenv);
		return;
	}

	// Destroy the environment that caused the fault.
	cprintf("[%08x] user fault va %08x ip %08x\n",
		curenv->env_id, fault_va, tf->tf_eip);
	print_trapframe(tf);
	env_destroy(curenv);
}

void
system_call_handler(struct Trapframe *tf)
{
	struct PushRegs *regs = &(tf->tf_regs);
	uint32_t ret;
	// %eax - system call number
	// %edx to %esi - arguments for system call
	// return value stored in %eax
	ret = syscall(regs->reg_eax, regs->reg_edx, regs->reg_ecx,
		regs->reg_ebx, regs->reg_edi, regs->reg_esi);
	regs->reg_eax = ret;
}
