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
    test qword [rsp + 24], 3
    jz .kernel_mode
    swapgs
.kernel_mode:

    ; 1. Genel Amaçlı Yazmaçları (GPR) Kaydet
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

    ; 2. FPU/SSE Yazmaçlarını Kaydet (GÜVENLİK YAMASI)
    mov rbp, rsp      ; Mevcut yığın işaretçisini RBP'ye yedekle
    and rsp, -16      ; fxsave için yığını 16-byte hizasına çek
    sub rsp, 512      ; FPU durumu için 512 bayt yer ayır
    fxsave [rsp]      ; Tüm XMM ve FPU yazmaçlarını yığına kaydet

    mov rdi, rbp      ; C fonksiyonuna argüman olarak GPR yapısını (registers_t*) gönder
    call isr_handler  

    ; 3. FPU/SSE Yazmaçlarını Geri Yükle
    fxrstor [rsp]     ; XMM ve FPU yazmaçlarını eski haline getir
    mov rsp, rbp      ; Yığını eski hizasına ve GPR'lerin olduğu yere geri çek

    ; 4. Genel Amaçlı Yazmaçları Geri Yükle
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

    ; 1. Genel Amaçlı Yazmaçları (GPR) Kaydet
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

    ; 2. FPU/SSE Yazmaçlarını Kaydet (GÜVENLİK YAMASI)
    mov rbp, rsp      
    and rsp, -16      
    sub rsp, 512      
    fxsave [rsp]      

    mov rdi, rbp      
    call irq_handler  

    ; 3. FPU/SSE Yazmaçlarını Geri Yükle
    fxrstor [rsp]     
    mov rsp, rbp      

    ; 4. Genel Amaçlı Yazmaçları Geri Yükle
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