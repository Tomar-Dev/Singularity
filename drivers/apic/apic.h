// drivers/apic/apic.h
#ifndef APIC_H
#define APIC_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LAPIC_ID        0x0020  
#define LAPIC_VER       0x0030  
#define LAPIC_TPR       0x0080  
#define LAPIC_EOI       0x00B0  
#define LAPIC_SVR       0x00F0  
#define LAPIC_ESR       0x0280  
#define LAPIC_ICR_LOW   0x0300  
#define LAPIC_ICR_HIGH  0x0310  
#define LAPIC_TIMER     0x0320  
#define LAPIC_PC        0x0340  
#define LAPIC_LINT0     0x0350  
#define LAPIC_LINT1     0x0360  
#define LAPIC_ERROR     0x0370  
#define LAPIC_TICR      0x0380  
#define LAPIC_TCCR      0x0390  
#define LAPIC_TDCR      0x03E0
// FIX: Missing definitions added
#define LAPIC_ISR       0x0100
#define LAPIC_IRR       0x0200

#define MSR_X2APIC_BASE     0x800
#define MSR_X2APIC_EOI      0x80B
#define MSR_X2APIC_SVR      0x80F
#define MSR_X2APIC_ICR      0x830

#define APIC_TIMER_ONE_SHOT     0x00000
#define APIC_TIMER_PERIODIC     0x20000
#define APIC_TIMER_TSC_DEADLINE 0x40000

#define ICR_FIXED       0x00000000
#define ICR_INIT        0x00000500
#define ICR_STARTUP     0x00000600
#define ICR_LEVEL       0x00008000
#define ICR_ASSERT      0x00004000

#define SVR_ENABLE      0x100
#define SPURIOUS_VECTOR 0xFF

#define MSR_IA32_TSC_DEADLINE 0x6E0
#define MSR_IA32_APIC_BASE    0x1B

void init_apic(uint32_t physical_addr);
void apic_finalize(); 
uint8_t get_apic_id();
void apic_send_eoi();

void apic_enable_local();

void apic_timer_init(uint32_t frequency);
void apic_timer_oneshot(uint64_t us);

bool is_apic_enabled();
bool is_x2apic_enabled();

void apic_send_ipi(uint8_t apic_id, uint32_t vector);
void apic_broadcast_ipi(uint32_t vector);

uint64_t apic_get_msi_address(uint8_t cpu_id);
uint32_t apic_get_msi_data(uint8_t vector);

#ifdef __cplusplus
}
#endif

#endif
