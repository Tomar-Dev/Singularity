; archs/cpu/x86_64/interrupts/interrupts.asm
bits 64

extern isr_handler
extern irq_handler

section .text.isr

%macro ISR_NOERRCODE 1
  global isr%1
  isr%1:
    cli
    push 0              
    push %1             
    jmp isr_common_stub
%endmacro

%macro ISR_ERRCODE 1
  global isr%1
  isr%1:
    cli
    push %1             
    jmp isr_common_stub
%endmacro

%macro IRQ 2
  global irq%1
  irq%1:
    cli
    push 0              
    push %2             
    jmp irq_common_stub
%endmacro

ISR_NOERRCODE 0
ISR_NOERRCODE 1
ISR_NOERRCODE 2
ISR_NOERRCODE 3
ISR_NOERRCODE 4
ISR_NOERRCODE 5
ISR_NOERRCODE 6
ISR_NOERRCODE 7
ISR_ERRCODE   8
ISR_NOERRCODE 9
ISR_ERRCODE   10
ISR_ERRCODE   11
ISR_ERRCODE   12
ISR_ERRCODE   13
ISR_ERRCODE   14
ISR_NOERRCODE 15
ISR_NOERRCODE 16
ISR_ERRCODE   17
ISR_NOERRCODE 18
ISR_NOERRCODE 19
ISR_NOERRCODE 20
ISR_NOERRCODE 21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_ERRCODE   30
ISR_NOERRCODE 31

IRQ 0,  32
IRQ 1,  33
IRQ 2,  34
IRQ 3,  35
IRQ 4,  36
IRQ 5,  37
IRQ 6,  38
IRQ 7,  39
IRQ 8,  40
IRQ 9,  41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47
IRQ 16, 48

IRQ 253, 253
IRQ 254, 254
IRQ 255, 255

isr_common_stub:
    cld
    ; FIX: User Mode'dan gelindiyse GS yazmacını Kernela çevir.
    test qword [rsp + 24], 3
    jz .kernel_mode
    swapgs
.kernel_mode:

    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp      
    
    test rsp, 0xF
    jz .aligned
    
    sub rsp, 8
    call isr_handler
    add rsp, 8
    jmp .restore

.aligned:
    call isr_handler  

.restore:
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16       
    
    ; FIX: Kernel'a User Mode'dan geldiysen geri donerken GS'i eski haline cevir.
    test qword[rsp + 8], 3
    jz .kernel_mode_ret
    swapgs
.kernel_mode_ret:
    iretq             

irq_common_stub:
    cld 
    test qword[rsp + 24], 3
    jz .kernel_mode_irq
    swapgs
.kernel_mode_irq:

    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp      
    
    test rsp, 0xF
    jz .aligned_irq
    
    sub rsp, 8
    call irq_handler
    add rsp, 8
    jmp .restore_irq

.aligned_irq:
    call irq_handler  

.restore_irq:
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16
    
    test qword [rsp + 8], 3
    jz .kernel_mode_irq_ret
    swapgs
.kernel_mode_irq_ret:
    iretq

section .note.GNU-stack noalloc noexec nowrite progbits