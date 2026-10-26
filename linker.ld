ENTRY(_start)
PHDRS {
 boot PT_LOAD FLAGS(4);
 code PT_LOAD FLAGS(5);
 constants PT_LOAD FLAGS(4);
 data PT_LOAD FLAGS(6);
}
SECTIONS {
 . = 1M;
 kernel_start = .;
 .multiboot : { KEEP(*(.multiboot)) } :boot
 . = ALIGN(4096);
 kernel_text_start = .;
 .text : { *(.text*) } :code
 . = ALIGN(4096);
 .rodata : { *(.rodata*) *(.eh_frame*) } :constants
 . = ALIGN(4096);
 kernel_ro_end = .;
 .data : { *(.data*) } :data
 .bss : { *(COMMON) *(.bss*) } :data
 . = ALIGN(4096);
 kernel_end = .;
 /DISCARD/ : { *(.comment) *(.note*) }
}
