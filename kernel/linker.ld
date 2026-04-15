/* kernel/linker.ld */
ENTRY(start)

PHDRS
{
    boot    PT_LOAD FLAGS(5); /* R+X */
    text    PT_LOAD FLAGS(5); /* R+X */
    rodata  PT_LOAD FLAGS(4); /* R   */
    data    PT_LOAD FLAGS(6); /* R+W */
}

SECTIONS
{
    . = 2M;

    .boot : ALIGN(4096)
    {
        KEEP(*(.multiboot_header))
    } :boot

    .text : ALIGN(4096)
    {
        __text_start = .;
        *(.text .text.*)
        KEEP(*(.text.isr))
        __text_end = .;
    } :text

    .rodata : ALIGN(4096)
    {
        __rodata_start = .;
        *(.rodata .rodata.*)
        __rodata_end = .;
    } :rodata

    .data : ALIGN(4096)
    {
        __data_start = .;
        *(.data .data.*)
        
        . = ALIGN(8);
        __init_array_start = .;
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = .;
        
        . = ALIGN(8);
        __fini_array_start = .;
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array))
        __fini_array_end = .;
        
        __data_end = .;
    } :data

    .bss : ALIGN(4096)
    {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    } :data

    .stack (NOLOAD) : ALIGN(4096)
    {
        __stack_start = .;
        *(.bootstrap_stack)
        __stack_end = .;
    } :data

    . = ALIGN(4096);
    end = .;

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame)
        *(.note.gnu.build-id)
    }
}