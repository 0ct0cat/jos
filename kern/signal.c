/*
 * Copyright 2011 Li Shiyu <ryan@ryanium.com>
 */

#include <inc/string.h>

#include <kern/signal.h>
#include <kern/env.h>
#include <kern/trap.h>
#include <kern/sched.h>
#include <kern/pmap.h>

static void
SIG_TERM() {
	env_destroy(curenv);
	sched_yield();
}

static void
SIG_DUMP() {
	print_trapframe(&(curenv->env_tf));
	env_destroy(curenv);
	sched_yield();
}

static void
SIG_CONT() {
	curenv->env_status = ENV_RUNNABLE;
	sched_yield();
}

static void
SIG_STOP() {
	curenv->env_status = ENV_NOT_RUNNABLE;
	sched_yield();
}

static struct Sigaction default_handlers[] = {
	{ SIG_TERM, NULL },     // SIGHUP
	{ SIG_TERM, NULL },     // SIGINT
	{ SIG_DUMP, NULL },     // SIGQUIT
	{ SIG_DUMP, NULL },     // SIGILL
	{ SIG_DUMP, NULL },     // SIGTRAP
	{ SIG_DUMP, NULL },     // SIGABRT, SIGIOT
	{ SIG_DUMP, NULL },     // SIGBUS
	{ SIG_DUMP, NULL },     // SIGFPE
	{ SIG_TERM, NULL },     // SIGKILL
	{ SIG_TERM, NULL },     // SIGUSR1
	{ SIG_DUMP, NULL },     // SIGSEGV
	{ SIG_TERM, NULL },     // SIGUSR2
	{ SIG_TERM, NULL },     // SIGPIPE
	{ SIG_TERM, NULL },     // SIGALRM
	{ SIG_TERM, NULL },     // SIGTERM
	{ SIG_TERM, NULL },     // SIGSTKFLT
	{ SIG_IGN, NULL },      // SIGCHLD
	{ SIG_CONT, NULL },     // SIGCONT
	{ SIG_STOP, NULL },     // SIGSTOP
	{ SIG_STOP, NULL },     // SIGTSTP
	{ SIG_STOP, NULL },     // SIGTTIN
	{ SIG_STOP, NULL },     // SIGTTOU
	{ SIG_IGN, NULL },      // SIGURG
	{ SIG_DUMP, NULL },     // SIGXCPU
	{ SIG_DUMP, NULL },     // SIGXFSZ
	{ SIG_TERM, NULL },     // SIGVTALRM
	{ SIG_TERM, NULL },     // SIGPROF
	{ SIG_IGN, NULL },      // SIGWINCH
	{ SIG_TERM, NULL },     // SIGIO, SIGPOLL
	{ SIG_TERM, NULL },     // SIGPWR
	{ SIG_DUMP, NULL }      // SIGSYS, SIGUNUSED
};

void
sig_deliver(sig_t signum, struct UTrapframe *utf)
{
	void *handler = NULL;
	size_t size;
	uint32_t dst;

	if (utf != NULL) {
		// use sighandler_info_t
		if (curenv->env_sigact[signum].handler_info != SIG_DFL_INFO)
			handler = curenv->env_sigact[signum].handler_info;
		else
			handler = default_handlers[signum].handler_info;
	}
	else {
		// use sighandler_info
		if (curenv->env_sigact[signum].handler != SIG_DFL)
			handler = curenv->env_sigact[signum].handler;
		else
			handler = default_handlers[signum].handler;
	}
	if (handler == (void *) SIG_IGN)
		return;

	if (utf->utf_esp < UXSTACKTOP-PGSIZE
		|| utf->utf_esp >= UXSTACKTOP
		|| utf->utf_esp - sizeof(struct UTrapframe) - 4 >= UXSTACKTOP-PGSIZE) {
		// decide the destination of UTrapframe
		if (utf->utf_esp >= UXSTACKTOP-PGSIZE && utf->utf_esp < UXSTACKTOP) {
			size = 4 + sizeof(*utf);
			dst = utf->utf_esp - size;
		}
		else {
			size = sizeof(*utf);
			dst = UXSTACKTOP - size;
		}

		// make sure exception stack is allocated and pgfault_upcall is valid
		user_mem_assert(curenv, (void *) dst, size, PTE_P|PTE_U|PTE_W);
		user_mem_assert(curenv, handler, sizeof(void *), PTE_P|PTE_U);

		// set up UTrapframe
		memmove((void *) dst, utf, sizeof(*utf));
		// push pointer to UTrapframe as argument to handler
		*((uint32_t *) (dst - sizeof(dst))) = dst;

		curenv->env_tf.tf_esp = dst - sizeof(dst);
		curenv->env_tf.tf_eip = (uint32_t) handler;
		env_run(curenv);
		return;
	}
}

void
sig_entry(void (*handler)(), uint32_t stacktop)
{
	asm volatile("popl %ebp");

	asm volatile("pushl %esp\n"
		"movl 0x8(%esp), %eax\n"
		"call *%eax\n"
		"add $4, %esp");

	asm volatile("movl 48(%esp), %eax\n"
		"subl $4, %eax\n"
		"movl 40(%esp), %ebx\n"
		"movl %ebx, (%eax)\n"
		"movl %eax, 48(%esp)");

	asm volatile("addl $8, %esp\n"
		"popal");

	// Restore eflags from the stack.  After you do this, you can
	// no longer use arithmetic operations or anything else that
	// modifies eflags.
	asm volatile("addl $4, %esp\n"
		"popfl");

	// Switch back to the adjusted trap-time stack.
	asm volatile("popl %esp");

	// Return to re-execute the instruction that faulted.
	asm volatile("ret");
}
