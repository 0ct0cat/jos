#ifndef JOS_KERN_E100_H
#define JOS_KERN_E100_H

#include <inc/types.h>

#include <kern/pci.h>

// length of DMA rings
#define E100_RING_N 16
#define E100_RING_NEXT(i) (((i) + 1) % E100_RING_N)

// max size of an Ethernet packet in bytes
#define E100_ETH_MAX_BYTE 1518

// e100 System Control Block (SCB) command words
#define E100_CU_START (1 << 4)

#define E100_STATUS_CU_MASK (3 << 6)
#define E100_STATUS_CU_IDLE 0

#define E100_CMD_TRNS 0x4
#define E100_CMD_EL (1 << 15)

#define E100_STATUS_OK (1 << 13)

struct e100_cb_hdr {
	volatile uint16_t cb_status;
	uint16_t cb_control;
	uint32_t cb_link;
} __attribute__ ((packed));

struct e100_tcb {
	struct e100_cb_hdr tcb_hdr;
	uint32_t tcb_tbd_addr;
	uint16_t tcb_byte_n;
	uint8_t tcb_thrs;
	uint8_t tcb_tbd_count;
	uint8_t tcb_data[E100_ETH_MAX_BYTE];
} __attribute__ ((packed));

struct e100_tcb e100_tcb_ring[E100_RING_N];

int e100_attach(struct pci_func *f);
void e100_tcb_init();

int e100_cu_start();
int e100_transmit(void *buffer, size_t len);

#endif	// JOS_KERN_E100_H
