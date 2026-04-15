; archs/cpu/x86_64/asm/graphics.asm
bits 64

global fill_rect_avx
global memcpy_nt_avx
global draw_char_row_avx

section .rodata
align 32
font_bit_mask:
    dd 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01

section .text

fill_rect_avx:
    movd xmm0, esi              
    vpbroadcastd ymm0, xmm0     

.loop_128:
    cmp rdx, 32                 
    jb .loop_8                  

    vmovdqu [rdi], ymm0         
    vmovdqu [rdi + 32], ymm0    
    vmovdqu [rdi + 64], ymm0    
    vmovdqu [rdi + 96], ymm0    

    add rdi, 128                
    sub rdx, 32                 
    jmp .loop_128

.loop_8:
    cmp rdx, 8                  
    jb .loop_1                  

    vmovdqu [rdi], ymm0         
    add rdi, 32
    sub rdx, 8
    jmp .loop_8

.loop_1:
    test rdx, rdx
    jz .done_fill
    
    mov [rdi], esi              
    add rdi, 4
    dec rdx
    jmp .loop_1

.done_fill:
    vzeroupper                  
    ret

memcpy_nt_avx:
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
    vmovdqu ymm1, [rsi + 32]
    vmovdqu ymm2, [rsi + 64]
    vmovdqu ymm3, [rsi + 96]

    vmovntdq [rdi], ymm0
    vmovntdq [rdi + 32], ymm1
    vmovntdq [rdi + 64], ymm2
    vmovntdq [rdi + 96], ymm3

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

draw_char_row_avx:
    movd xmm0, esi
    vpbroadcastd ymm0, xmm0     

    movd xmm1, edx
    vpbroadcastd ymm1, xmm1     

    movzx eax, cl
    movd xmm2, eax              
    vpbroadcastd ymm2, xmm2     

    vpand ymm2, ymm2, [rel font_bit_mask]
    vpcmpeqd ymm2, ymm2, [rel font_bit_mask] 

    vpblendvb ymm0, ymm1, ymm0, ymm2

    vmovdqu [rdi], ymm0
    vzeroupper
    ret

section .note.GNU-stack noalloc noexec nowrite progbits