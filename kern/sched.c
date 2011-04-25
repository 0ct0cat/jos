#include <inc/assert.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/monitor.h>


// Choose a user environment to run and run it.
void
sched_yield(void)
{
	// Implement simple round-robin scheduling.
	// Search through 'envs' for a runnable environment,
	// in circular fashion starting after the previously running env,
	// and switch to the first such environment found.
	// It's OK to choose the previously running env if no other env
	// is runnable.
	// But never choose envs[0], the idle environment,
	// unless NOTHING else is runnable.

	static unsigned int last_run = 0;
	unsigned int i, idx;

	for (i = 0; i < NENV; ++i) {
		idx = (i + last_run + 1) % NENV; // start from the next one
		if (idx > 0 && envs[idx].env_status == ENV_RUNNABLE) {
			last_run = idx;
			env_run(&envs[idx]);
			return;
		}
	}

	// Run the special idle environment when nothing else is runnable.
	if (envs[0].env_status == ENV_RUNNABLE) {
		last_run = 0;
		env_run(&envs[0]);
	}
	else {
		cprintf("Destroyed all environments - nothing more to do!\n");
		while (1)
			monitor(NULL);
	}
}
