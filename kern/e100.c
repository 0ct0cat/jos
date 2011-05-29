#include <inc/x86.h>
#include <inc/string.h>

#include <kern/pci.h>
#include <kern/e100.h>
#include <kern/console.h>
#include <kern/pmap.h>
#include <kern/trap.h>

static uint8_t e100_irq;
static uint32_t e100_base;

int e100_attach(struct pci_func *f) {
	pci_func_enable(f);

	e100_irq = f->irq_line;
	e100_base = f->reg_base[1];

	// reset e100
	outl(e100_base + 0x8, 0x0);

	e100_tcb_init();

	return 0;
}

// begin of proceeding queue
static unsigned int e100_tcb_qbegin = 0;
// end of proceeding queue
static unsigned int e100_tcb_qend = 0;
// end of new queue (begin is qend above)
static unsigned int e100_tcb_nend = 0;

void e100_tcb_init() {
	unsigned int i, j;

	e100_tcb_qbegin = e100_tcb_qend = 0;
	e100_tcb_nend = 0;
	for (i = 0; i < E100_RING_N; ++i) {
		e100_tcb_ring[i] = (struct e100_tcb) {
			.tcb_hdr = {
				.cb_status = 0,
				.cb_control = E100_CMD_TRNS,
				.cb_link = (uint32_t) PADDR(&e100_tcb_ring[E100_RING_NEXT(i)])
			},
			.tcb_tbd_addr = 0xffffffff,
			.tcb_byte_n = 0,
			.tcb_thrs = 0xe0,
			.tcb_tbd_count = 0
		};
		e100_tcb_ring[i].tcb_data[0] = i;
	}
};

bool e100_cu_is_idle() {
	uint16_t status;
	status = inw(e100_base);
	return (status & E100_STATUS_CU_MASK) == E100_STATUS_CU_IDLE;
}

int e100_cu_start()
{
	outl(e100_base + 4, (uint32_t) PADDR(&e100_tcb_ring[e100_tcb_qbegin]));
	// start CU
	outw(e100_base + 2, E100_CU_START);
	return 0;
}

int e100_transmit(void *buffer, size_t len) {
	if (len > E100_ETH_MAX_BYTE)
		return -1;

	// need to reclaim processed packets first
	if (E100_RING_NEXT(e100_tcb_nend) == e100_tcb_qbegin) {
		// take out processed items
		while (e100_tcb_ring[e100_tcb_qbegin].tcb_hdr.cb_status & E100_STATUS_OK)
			e100_tcb_qbegin = E100_RING_NEXT(e100_tcb_qbegin);

		if (E100_RING_NEXT(e100_tcb_nend) == e100_tcb_qbegin)
			return -1;
	}

	// set up content
	e100_tcb_ring[e100_tcb_nend].tcb_hdr.cb_status = 0;
	e100_tcb_ring[e100_tcb_nend].tcb_byte_n = len;
	memmove(e100_tcb_ring[e100_tcb_nend].tcb_data, buffer, len);

	if (e100_cu_is_idle()) {
		e100_tcb_ring[e100_tcb_nend].tcb_hdr.cb_control |= E100_CMD_EL;
		e100_tcb_qbegin = e100_tcb_qend;
		e100_tcb_qend = E100_RING_NEXT(e100_tcb_nend);
		e100_cu_start();
	}

	e100_tcb_nend = E100_RING_NEXT(e100_tcb_nend);

	return 0;
}
