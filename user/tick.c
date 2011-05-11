#include <inc/lib.h>

void
umain(void)
{
	unsigned int secs = 0;
	unsigned int last, cur;
	last = sys_uptime();
	while (1) {
		cur = sys_uptime();
		if (cur - last >= 1000) {
			++secs;
			last = cur;
			cprintf("Second %d of my life...\n", secs);
		}
		if (secs >= 10) {
			cprintf("Committing suicide, adieu!\n");
			return;
		}
	}
}
