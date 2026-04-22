; drivers/timer/timer_asm.asm
bits 64

global pit_wait_calibration_asm
global pit_channel2_start_asm

section .text

pit_channel2_start_asm:
    push rbx
    mov ebx, edi
    
    mov dx, 0x43
    mov al, 0xB6
    out dx, al
    
    mov dx, 0x42
    mov eax, ebx
    out dx, al
    mov al, bh
    out dx, al
    
    in al, 0x61
    or al, 0x03
    out 0x61, al
    
    pop rbx
    ret

pit_wait_calibration_asm:
    push rbx
    push rcx
    
    mov rax, 1193
    mul rdi
    
    cmp rax, 0xFFFF
    jbe .valid
    mov rax, 0xFFFF
.valid:
    mov rbx, rax
    
    in al, 0x61
    and al, 0xFD
    or al, 0x01
    out 0x61, al
    
    mov dx, 0x43
    mov al, 0xB0
    out dx, al
    
    mov dx, 0x42
    mov eax, ebx
    out dx, al
    mov al, bh
    out dx, al
    
.wait:
    pause
    in al, 0x61
    test al, 0x20
    jz .wait
    
    in al, 0x61
    and al, 0xFE
    out 0x61, al
    
    pop rcx
    pop rbx
    ret

section .note.GNU-stack noalloc noexec nowrite progbits