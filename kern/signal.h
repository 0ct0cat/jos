/*
 * Copyright 2011 Li Shiyu <ryan@ryanium.com>
 */

#ifndef JOS_KERN_SIGNAL_H
#define JOS_KERN_SIGNAL_H

#include <inc/signal.h>

void sig_deliver(sig_t signum, struct UTrapframe *utf);
void sig_entry(void (*handler)(), uint32_t stacktop);

#endif /* JOS_INC_SIGNAL_H */
