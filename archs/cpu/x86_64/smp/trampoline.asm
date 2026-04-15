; archs/cpu/x86_64/smp/trampoline.asm
bits 16

global trampoline_start
global trampoline_end
global ap_startup_ptr
global p4_table_ptr
global stack_ptr_ptr

trampoline_start:
    cli
    cld

    mov ax, 0x0800
    mov ds, ax
    mov es, ax
    xor ax, ax
    mov ss, ax
    mov sp, 0x7000

    ; 16-bit modundayken doğrudan 64-bit destekli GDT'yi yüklüyoruz.
    lgdt[gdtr_limit - trampoline_start]

    ; CR4'te PAE (bit 5), PGE (bit 7), OSFXSR (bit 9), OSXMMEXCPT (bit 10) aktifleştirilir
    mov eax, 10100000b
    or  eax, (1 << 9) | (1 << 10)
    mov cr4, eax

    ; 64-bit sayfa tablosunu CR3'e yükle
    ; DS = 0x0800 (Fiziksel 0x8000) olduğu için ofset (0x8000 ilavesi olmadan) doğrudan okunur
    mov eax, [p4_table_ptr - trampoline_start]
    mov cr3, eax

    ; EFER MSR'ında Long Mode (LME) ve No-Execute (NXE) açılır
    mov ecx, 0xC0000080
    rdmsr
    or  eax, (1 << 8) | (1 << 11)
    wrmsr

    ; Doğrudan 16-to-64 Bit Geçişi!
    ; Protected Mode (PE) ve Paging (PG) AYNI ANDA aktif edilir. İşlemci anında Compatibility Mode'a düşer.
    mov eax, cr0
    or  eax, 0x80000001
    mov cr0, eax

    ; Vakit kaybetmeden 64-bit kod segmentine (0x08) fırlat (Direct Far Jump)
    jmp dword 0x08:(trampoline64 - trampoline_start + 0x8000)

bits 64
trampoline64:
    ; AP Çekirdeği şu an NATIVE 64-bit Long Mode'da!
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    mov rax, cr0
    and eax, ~(1 << 2)
    or  eax, (1 << 1) | (1 << 5)
    mov cr0, rax

    fninit

    sub  rsp, 16
    and  rsp, -16
    mov  dword [rsp], 0x1F80
    ldmxcsr [rsp]

    mov  eax, 1
    xor  ecx, ecx
    cpuid
    mov  ebx, ecx

    test ecx, (1 << 26)
    jz   .ap_fpu_done

    mov  rax, cr4
    or   eax, (1 << 18)
    mov  cr4, rax

    mov  eax, 0xD
    xor  ecx, ecx
    cpuid
    test eax, eax
    jz   .ap_fpu_done

    test ebx, (1 << 28)
    jz   .ap_xsave_no_avx

    xor  ecx, ecx
    xgetbv
    or   eax, (1 << 0) | (1 << 1) | (1 << 2)
    xsetbv

    xor  ecx, ecx
    xgetbv
    test eax, (1 << 2)
    jz   .ap_xsave_no_avx
    jmp  .ap_fpu_done

.ap_xsave_no_avx:
    xor  ecx, ecx
    xgetbv
    or   eax, (1 << 0) | (1 << 1)
    xsetbv

.ap_fpu_done:
    ; 64-bit modda DS=0 olduğu için mutlak lineer adresleme (0x8000+) kullanılır
    mov  rax, 0x8000
    add  rax, (stack_ptr_ptr - trampoline_start)
    mov  rsp, [rax]

    mov  rax, 0x8000
    add  rax, (ap_startup_ptr - trampoline_start)
    mov  rax, [rax]

    call rax
    cli
.hang:
    hlt
    jmp .hang

align 16
gdtr_limit:
    dw gdt_end - gdt_start - 1
    dd 0x8000 + (gdt_start - trampoline_start)

align 16
gdt_start:
    dq 0x0000000000000000
    dq 0x00AF9A000000FFFF ; 0x08 - Native 64-bit Code Segment (L=1, P=1, DPL=0)
    dq 0x00AF92000000FFFF ; 0x10 - Native 64-bit Data Segment (P=1, DPL=0)
gdt_end:

align 8
p4_table_ptr:   dq 0
ap_startup_ptr: dq 0
stack_ptr_ptr:  dq 0

trampoline_end:

section .note.GNU-stack noalloc noexec nowrite progbits