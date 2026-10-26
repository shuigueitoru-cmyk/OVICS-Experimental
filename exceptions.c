/* The boot entry installs a private flat GDT. Timer initialization installs IRQ gates after this exception table is ready. */
struct idt_gate { unsigned short low, selector; unsigned char zero, flags; unsigned short high; } __attribute__((packed));
struct table_pointer { unsigned short limit; unsigned int base; } __attribute__((packed));
static struct idt_gate idt[256] __attribute__((aligned(16)));
extern unsigned int exception_stubs[32];
extern void isr_default(void);
extern void exception_stop(void) __attribute__((noreturn));
struct fault_record { unsigned int vector,address,error,eip,halted; };
volatile struct fault_record vm_fault;
volatile unsigned int vm_fault_probe_physical, vm_fault_probe_readback;
void exceptions_initialize(void) {
    for(unsigned int i=0;i<256;i++) {
        unsigned int a=i<32?exception_stubs[i]:(unsigned int)isr_default;
        idt[i].low=(unsigned short)a;idt[i].high=(unsigned short)(a>>16);
        idt[i].selector=8;idt[i].zero=0;idt[i].flags=0x8e;
    }
    struct table_pointer p={sizeof(idt)-1,(unsigned int)idt};
    __asm__ volatile("lidt %0"::"m"(p):"memory");
}
void exception_handler(unsigned int vector,unsigned int error,unsigned int address,unsigned int eip) __attribute__((noreturn));
void exception_handler(unsigned int vector,unsigned int error,unsigned int address,unsigned int eip) {
    vm_fault.vector=vector;vm_fault.address=address;vm_fault.error=error;vm_fault.eip=eip;
    gfx_fault_console();
    clear_screen();
    if(vector==14) {
        vm_stats.fault_count++;
        print("OVICS PAGE FAULT\n\nAddress: 0x");print_hex(address,8);
        print("\nError Code: 0x");print_hex(error,8);
        print("\nReason: ");print(error&1?"Protection violation":"Not Present");
        print("\nAccess: ");print(error&2?"Write":"Read");
        print("\nPrivilege: ");print(error&4?"User":"Supervisor");
        print("\nReserved-bit violation: ");print(error&8?"Yes":"No");
        print("\nInstruction fetch flag: ");print(error&16?"Yes":"No");
    } else {
        print("OVICS CPU EXCEPTION\nVector: ");print_number(vector);
        print("\nError Code: 0x");print_hex(error,8);
    }
    print("\nEIP: 0x");print_hex(eip,8);
    print("\nFatal exception. CPU halted safely.\n");
    vm_fault.halted=1;exception_stop();
}
