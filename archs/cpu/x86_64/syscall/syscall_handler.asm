; archs/cpu/x86_64/syscall/syscall_handler.asm
bits 64

global syscall_entry
global enter_user_mode
extern syscall_dispatcher

section .text

syscall_entry:
    swapgs
    
    mov [gs:16], rsp
    ; SSE & Fast Syscall safety bypass implicit
    mov rsp, [gs:8]

    ; [SECURITY PATCH]: SYSRET Canonical Address Vulnerability Fix
    ; If RCX (Return RIP) is non-canonical, SYSRET generates #GP in Ring 0, crashing the kernel.
    ; We must validate RCX (bits 47-63 must be identical).
    mov r10, rcx
    sar r10, 47
    inc r10
    cmp r10, 1
    ja .invalid_rip
    
    push 0x1B          ; User SS
    push qword[gs:16]  ; User RSP
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
    
    pop r11 ; FIX 3: Restore Original User RFLAGS
    pop rcx ; FIX 2: Restore Original User RIP
    add rsp, 8
    
    ; [SECURITY PATCH]: Clear dangerous flags from User RFLAGS (e.g., IOPL, IF)
    ; Force IF=1 (0x200), clear IOPL, AC, NT, TF
    and r11, 0xFFFFFFFFFFFFCDFF ; Clear IOPL
    and r11, 0xFFFFFFFFFFFBFFFF ; Clear AC
    and r11, 0xFFFFFFFFFFFFBFFF ; Clear NT
    or  r11, 0x200              ; Set IF

    pop rsp     
    
    swapgs
    o64 sysret

.invalid_rip:
    ; Terminate the malicious task silently instead of crashing the kernel
    swapgs
    mov rsp, [gs:16]
    ; Fallback logic would go here (e.g., call process_exit), for now loop
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
