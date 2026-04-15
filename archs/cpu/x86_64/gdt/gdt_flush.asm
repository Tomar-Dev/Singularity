; archs/cpu/x86_64/gdt/gdt_flush.asm
bits 64

global gdt_flush
global tss_flush

gdt_flush:
    lgdt [rdi]

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    push 0x08
    push .reload_cs
    retfq

.reload_cs:
    ret

tss_flush:
    mov ax, 0x28
    ltr ax
    ret

; FIX: Security Warning
section .note.GNU-stack noalloc noexec nowrite progbits