// archs/cpu/x86_64/apic/ioapic.h
#ifndef IOAPIC_H
#define IOAPIC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IOREGSEL 0x00
#define IOWIN    0x10

#define IOAPICID  0x00
#define IOAPICVER 0x01
#define IOAPICARB 0x02
#define IOREDTBL  0x10

#define IOAPIC_DELMODE_FIXED   (0x0 << 8)
#define IOAPIC_DELMODE_LOWEST  (0x1 << 8)
#define IOAPIC_DELMODE_SMI     (0x2 << 8)
#define IOAPIC_DELMODE_NMI     (0x4 << 8)
#define IOAPIC_DELMODE_INIT    (0x5 << 8)
#define IOAPIC_DELMODE_EXTINT  (0x7 << 8)

#define IOAPIC_POL_HIGH        (0 << 13)
#define IOAPIC_POL_LOW         (1 << 13)

#define IOAPIC_TRIG_EDGE       (0 << 15)
#define IOAPIC_TRIG_LEVEL      (1 << 15)

#define IOAPIC_MASK            (1 << 16)

void init_ioapic(void);
void ioapic_set_entry(uint8_t irq, uint8_t vector);
void ioapic_set_entry_full(uint8_t irq, uint8_t vector, uint8_t dest, uint32_t delivery_flags);
void ioapic_mask_irq(uint8_t irq);
void ioapic_unmask_irq(uint8_t irq);
uint8_t ioapic_get_max_redir(void);

#ifdef __cplusplus
}
#endif

#endif