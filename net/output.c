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
		sys_transmit(nsipcbuf.pkt.jp_data, nsipcbuf.pkt.jp_len);
	}
}
