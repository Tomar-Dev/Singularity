; drivers/uefi/uefi_asm.asm
bits 64

global uefi_call_wrapper
global uefi_call_wrapper5_asm

section .text

uefi_call_wrapper:
    push rbp
    mov rbp, rsp

    push rbx
    push r12
    push r13
    push r14
    push r15

    sub rsp, 48

    mov r10, rdi 
    mov rcx, rsi  
    mov rdx, rdx  
    mov r8,  rcx  
    mov r9,  r8   

    call r10

    add rsp, 48
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

uefi_call_wrapper5_asm:
    push rbp
    mov rbp, rsp

    push rbx
    push r12
    push r13
    push r14
    push r15

    sub rsp, 48

    mov r10, rdi
    
    mov rcx, rsi
    mov rdx, rdx
    mov rax, r8
    mov r8,  rcx
    mov r9,  rax
    
    mov rax, r9
    mov[rsp + 32], rax

    call r10

    add rsp, 48
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

section .note.GNU-stack noalloc noexec nowrite progbits