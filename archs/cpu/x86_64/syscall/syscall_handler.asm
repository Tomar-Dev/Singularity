; archs/cpu/x86_64/syscall/syscall_handler.asm
bits 64

global syscall_entry
global enter_user_mode
extern syscall_dispatcher

section .text

syscall_entry:
    swapgs
    
    mov[gs:0x10], rsp
    ; GÜVENLİK YAMASI: Syscall Stack Isolation
    ; Artık Ring 3'ten gelen çağrılar, Thread'in kendi Kernel Stack'ini (gs:0x08)
    ; değil, sadece Syscall'lara özel tahsis edilmiş izole yığını (gs:0x30) kullanır.
    mov rsp, [gs:0x30]

    mov r10, rcx
    sar r10, 47
    inc r10
    cmp r10, 1
    ja .invalid_rip
    
    push 0x1B          ; User SS
    push qword[gs:0x10]; User RSP
    push r11           ; User RFLAGS
    push 0x23          ; User CS
    push rcx           ; User RIP
    
    push 0             ; Error Code
    push 0x80          ; Vector

    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rbp
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax

    mov rdi, rsp 
    call syscall_dispatcher

    pop rax 
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop rbp
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15

    add rsp, 16 
    
    pop r11 
    pop rcx 
    add rsp, 8
    
    and r11, 0xFFFFFFFFFFFFCDFF 
    and r11, 0xFFFFFFFFFFFBFFFF 
    and r11, 0xFFFFFFFFFFFFBFFF 
    or  r11, 0x200              

    pop rsp     
    
    swapgs
    o64 sysret

.invalid_rip:
    swapgs
    mov rsp,[gs:0x10]
    jmp $

enter_user_mode:
    cli
    mov ax, 0x1B 
    mov ds, ax
    mov es, ax
    mov fs, ax
    
    push 0x1B
    push rsi
    
    pushf
    pop rax
    or rax, 0x200
    push rax
    
    push 0x23
    push rdi
    
    swapgs
    iretq

section .note.GNU-stack noalloc noexec nowrite progbits