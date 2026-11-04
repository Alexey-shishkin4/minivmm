/*
 * Linker script for flat-binary guest payloads.
 * The VMM loads the binary at PAYLOAD_BASE and starts execution at its first byte.
 * TODO: keep in sync with the load address used by src/boot/raw_loader.c.
 */

ENTRY(_start)

SECTIONS
{
    . = 0x1000;
    .text : { *(.text.entry) *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) *(COMMON) }
    /DISCARD/ : { *(.note*) *(.comment) *(.eh_frame*) }
}
