// archs/cpu/x86_64/smp/smp.c
#include "archs/cpu/x86_64/smp/smp.h"
#include "drivers/acpi/acpi.h"
#include "drivers/apic/apic.h"
#include "memory/pmm.h"
#include "memory/paging.h"
#include "memory/kheap.h"
#include "libc/string.h"
#include "libc/stdio.h"
#include "archs/cpu/x86_64/gdt/gdt.h"
#include "archs/cpu/x86_64/idt/idt.h"
#include "drivers/serial/serial.h"
#include "archs/cpu/x86_64/core/msr.h"
#include "kernel/config.h"
#include "system/process/process.h"
#include "archs/cpu/cpu_hal.h"
#include "archs/cpu/x86_64/context/tss.h"
#include "archs/cpu/x86_64/core/fpu.h"
#include "archs/cpu/x86_64/syscall/syscall.h"
#include "drivers/timer/tsc.h"
#include "kernel/debug.h"
extern void print_status(const char* prefix, const char* msg, const char* status);

extern uint8_t trampoline_start[];
extern uint8_t trampoline_end[];
extern uint8_t p4_table_ptr[];
extern uint8_t ap_startup_ptr[];
extern uint8_t stack_ptr_ptr[];
extern uint32_t fpu_default_mxcsr;

extern uint64_t stack_top;
extern uintptr_t __stack_chk_guard;

smp_cpu_t cpus[MAX_CPUS];
per_cpu_t* per_cpu_data[MAX_CPUS];
uint8_t num_cpus = 0;

static volatile uint32_t ap_boot_flag = 0;

static per_cpu_t fallback_cpu_data[MAX_CPUS];

#define SMP_STACK_SIZE 65536

__attribute__((no_stack_protector))
void prepare_cpu_data(uint8_t cpu_id, uint32_t lapic_id) {
    if (cpu_id >= MAX_CPUS) {
        return;
    } else {
        if (!per_cpu_data[cpu_id]) {
            per_cpu_t* cpu_data = (per_cpu_t*)kmalloc_aligned(sizeof(per_cpu_t), 64);
            if (!cpu_data) {
                serial_write("SMP OOM - Using Fallback!\n");
                cpu_data = &fallback_cpu_data[cpu_id];
            }
            
            memset(cpu_data, 0, sizeof(per_cpu_t));

            cpu_data->self       = cpu_data;
            cpu_data->cpu_id     = cpu_id;
            cpu_data->lapic_id   = lapic_id;
            cpu_data->stack_canary = __stack_chk_guard;

            if (cpu_id == 0) {
                cpu_data->kernel_stack = (uint64_t)&stack_top;
            } else {
                void* kstack = kmalloc(SMP_STACK_SIZE);
                if (kstack) {
                    cpu_data->kernel_stack = (uint64_t)kstack + SMP_STACK_SIZE;
                } else {
                    serial_write("SMP Stack OOM! Halting AP initialization.\n");
                    panic_at(__FILE__, __LINE__, KERR_MEM_OOM, "AP Stack Allocation Failed! Cannot boot CPU safely.");
                }
            }

            per_cpu_data[cpu_id] = cpu_data;
        }
    }
}

__attribute__((no_stack_protector))
void smp_load_gs(uint8_t cpu_id) {
    if (cpu_id >= MAX_CPUS) {
        return;
    } else {
        if (!per_cpu_data[cpu_id]) {
            return;
        } else {
            uint64_t addr = (uint64_t)per_cpu_data[cpu_id];
            wrmsr(MSR_GS_BASE, addr);
            wrmsr(MSR_KERNEL_GS_BASE, 0); 
        }
    }
}

static void microdelay(int us) {
    uint64_t start = tsc_read_asm();
    uint64_t freq = get_tsc_freq();
    if (freq == 0) {
        freq = 2000000000ULL; 
    }
    
    uint64_t target = start + (freq / 1000000) * us;
    while (tsc_read_asm() < target) {
        hal_cpu_relax();
    }
}

__attribute__((no_stack_protector))
void init_smp() {
    hal_interrupts_disable();

    prepare_cpu_data(0, get_apic_id());
    smp_load_gs(0);

    uint64_t trampoline_base = 0x8000;
    uint32_t trampoline_size = (uint32_t)(trampoline_end - trampoline_start);

    map_page(0x8000, 0x8000, PAGE_PRESENT | PAGE_WRITE | PAGE_PCD);
    map_page(0x7000, 0x7000, PAGE_PRESENT | PAGE_WRITE | PAGE_PCD);

    memcpy((void*)trampoline_base, trampoline_start, trampoline_size);

    uintptr_t off_p4    = (uintptr_t)p4_table_ptr    - (uintptr_t)trampoline_start;
    uintptr_t off_entry = (uintptr_t)ap_startup_ptr  - (uintptr_t)trampoline_start;
    uintptr_t off_stack = (uintptr_t)stack_ptr_ptr   - (uintptr_t)trampoline_start;

    volatile uint64_t* target_p4    = (uint64_t*)(trampoline_base + off_p4);
    volatile uint64_t* target_entry = (uint64_t*)(trampoline_base + off_entry);
    volatile uint64_t* target_stack = (uint64_t*)(trampoline_base + off_stack);

    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));

    *target_p4    = cr3;
    *target_entry = (uint64_t)&ap_startup;

    cpus[0].lapic_id = get_apic_id();
    cpus[0].status   = 2;
    num_cpus = 1;

    int limit;
    if (acpi_cpu_count > 0) {
        limit = acpi_cpu_count;
    } else {
        limit = 8;
    }
    
    int logical_id_counter = 1;

    serial_write("[SMP] Booting APs sequentially...\n");

    uint64_t freq = get_tsc_freq();
    if (freq == 0) freq = 2000000000; 

    for (int i = 0; i < limit; i++) {
        uint8_t apic_id;
        if (acpi_cpu_count > 0) apic_id = acpi_cpu_ids[i];
        else apic_id = i;
        
        if (apic_id == cpus[0].lapic_id) continue;
        
        prepare_cpu_data(logical_id_counter, apic_id);

        void* ap_stack = kmalloc_aligned(SMP_STACK_SIZE, 4096);
        if (!ap_stack) continue;
        
        *target_stack = (uint64_t)ap_stack + SMP_STACK_SIZE;
        
        __atomic_store_n(&ap_boot_flag, 0, __ATOMIC_SEQ_CST);
        __asm__ volatile("mfence" ::: "memory");

        apic_send_ipi(apic_id, 0x00004500); 
        microdelay(100);
        apic_send_ipi(apic_id, 0x00004608); 
        microdelay(2000); // FIX 11: Older CPU SIPI Stabilization
        apic_send_ipi(apic_id, 0x00004608); 

        uint64_t start_tsc = tsc_read_asm();
        uint64_t timeout_cycles = (freq / 1000) * 50; // 50ms Timeout
        bool booted = false;
        
        while ((tsc_read_asm() - start_tsc) < timeout_cycles) {
            if (__atomic_load_n(&ap_boot_flag, __ATOMIC_SEQ_CST) == apic_id) {
                booted = true;
                break;
            }
            hal_cpu_relax();
        }
        
        // FIX: Eger cekirdek uyanmadiysa (Timeout), bir sonraki cekirdegi baslatmayi durdur.
        // Aksi takdirde gec uyanan cekirdek (Zombi AP), bir sonraki cekirdegin stack'ini
        // alip ust uste calisir (Stack Collision) ve sistem cokecekti.
        if (booted) {
            logical_id_counter++;
        } else {
            serial_printf("[SMP] WARNING: AP %d failed to wake up. Halting further AP boot sequence to prevent Stack Collision.\n", apic_id);
            break; 
        }
    }

    char msg[64];
    sprintf(msg, "Sequential Boot: %d CPUs Online", num_cpus);
    print_status("[ SMP  ]", msg, "INFO");
}

static int ap_init_lock = 0;

__attribute__((no_stack_protector))
void ap_startup() {
    uint8_t my_apic = get_apic_id();
    
    while (__atomic_test_and_set(&ap_init_lock, __ATOMIC_ACQUIRE)) {
        hal_cpu_relax();
    }

    apic_enable_local();

    int my_logical_id = -1;
    for (int i = 0; i < MAX_CPUS; i++) {
        if (per_cpu_data[i]) {
            if (per_cpu_data[i]->lapic_id == my_apic) {
                my_logical_id = i;
                break;
            }
        }
    }

    if (my_logical_id != -1) {
        gdt_init_cpu(my_logical_id);
        idt_load();
        smp_load_gs(my_logical_id);

        uint64_t my_stack = per_cpu_data[my_logical_id]->kernel_stack;
        __asm__ volatile("mov %0, %%rsp" :: "r"(my_stack));

        tss_install(my_logical_id, my_stack);

        cpus[num_cpus].lapic_id = my_apic;
        cpus[num_cpus].status   = 2;
        
        __atomic_fetch_add(&num_cpus, 1, __ATOMIC_SEQ_CST);
    } else {
        panic_at(__FILE__, __LINE__, KERR_UNKNOWN, "Unregistered AP woke up!");
    }

    __atomic_clear(&ap_init_lock, __ATOMIC_RELEASE);
    
    __atomic_store_n(&ap_boot_flag, my_apic, __ATOMIC_SEQ_CST);

    init_fpu();
    init_syscalls(); 

    hal_interrupts_disable();
    apic_timer_init(250);
    hal_interrupts_enable();

    init_multitasking_ap();

    while (1) {
        yield();
        hal_cpu_halt();
    }
}

uint8_t get_current_cpu_id() {
    return get_apic_id();
}
