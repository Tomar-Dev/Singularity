; archs/cpu/x86_64/asm/fast_crc.asm
bits 64

global crc32_sse42
global crc32_and_len_asm

section .text

crc32_sse42:
    mov rax, rdi
    not eax

    mov rcx, rdx
    test rcx, rcx
    jz .done

    mov r8, rcx
    shr r8, 3
    jz .remainder

.loop_64:
    crc32 rax, qword [rsi]
    add rsi, 8
    dec r8
    jnz .loop_64

.remainder:
    and rcx, 7
    jz .done

.loop_8:
    crc32 eax, byte [rsi]
    inc rsi
    dec rcx
    jnz .loop_8

.done:
    not eax
    ret

crc32_and_len_asm:
    mov rax, rdi
    not eax
    xor rcx, rcx

.loop:
    mov r8b, byte [rsi + rcx]
    test r8b, r8b
    jz .finish

    crc32 eax, r8b
    inc rcx
    jmp .loop

.finish:
    not eax
    mov [rdx], ecx
    ret

section .note.GNU-stack noalloc noexec nowrite progbits