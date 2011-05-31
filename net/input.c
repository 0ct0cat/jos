#include "ns.h"

#include <inc/mmu.h>

extern union Nsipc nsipcbuf;

void
input(envid_t ns_envid)
{
	binaryname = "ns_input";

	// 	- read a packet from the device driver
	//	- send it to the network server
	// Hint: When you IPC a page to the network server, it will be
	// reading from it for a while, so don't immediately receive
	// another packet in to the same physical page.

	int len;

	while (1) {
		sys_page_alloc(0, &nsipcbuf, PTE_P|PTE_U|PTE_W);
		len = sys_receive(&nsipcbuf.pkt.jp_data);
		if (len >= 0) {
			nsipcbuf.pkt.jp_len = len;
			ipc_send(ns_envid, NSREQ_INPUT, &nsipcbuf, PTE_P|PTE_U);
		}
	}
}
