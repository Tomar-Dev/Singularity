; archs/cpu/x86_64/memory/paging_asm.asm
bits 64

global virt_to_phys_asm
global invlpg_range_asm_fast
global cr3_write_noflush
global cr3_read_pcid

section .text

; FIX: All bitmask immediates loaded via mov r11,imm64 + and to avoid the
virt_to_phys_asm:
    mov  rax, cr3
    mov  r11, 0x000FFFFFFFFFF000
    and  rax, r11

    mov  rdx, rdi
    shr  rdx, 39
    and  rdx, 0x1FF
    mov  rax, [rax + rdx * 8]
    test al, 1
    jz   .not_present

    and  rax, r11
    mov  rdx, rdi
    shr  rdx, 30
    and  rdx, 0x1FF
    mov  rax, [rax + rdx * 8]
    test al, 1
    jz   .not_present
    test al, 0x80
    jnz  .huge_1gb

    and  rax, r11
    mov  rdx, rdi
    shr  rdx, 21
    and  rdx, 0x1FF
    mov  rax, [rax + rdx * 8]
    test al, 1
    jz   .not_present
    test al, 0x80
    jnz  .huge_2mb

    and  rax, r11
    mov  rdx, rdi
    shr  rdx, 12
    and  rdx, 0x1FF
    mov  rax, [rax + rdx * 8]
    test al, 1
    jz   .not_present

    and  rax, r11
    mov  r10, 0xFFF
    and  rdi, r10
    add  rax, rdi
    ret

.huge_2mb:
    mov  r11, 0x000FFFFFE00000
    and  rax, r11
    mov  r10, 0x1FFFFF
    and  rdi, r10
    add  rax, rdi
    ret

.huge_1gb:
    mov  r11, 0x000FFFFC0000000
    and  rax, r11
    mov  r10, 0x3FFFFFFF
    and  rdi, r10
    add  rax, rdi
    ret

.not_present:
    xor  rax, rax
    ret

; FIX: page-alignment mask loaded as 64-bit literal (mov r10 + and)
invlpg_range_asm_fast:
    test rsi, rsi
    jz   .done

    mov  r10, 0xFFFFFFFFFFFFF000
    and  rdi, r10

.loop:
    invlpg [rdi]
    add    rdi, 0x1000
    dec    rsi
    jnz    .loop

.done:
    ret

cr3_write_noflush:
    mov  r10, 0x000FFFFFFFFFF000
    and  rdi, r10
    and  rsi, 0xFFF
    or   rdi, rsi
    bts  rdi, 63
    mov  cr3, rdi
    ret

; Physical base = result & 0x000FFFFFFFFFF000
cr3_read_pcid:
    mov  rax, cr3
    ret

section .note.GNU-stack noalloc noexec nowrite progbits