; system/process/stack_asm.asm
bits 64

global setup_stack_frame
extern kthread_starter

section .text

setup_stack_frame:
    mov rax, rdi
    
    ; Stack'i mutlak 16-byte hizasina cek.
    mov r11, 0xFFFFFFFFFFFFFFF0
    and rax, r11
    
    ; Padding: 'ret' komutu calisip 'kthread_starter' icine girildiginde 
    ; RSP % 16 == 8 olmak ZORUNDADIR (x86_64 System V ABI).
    ; switch_to_task toplam 64 byte pop yapar (48 byte GPRs + 8 byte RFLAGS + 8 byte RIP).
    ; Dummy return address 8 byte tutar.
    ; Toplam = 72 byte. 
    ; Eger rax 16-byte hizaliysa ve 72 cikarirsak RSP % 16 == 8 cikar.
    ; Bu islemci kilitlenmelerini (AVX/SSE alignment) engeller.
    
    sub rax, 8          ; Dummy return address
    mov qword [rax], 0
    
    sub rax, 8          ; Return Instruction Pointer (RIP)
    mov r10, kthread_starter
    mov [rax], r10
    
    sub rax, 8          ; RFLAGS (Interrupts Enable = 0x202)
    mov qword [rax], 0x202
    
    sub rax, 8          ; rbx
    mov qword [rax], 0
    
    sub rax, 8          ; rbp
    mov qword [rax], 0
    
    sub rax, 8          ; r12
    mov qword [rax], 0
    
    sub rax, 8          ; r13
    mov qword [rax], 0
    
    sub rax, 8          ; r14
    mov qword [rax], 0
    
    sub rax, 8          ; r15
    mov qword [rax], 0
    
    mov [rcx], rax      ; out_rsp
    ret

section .note.GNU-stack noalloc noexec nowrite progbits