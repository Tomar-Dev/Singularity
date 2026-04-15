; archs/cpu/x86_64/asm/fast_mem.asm
bits 64

global memcpy_avx
global memset_avx
global memzero_nt_avx_asm
global memcpy_nt_avx_asm
global memcpy64_asm
global memcpy_sse2_wc

section .text

memcpy_sse2_wc:
    cmp rdx, 16
    jb .fallback_wc

.loop_wc:
    cmp rdx, 64
    jb .tail_wc

    movdqu xmm0, [rsi]
    movdqu xmm1, [rsi + 16]
    movdqu xmm2,[rsi + 32]
    movdqu xmm3, [rsi + 48]

    movntdq [rdi], xmm0
    movntdq [rdi + 16], xmm1
    movntdq [rdi + 32], xmm2
    movntdq[rdi + 48], xmm3

    add rsi, 64
    add rdi, 64
    sub rdx, 64
    jmp .loop_wc

.tail_wc:
    cmp rdx, 16
    jb .fallback_wc

    movdqu xmm0, [rsi]
    movntdq [rdi], xmm0
    
    add rsi, 16
    add rdi, 16
    sub rdx, 16
    jmp .tail_wc

.fallback_wc:
    test rdx, rdx
    jz .done_wc
    
    mov rcx, rdx
    rep movsb

.done_wc:
    sfence
    ret

memcpy_avx:
    mov rax, rdi        
    cmp rdx, 128
    jb .fallback

.loop_128:
    vmovdqu ymm0,[rsi]
    vmovdqu ymm1, [rsi + 32]
    vmovdqu ymm2, [rsi + 64]
    vmovdqu ymm3,[rsi + 96]

    vmovdqu [rdi], ymm0
    vmovdqu[rdi + 32], ymm1
    vmovdqu [rdi + 64], ymm2
    vmovdqu [rdi + 96], ymm3

    add rsi, 128
    add rdi, 128
    sub rdx, 128
    cmp rdx, 128
    jae .loop_128

    vzeroupper

.fallback:
    mov rcx, rdx
    rep movsb
    ret

memset_avx:
    mov rax, rdi        
    cmp rdx, 128
    jb .fallback

    movd xmm0, esi          
    vpbroadcastb ymm0, xmm0 

.loop_128:
    vmovdqu [rdi], ymm0
    vmovdqu[rdi + 32], ymm0
    vmovdqu [rdi + 64], ymm0
    vmovdqu [rdi + 96], ymm0

    add rdi, 128
    sub rdx, 128
    cmp rdx, 128
    jae .loop_128

    vzeroupper

.fallback:
    mov eax, esi    
    mov rcx, rdx    
    rep stosb
    ret

memzero_nt_avx_asm:
    vpxor ymm0, ymm0, ymm0  

.align_loop:
    test rdi, 31
    jz .loop_nt
    test rsi, rsi
    jz .done_nt
    mov byte [rdi], 0
    inc rdi
    dec rsi
    jmp .align_loop

.loop_nt:
    cmp rsi, 128
    jb .fallback_nt

    vmovntdq [rdi], ymm0
    vmovntdq [rdi + 32], ymm0
    vmovntdq[rdi + 64], ymm0
    vmovntdq [rdi + 96], ymm0

    add rdi, 128
    sub rsi, 128
    jmp .loop_nt

.fallback_nt:
    test rsi, rsi
    jz .done_nt
    
    mov rcx, rsi
    xor rax, rax
    rep stosb

.done_nt:
    sfence                  
    vzeroupper
    ret

memcpy_nt_avx_asm:
    cmp rdx, 32
    jb .fallback_copy

    mov rax, rdi
    and rax, 31
    jz .aligned_loop

    mov rcx, 32
    sub rcx, rax 
    sub rdx, rcx 
    
.align_loop:
    mov al, [rsi]
    mov [rdi], al
    inc rsi
    inc rdi
    dec rcx
    jnz .align_loop

.aligned_loop:
    cmp rdx, 128
    jb .tail_loop

    vmovdqu ymm0, [rsi]
    vmovdqu ymm1,[rsi + 32]
    vmovdqu ymm2, [rsi + 64]
    vmovdqu ymm3, [rsi + 96]

    vmovntdq [rdi], ymm0
    vmovntdq [rdi + 32], ymm1
    vmovntdq [rdi + 64], ymm2
    vmovntdq[rdi + 96], ymm3

    add rsi, 128
    add rdi, 128
    sub rdx, 128
    jmp .aligned_loop

.tail_loop:
    cmp rdx, 32
    jb .fallback_copy

    vmovdqu ymm0, [rsi]
    vmovntdq [rdi], ymm0
    
    add rsi, 32
    add rdi, 32
    sub rdx, 32
    jmp .tail_loop

.fallback_copy:
    test rdx, rdx
    jz .done_copy
    
    mov rcx, rdx
    rep movsb

.done_copy:
    sfence          
    vzeroupper
    ret

memcpy64_asm:
    mov rcx, rdx
    shr rcx, 3      
    rep movsq       
    mov rcx, rdx
    and rcx, 7      
    rep movsb       
    ret

section .note.GNU-stack noalloc noexec nowrite progbits