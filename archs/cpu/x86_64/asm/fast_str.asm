; archs/cpu/x86_64/asm/fast_str.asm
bits 64

global strlen_sse2
global memcmp_sse2
global memchr_avx

section .text

strlen_sse2:
    mov rax, rdi
    mov rcx, rdi
    pxor xmm0, xmm0

.align_loop:
    test rcx, 0xF
    jz .aligned_loop
    cmp byte [rcx], 0
    je .done
    inc rcx
    jmp .align_loop

    align 16
.aligned_loop:
    movdqa xmm1, [rcx]
    pcmpeqb xmm1, xmm0
    pmovmskb edx, xmm1
    
    test edx, edx
    jnz .found
    
    add rcx, 16
    jmp .aligned_loop

.found:
    bsf edx, edx
    add rcx, rdx

.done:
    sub rcx, rax
    mov rax, rcx
    ret

memcmp_sse2:
    test rdx, rdx
    jz .equal

    cmp rdx, 32
    jb .fallback

.loop_16:
    movdqu xmm0, [rdi]
    movdqu xmm1, [rsi]
    pcmpeqb xmm0, xmm1
    pmovmskb eax, xmm0
    
    cmp eax, 0xFFFF
    jne .mismatch_found
    
    add rdi, 16
    add rsi, 16
    sub rdx, 16
    cmp rdx, 16
    jae .loop_16

    test rdx, rdx
    jz .equal

.fallback:
    mov al, [rdi]
    mov cl, [rsi]
    cmp al, cl
    jne .diff
    inc rdi
    inc rsi
    dec rdx
    jnz .fallback

.equal:
    xor rax, rax
    ret

.diff:
    sub eax, ecx
    movsx rax, al
    ret

.mismatch_found:
    jmp .fallback

memchr_avx:
    test rdx, rdx
    jz .not_found

    movd xmm0, esi
    vpbroadcastb ymm0, xmm0

    cmp rdx, 32
    jb .chr_fallback

.chr_loop:
    vmovdqu ymm1, [rdi]
    vpcmpeqb ymm1, ymm1, ymm0
    vpmovmskb eax, ymm1

    test eax, eax
    jnz .chr_found

    add rdi, 32
    sub rdx, 32
    cmp rdx, 32
    jae .chr_loop

    vzeroupper

    test rdx, rdx
    jz .not_found

.chr_fallback:
    cmp byte [rdi], sil
    je .found_ptr
    inc rdi
    dec rdx
    jnz .chr_fallback

.not_found:
    xor rax, rax
    ret

.chr_found:
    bsf eax, eax
    add rdi, rax
.found_ptr:
    mov rax, rdi
    vzeroupper
    ret

section .note.GNU-stack noalloc noexec nowrite progbits