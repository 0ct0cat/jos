#include <inc/lib.h>

void
pgflt_handler(struct UTrapframe *utf)
{
	cprintf("fault address: %08x\n", utf);
	exit();
}

void
umain()
{
	struct Sigaction action = { NULL, (sighandler_info_t *) pgflt_handler };
	int a;

	sys_sigaction(SIGSEGV, &action, NULL);
	a = *((int *) 0xdeadbeef);
	cprintf("a: %d\n", a);
}
