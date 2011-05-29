#include "ns.h"

#include <inc/syscall.h>

extern union Nsipc nsipcbuf;

void
output(envid_t ns_envid)
{
	binaryname = "ns_output";

	envid_t envid;

	// read a packet from the network server
	// send the packet to the device driver
	while (1) {
		ipc_recv(&envid, &nsipcbuf, 0);
		if (envid != ns_envid)
			continue;
		unsigned int i;
		cprintf("Data (%d): ", nsipcbuf.pkt.jp_len);
		for (i = 0; i < nsipcbuf.pkt.jp_len; ++i)
			cprintf("%x ", nsipcbuf.pkt.jp_data[i]);
		cprintf("\n");
		sys_transmit(nsipcbuf.pkt.jp_data, nsipcbuf.pkt.jp_len);
	}
}
