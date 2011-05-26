#include <inc/x86.h>

#include <kern/pci.h>
#include <kern/e100.h>
#include <kern/console.h>

static uint8_t e100_irq;
static uint32_t e100_base;

int e100_attach(struct pci_func *f) {
	pci_func_enable(f);

	e100_irq = f->irq_line;
	e100_base = f->reg_base[1];

	// reset e100
	outl(e100_base + 0x8, 0x0);

	return 0;
}
