; archs/cpu/x86_64/asm/fpu_asm.asm
;   1. "or ax, 0x600"  → "or eax, 0x600"  (16-bit op CR4 üst bitlerini sıfırlıyordu → #GP)
;   2. "and ax, 0xFFFB" → "and eax, ~(1<<2)" (aynı 16-bit sorunu CR0'da)
bits 64

global fpu_init_native_asm
global fpu_load_mxcsr_asm

section .text

fpu_init_native_asm:
    push rbx
    push rcx
    push rdx

    ;    CR0'a yazarken tüm 64 bit kullanılır — üst kısım bozuk olursa #GP.
    mov rax, cr0
    and eax, ~(1 << 2)
    or  eax, (1 << 1) | (1 << 5)
    mov cr0, rax

    ;    → #GP veya güvenlik bypass. Bu asıl donma sebebi buydu.
    mov rax, cr4
    or  eax, (1 << 9) | (1 << 10)
    mov cr4, rax

    mov eax, 1
    xor ecx, ecx
    cpuid
    mov ebx, ecx

    test ecx, (1 << 26)
    jz  .no_xsave

    mov rax, cr4
    or  eax, (1 << 18)
    mov cr4, rax

    mov eax, 0xD
    xor ecx, ecx
    cpuid
    test eax, eax
    jz  .no_xsave

    test ebx, (1 << 28)
    jz  .xsave_only

    xor ecx, ecx
    xgetbv
    or  eax, (1 << 0) | (1 << 1) | (1 << 2)
    xsetbv

    xor ecx, ecx
    xgetbv
    test eax, (1 << 2)
    jz  .xsave_only

    xor ecx, ecx
    xgetbv
    shl rdx, 32
    or  rax, rdx
    jmp .done

.xsave_only:
    xor ecx, ecx
    xgetbv
    or  eax, (1 << 0) | (1 << 1)
    xsetbv

    xor ecx, ecx
    xgetbv
    shl rdx, 32
    or  rax, rdx
    jmp .done

.no_xsave:
    xor rax, rax

.done:
    fninit
    pop rdx
    pop rcx
    pop rbx
    ret

fpu_load_mxcsr_asm:
    push rbp
    mov  rbp, rsp
    sub  rsp, 16
    and  rsp, -16

    mov  [rsp], edi
    ldmxcsr [rsp]

    mov  rsp, rbp
    pop  rbp
    ret

section .note.GNU-stack noalloc noexec nowrite progbits