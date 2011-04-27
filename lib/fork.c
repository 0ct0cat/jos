// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at vpt
	//   (see <inc/memlayout.h>).

	if (!((err & FEC_WR) && (vpt[VPN(addr)] & PTE_COW))) {
		cprintf("virtual address: %08x, %d, %d\n", addr, err & FEC_WR, vpt[VPN(addr)] & PTE_COW);
		panic("unrecoverable user page fault");
	}

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.
	//   No need to explicitly delete the old page's mapping.

	if ((r = sys_page_alloc(0, PFTEMP, PTE_W|PTE_U|PTE_P)) < 0) {
		cprintf("sys_page_alloc() error: %e\n", r);
		panic("cannot alloc page");
	}
	memmove((void *) PFTEMP, ROUNDDOWN(addr, PGSIZE), PGSIZE);
	if ((r = sys_page_map(0, (void *) PFTEMP,
		0, ROUNDDOWN(addr, PGSIZE), PTE_W|PTE_U|PTE_P)) < 0) {
		cprintf("sys_page_map() error: %e\n", r);
		panic("cannot map page");
	}
	if ((r = sys_page_unmap(0, (void *) PFTEMP)) < 0) {
		cprintf("sys_page_unmap() error: %e\n", r);
		panic("cannot unmap page");
	}
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
// 
static int
duppage(envid_t envid, unsigned pn)
{
	int r, perm;
	pte_t pt_entry;

	pt_entry = vpt[pn];

	// find permissions
	if (pt_entry & (PTE_W|PTE_COW))
		perm = PTE_COW|PTE_U|PTE_P;
	else
		perm = PTE_U|PTE_P;

	// if map as copy-on-write, remap with different perm
	if (perm & PTE_COW)
		if ((r = sys_page_map(0, (void *) (pn * PGSIZE),
			0, (void *) (pn * PGSIZE), perm)) < 0) {
			cprintf("cannot remap at %08x\n", pn * PGSIZE);
			return r;
		}
	// duplicate mapping at envid
	if ((r = sys_page_map(0, (void *) (pn * PGSIZE),
		envid, (void *) (pn * PGSIZE), perm)) < 0) {
		cprintf("cannot duplicate mapping at %08x\n", pn * PGSIZE);
		return r;
	}
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use vpd, vpt, and duppage.
//   Remember to fix "env" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	int r;
	unsigned int pd_idx, pt_idx;
	envid_t child_id;
	set_pgfault_handler(pgfault);

	child_id = sys_exofork();

	if (child_id < 0) {
		// error
	}
	else if (child_id > 0) {
		// parent
		for (pd_idx = 0; pd_idx < PDX(UTOP); ++pd_idx) {
			if (vpd[pd_idx] & PTE_P) {
				for (pt_idx = 0; pt_idx < NPTENTRIES; ++pt_idx) {
					if ((pd_idx << PDXSHIFT) + (pt_idx << PTXSHIFT) != UXSTACKTOP - PGSIZE) {
						if (vpt[VPN((pd_idx << PDXSHIFT) + (pt_idx << PTXSHIFT))] & PTE_P) {
							if ((r = duppage(child_id, VPN((pd_idx << PDXSHIFT) + (pt_idx << PTXSHIFT)))) < 0) {
								cprintf("error duplicating at %08x: %e\n",
									(pd_idx << PDXSHIFT) + (pt_idx << PTXSHIFT),
									r);
								panic("boo");
							}
						}
					}
				}
			}
		}
		if ((r = sys_page_alloc(child_id, (void *) (UXSTACKTOP - PGSIZE), PTE_W)) < 0)
			return r;
		if ((r = sys_env_set_pgfault_upcall(child_id, env->env_pgfault_upcall)) < 0)
			return r;
		if ((r = sys_env_set_status(child_id, ENV_RUNNABLE)) < 0)
			return r;
	}
	else {
		// child
		env = &envs[ENVX(sys_getenvid())];
		return 0;
	}
	return child_id;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
