BITS 32
MULTIBOOT_MAGIC equ 0x1BADB002
MULTIBOOT_FLAGS equ 3
section .multiboot
align 4
 dd MULTIBOOT_MAGIC, MULTIBOOT_FLAGS, -(MULTIBOOT_MAGIC+MULTIBOOT_FLAGS)
section .rodata
align 8
global gdt_start, gdt_end
gdt_start:
 dq 0
 ; Flat code/data descriptors, accessed bits already set.
 dq 0x00CF9B000000FFFF
 dq 0x00CF93000000FFFF
gdt_end:
gdt_pointer:
 dw gdt_end-gdt_start-1
 dd gdt_start
section .bss
align 16
global stack_bottom, stack_top, exception_stack_bottom, exception_stack_top
global irq1_wake_count, irq12_wake_count
irq1_wake_count: resd 1
irq12_wake_count: resd 1
alignb 16
stack_bottom: resb 16384
stack_top:
exception_stack_bottom: resb 16384
exception_stack_top:
section .text
global _start
extern kernel_main, exception_handler, timer_irq, ps2_irq_capture
_start:
 cli
 cld
 mov esi, eax
 mov edi, ebx
 lgdt [gdt_pointer]
 jmp 0x08:.flat
.flat:
 mov ax, 0x10
 mov ds, ax
 mov es, ax
 mov fs, ax
 mov gs, ax
 mov ss, ax
 mov esp, stack_top
 sub esp, 8
 push edi
 push esi
 call kernel_main
 add esp, 16
 jmp exception_stop
%macro ISR 1
global isr%1
isr%1:
%if %1 != 8 && %1 != 10 && %1 != 11 && %1 != 12 && %1 != 13 && %1 != 14 && %1 != 17 && %1 != 21 && %1 != 29 && %1 != 30
 push dword 0
%endif
 push dword %1
 jmp exception_common
%endmacro
%assign n 0
%rep 32
 ISR n
 %assign n n+1
%endrep
; Interrupt gate clears IF. Save every GPR/segment and preserve the interrupted
; DF via IRETD; align the temporary C call stack, restoring the exact old ESP.
global irq0_stub
irq0_stub:
 pushad
 push ds
 push es
 push fs
 push gs
 mov ax, 0x10
 mov ds, ax
 mov es, ax
 cld
 mov ebx, esp
 and esp, -16
 call timer_irq
 mov esp, ebx
 pop gs
 pop fs
 pop es
 pop ds
 popad
 iretd
; Bounded raw PS/2 input capture, never GUI or network work.
%macro PS2_IRQ 2
global %1
%1:
 pushad
 push ds
 push es
 push fs
 push gs
 mov ax, 0x10
 mov ds, ax
 mov es, ax
 cld
 mov ebx, esp
 and esp, -16
 call ps2_irq_capture
 mov esp, ebx
 inc dword [%2]
 mov al, 0x20
%ifidni %1, irq12_stub
 out 0xa0, al
%endif
 out 0x20, al
 pop gs
 pop fs
 pop es
 pop ds
 popad
 iretd
%endmacro
PS2_IRQ irq1_stub, irq1_wake_count
PS2_IRQ irq12_stub, irq12_wake_count
global isr_default
isr_default:
 push dword 0
 push dword 255
exception_common:
 cli
 cld
 mov eax, [esp]       ; vector
 mov ebx, [esp+4]     ; CPU error code, or synthetic zero for no-error exceptions
 mov ecx, [esp+8]     ; fault EIP
 mov edx, cr2         ; save before diagnostic code can fault
 mov esp, exception_stack_top
 push ecx
 push edx
 push ebx
 push eax
 call exception_handler
global exception_stop, exception_halted
exception_stop:
 cli
exception_halted:
 hlt
 jmp exception_halted
section .rodata
align 4
global exception_stubs
exception_stubs:
%assign n 0
%rep 32
 dd isr%+n
 %assign n n+1
%endrep
section .note.GNU-stack noalloc noexec nowrite progbits
