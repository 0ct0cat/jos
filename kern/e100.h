#ifndef JOS_KERN_E100_H
#define JOS_KERN_E100_H

#include <inc/types.h>

#include <kern/pci.h>
#include <kern/env.h>

// length of DMA rings
#define E100_RING_N 128
#define E100_RING_NEXT(i) (((i) + 1) % E100_RING_N)

// max size of an Ethernet packet in bytes
#define E100_ETH_MAX_BYTE 1518

// e100 System Control Block (SCB) command words
#define E100_CU_START (1 << 4)
#define E100_RU_START 1

// IRQ bits that are not masked
#define E100_IRQ_FR (1 << 6)
#define E100_IRQ_SI (1 << 1)
#define E100_IRQ_M 1

#define E100_STATUS_CU_MASK (3 << 6)
#define E100_STATUS_CU_IDLE 0

#define E100_CMD_TRNS 0x4
#define E100_CMD_EL (1 << 15)

#define E100_RFD_COUNT_MASK 0x3fff

#define E100_STATUS_OK (1 << 13)

#define QUEUE_MAX 32

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

struct e100_rfd {
	struct e100_cb_hdr rfd_hdr;
	uint32_t rfd_rsv;
	uint16_t rfd_count;
	uint16_t rfd_size;
	uint8_t rfd_data[E100_ETH_MAX_BYTE];
} __attribute__ ((packed));

int e100_attach(struct pci_func *f);
void e100_ring_init();

int e100_cu_start();
int e100_ru_start();

int e100_transmit(void *buffer, size_t len);
int e100_receive(void *buffer);

void e100_trap_handler();

#endif	// JOS_KERN_E100_H
