; system/disk/gpt_asm.asm
bits 64

global gpt_create_partition
global gpt_scan_partitions
global gpt_delete_partition

extern rust_gpt_create_partition
extern rust_gpt_scan_partitions
extern rust_gpt_delete_partition

section .text

gpt_create_partition:
    push rbp
    mov rbp, rsp
    
    and rsp, -16
    
    call rust_gpt_create_partition
    
    mov rsp, rbp
    pop rbp
    ret

gpt_scan_partitions:
    push rbp
    mov rbp, rsp
    and rsp, -16
    
    call rust_gpt_scan_partitions
    
    mov rsp, rbp
    pop rbp
    ret

gpt_delete_partition:
    push rbp
    mov rbp, rsp
    and rsp, -16
    
    call rust_gpt_delete_partition
    
    mov rsp, rbp
    pop rbp
    ret

section .note.GNU-stack noalloc noexec nowrite progbits