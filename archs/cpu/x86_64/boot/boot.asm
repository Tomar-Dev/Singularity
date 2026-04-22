; kernel/boot.asm
bits 32

%define MULTIBOOT2_MAGIC 0xe85250d6
%define MULTIBOOT_ARCH_I386 0
%define MULTIBOOT_TAG_TYPE_END 0
%define MULTIBOOT_TAG_TYPE_FRAMEBUFFER 5
%define MULTIBOOT_TAG_TYPE_MODULE_ALIGN 6

%define PAGE_TABLE_COUNT 4

section .multiboot_header
align 8
header_start:
    dd MULTIBOOT2_MAGIC
    dd MULTIBOOT_ARCH_I386
    dd header_end - header_start
    dd 0x100000000 - (MULTIBOOT2_MAGIC + MULTIBOOT_ARCH_I386 + (header_end - header_start))

    align 8
    dw MULTIBOOT_TAG_TYPE_FRAMEBUFFER
    dw 0
    dd 20
    dd 1024
    dd 768
    dd 32

    align 8
    dw MULTIBOOT_TAG_TYPE_MODULE_ALIGN
    dw 0
    dd 8
    
    align 8
    dw MULTIBOOT_TAG_TYPE_END
    dw 0
    dd 8
header_end:

section .bss
align 4096
global p4_table
global p3_table
global p2_tables

p4_table: resb 4096
p3_table: resb 4096
p2_tables: resb 4096 * PAGE_TABLE_COUNT

section .data
global multiboot_info_ptr
multiboot_info_ptr: dq 0

section .bootstrap_stack nobits alloc noexec write
align 16
stack_bottom: resb 65536 
stack_top:

section .rodata
gdt64:
    dq 0 
.code: equ $ - gdt64
    dq (1 << 43) | (1 << 44) | (1 << 47) | (1 << 53)
.data: equ $ - gdt64
    dq (1 << 44) | (1 << 47) | (1 << 41)
.pointer:
    dw $ - gdt64 - 1
    dq gdt64

section .text
global start
global stack_top
extern kmain

; -------------------------------------------------------------------------
; NATIVE OS ENTRY POINT (Multiboot2 Specification Standard)
; -------------------------------------------------------------------------
start:
    cli
    cld

    mov esp, stack_top
    mov[multiboot_info_ptr], ebx

    ; Paging tablolarının hazırlanması
    mov eax, p3_table
    or eax, 0b11
    mov [p4_table], eax

    mov ecx, 0
.link_p3_loop:
    mov eax, 4096
    mul ecx
    add eax, p2_tables
    or eax, 0b11
    mov [p3_table + ecx * 8], eax
    inc ecx
    cmp ecx, PAGE_TABLE_COUNT
    jne .link_p3_loop

    mov ecx, 0
    
.map_p2_loop:
    mov eax, 0x200000
    mul ecx
    
    ; OPTİMİZASYON YAMASI: Eski 3GB MMIO (PCD) kontrolü silindi.
    ; Tüm 4GB alan standart RAM (Önbelleklenebilir) olarak haritalanır.
    ; MMIO adresleri çalışma zamanında ioremap() ile düzeltilecektir.
    or eax, 0b10000011 
    
    mov[p2_tables + ecx * 8], eax
    mov [p2_tables + ecx * 8 + 4], edx
    
    inc ecx
    cmp ecx, PAGE_TABLE_COUNT * 512 
    jne .map_p2_loop

    ; 64-bit (Long Mode) ortamını hazırla
    mov eax, cr4
    or eax, 1 << 5     ; PAE (Physical Address Extension)
    mov cr4, eax

    mov eax, p4_table
    mov cr3, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8     ; LME (Long Mode Enable)
    wrmsr

    mov eax, cr0
    or eax, 1 << 31    ; PG (Paging Enable)
    mov cr0, eax

    lgdt [gdt64.pointer]
    jmp gdt64.code:long_mode_start

bits 64
long_mode_start:
    ; 64-bit Moda Kesin Geçiş
    xor ax, ax
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Güvenlik: CR0.WP ve CR4 modern ayarları
    mov rax, cr0
    and ax, 0xFFFB
    or ax, 0x2
    mov cr0, rax
    
    mov rax, cr4
    or ax, (3 << 9)    ; OSFXSR, OSXMMEXCPT
    mov cr4, rax
    
    mov rdi,[rel multiboot_info_ptr]
    call kmain
    
    cli
.halt:
    hlt
    jmp .halt

section .note.GNU-stack noalloc noexec nowrite progbits