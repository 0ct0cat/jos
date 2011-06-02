#include <inc/x86.h>
#include <inc/string.h>

#include <kern/env.h>
#include <kern/pci.h>
#include <kern/e100.h>
#include <kern/console.h>
#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/picirq.h>
#include <kern/sched.h>

static uint8_t e100_irq;
static uint32_t e100_base;

// Initialize E100 as a PCI device
int
e100_attach(struct pci_func *f) {
	pci_func_enable(f);

	e100_irq = f->irq_line;
	e100_base = f->reg_base[1];

	// enable E100 IRQ
	cprintf("E100 IRQ line: %d\n", e100_irq);
	set_e100_irqno(e100_irq);
	irq_setmask_8259A(irq_mask_8259A & ~(1 << e100_irq));

	// reset e100
	outl(e100_base + 8, 0x00000000);

	// mask all interrupts except FR, also not marking SI and M bits
	outb(e100_base + 3, ~((uint8_t) E100_IRQ_FR | E100_IRQ_SI | E100_IRQ_M));

	e100_ring_init();
	e100_ru_start();

	cprintf("E100 initialized.\n");

	return 0;
}

struct e100_tcb e100_tcb_ring[E100_RING_N];
struct e100_rfd e100_rfd_ring[E100_RING_N];

// begin of proceeding queue
static unsigned int e100_tcb_qbegin = 0;
// end of proceeding queue
static unsigned int e100_tcb_qend = 0;
// end of new queue (begin is qend above)
static unsigned int e100_tcb_nend = 0;

// current rfd index
static unsigned int e100_rfd_idx = 0;

// Initialize E100 DMA rings for receive and transmit
void
e100_ring_init() {
	unsigned int i, j;

	// initialize tcb
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

	// initialize rfd
	for (i = 0; i < E100_RING_N; ++i) {
		e100_rfd_ring[i] = (struct e100_rfd) {
			.rfd_hdr = {
				.cb_status = 0,
				.cb_control = 1 << 4,
				.cb_link = (uint32_t) PADDR(&e100_rfd_ring[E100_RING_NEXT(i)])
			},
			.rfd_rsv = 0,
			.rfd_count = 0,
			.rfd_size = E100_ETH_MAX_BYTE
		};
	}
};

// Returns whether the E100 commit unit (CU) is idle
bool
e100_cu_is_idle() {
	uint16_t status;
	status = inw(e100_base);
	return (status & E100_STATUS_CU_MASK) == E100_STATUS_CU_IDLE;
}

// Starts the E100 commit unit
int
e100_cu_start()
{
	outl(e100_base + 4, (uint32_t) PADDR(&e100_tcb_ring[e100_tcb_qbegin]));
	outb(e100_base + 2, E100_CU_START);
	return 0;
}

// Starts the E100 receive unit
int
e100_ru_start()
{
	outl(e100_base + 4, (uint32_t) PADDR(&e100_rfd_ring[0]));
	outb(e100_base + 2, E100_RU_START);
	return 0;
}

// Transmit a buffer with len via e100
int
e100_transmit(void *buffer, size_t len) {
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

static envid_t recv_q[QUEUE_MAX];
static unsigned int queue_front = 0;
static unsigned int queue_back = 0;

// Receive a packet to dst, returning the length
int
e100_receive(void *dst)
{
	struct e100_rfd *curitem;
	size_t len;

	curitem = &e100_rfd_ring[e100_rfd_idx];
	if (curitem->rfd_hdr.cb_status & E100_STATUS_OK) {
		len = curitem->rfd_count & E100_RFD_COUNT_MASK;
		user_mem_assert(curenv, dst, len, PTE_U|PTE_W);
		memmove(dst, curitem->rfd_data, len);
		curitem->rfd_hdr.cb_status = 0;
		e100_rfd_idx = E100_RING_NEXT(e100_rfd_idx);
		return len;
	}
	else {
		if ((queue_back + 1) % QUEUE_MAX != queue_front) {
			recv_q[queue_back] = curenv->env_id;
			queue_back = (queue_back + 1) % QUEUE_MAX;
			// block the environment
			curenv->env_status = ENV_NOT_RUNNABLE;
			curenv->env_tf.tf_regs.reg_eax = -1;
			sched_yield();
		}
		return -1;
	}
	return 0;
}

// FR interrupt - new packet has arrived
void
e100_trap_handler()
{
	// take an environment from queue
	if (queue_front != queue_back) {
		envs[ENVX(recv_q[queue_front])].env_status = ENV_RUNNABLE;
		queue_front = (queue_front + 1) % QUEUE_MAX;
	}

	// acknowledge FR interrupt and clear it
	outb(e100_base + 1, (uint8_t) ~0);
	irq_eoi();
}
