; system/process/switch.asm
;   v1 FIX: XSAVEC (mod 3) kaldırıldı.
;   v2 FIX: Şunlar eklendi:
;        Hizalı olmayan pointer #GP yerine graceful bir serial log üretir.

bits 64

global switch_to_task
extern g_fpu_mode
extern g_xsave_mask
extern global_panic_active

switch_to_task:
    cmp byte [rel global_panic_active], 0
    jne .abort_switch_panic

    pushfq
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15

    mov [rdi], rsp

    mov r9d, [rel g_fpu_mode]
    mov rax, [rel g_xsave_mask]
    mov r10, rdx
    mov rdx, rax
    shr rdx, 32

    cmp r9d, 2
    jge .use_xsaveopt
    cmp r9d, 1
    je  .use_xsave

.use_fxsave:
    test r10, 0x0F
    jnz .fxsave_align_error
    fxsave [r10]
    jmp .stack_switch

.fxsave_align_error:
    push rax
    push rcx
    push rdx
    push rsi
    push rdi
    mov al, '!'
    mov dx, 0x3F8
    out dx, al
    mov al, 'F'
    out dx, al
    mov al, 'X'
    out dx, al
    mov al, 'A'
    out dx, al
    mov al, 10
    out dx, al
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rax
    jmp .stack_switch

    ; 64-byte boundary, a #GP is generated."
.use_xsaveopt:
    test r10, 0x3F
    jnz .xsaveopt_align_error
    xsaveopt [r10]
    jmp .stack_switch

.xsaveopt_align_error:
    test r10, 0x0F
    jnz .stack_switch
    fxsave [r10]
    jmp .stack_switch

.use_xsave:
    test r10, 0x3F
    jnz .xsave_align_error
    xsave [r10]
    jmp .stack_switch

.xsave_align_error:
    test r10, 0x0F
    jnz .stack_switch
    fxsave [r10]

.stack_switch:
    mov rsp, rsi
    mov gs:[0x20], r8

    mov rax, [rel g_xsave_mask]
    mov rdx, rax
    shr rdx, 32

    cmp r9d, 0
    je  .restore_fxrstor

    test rcx, 0x3F
    jnz .restore_xrstor_align_error
    xrstor [rcx]
    jmp .restore_gprs

.restore_xrstor_align_error:
    test rcx, 0x0F
    jnz .restore_gprs
    fxrstor [rcx]
    jmp .restore_gprs

.restore_fxrstor:
    test rcx, 0x0F
    jnz .restore_gprs
    fxrstor [rcx]

.restore_gprs:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    popfq
    ret

.abort_switch_panic:
    ret

section .note.GNU-stack noalloc noexec nowrite progbits