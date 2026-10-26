/* RTL8139 C-mode, polling only. Included after kernel PCI/print helpers. */
#define RX_RING_SIZE 8192U
#define FRAME_LIMIT 1518U
static volatile unsigned char rx_ring[RX_RING_SIZE + 16 + 2048]
    __attribute__((aligned(256)));
static unsigned char tx_buffers[4][2048] __attribute__((aligned(256)));
static unsigned char last_frame[FRAME_LIMIT];
static unsigned int last_frame_length;
static unsigned int rx_offset, tx_slot;
static unsigned int tx_packets, tx_bytes, tx_errors;
static unsigned int rx_packets, rx_bytes, rx_errors;
static unsigned short network_port;
static unsigned int network_bus, network_slot, network_function;
static unsigned char network_mac[6];
static int network_ready;
static const char *network_error = "Not initialized.";
extern unsigned char kernel_start[], kernel_end[];

static unsigned short inw(unsigned short port)
{
    unsigned short value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static void outw(unsigned short port, unsigned short value)
{
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}
static void dma_barrier(void) { __asm__ volatile ("" : : : "memory"); }

static int network_find(void)
{
    for (unsigned int bus = 0; bus < 256; bus++) {
        for (unsigned int slot = 0; slot < 32; slot++) {
            if ((pci_read(bus, slot, 0, 0) & 0xFFFF) == 0xFFFF) continue;
            int functions = (pci_read(bus, slot, 0, 0x0C) & 0x00800000) ? 8 : 1;
            for (int f = 0; f < functions; f++) {
                if (pci_read(bus, slot, (unsigned int)f, 0) == 0x813910ECU) {
                    network_bus = bus; network_slot = slot;
                    network_function = (unsigned int)f;
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* RTL8139 bus mastering always receives physical addresses. Check every
   page, identity/contiguity and supervisor write access, not just the first. */
static int network_dma_physical(unsigned int va,unsigned int bytes,unsigned int *result) {
    if(!bytes || (va&255) || va>0xffffffffu-(bytes-1))return 0;
    unsigned int physical,flags;
    if(!vm_translate(va,&physical,&flags) || physical!=va || !(flags&VM_WRITE) || (flags&VM_USER))return 0;
    for(unsigned int offset=0;offset<bytes;) {
        unsigned int pa,f;
        if(!vm_translate(va+offset,&pa,&f) || pa!=physical+offset || !(f&VM_WRITE) || (f&VM_USER))return 0;
        unsigned int step=4096-((va+offset)&4095);
        if(step>=bytes-offset)break;offset+=step;
    }
    *result=physical;return 1;
}
static int network_initialize(void)
{
    network_ready = 0;
    if (!network_find()) { network_error = "RTL8139 not found."; return 0; }
    unsigned int bar = pci_read(network_bus, network_slot, network_function, 0x10);
    unsigned int base = bar & ~3U;
    if (!(bar & 1) || !base || base > 0xFF00U) {
        network_error = "Invalid RTL8139 I/O BAR."; return 0;
    }
    network_port = (unsigned short)base;
    unsigned int rx_physical=0,tx_physical[4];
    if ((unsigned int)rx_ring < (unsigned int)kernel_start ||
        (unsigned int)rx_ring+sizeof(rx_ring)>(unsigned int)kernel_end ||
        (unsigned int)tx_buffers < (unsigned int)kernel_start ||
        (unsigned int)tx_buffers+sizeof(tx_buffers)>(unsigned int)kernel_end ||
        !network_dma_physical((unsigned int)rx_ring,sizeof(rx_ring),&rx_physical)) {
        network_error="Unsupported DMA memory layout.";return 0;
    }
    for(unsigned int i=0;i<4;i++)if(!network_dma_physical((unsigned int)tx_buffers[i],sizeof(tx_buffers[i]),&tx_physical[i])) {
        network_error="Unsupported TX DMA memory layout.";return 0;
    }
    unsigned short command = (unsigned short)pci_read(network_bus, network_slot,
                                                       network_function, 4);
    outl(0xCF8, 0x80000000U | (network_bus << 16) | (network_slot << 11) |
                (network_function << 8) | 4);
    /* Word write preserves status bits (which have write-one-to-clear semantics). */
    outw(0xCFC, command | 5); /* I/O decoding + bus mastering */
    if ((pci_read(network_bus, network_slot, network_function, 4) & 5) != 5) {
        network_error = "PCI bus mastering could not be enabled."; return 0;
    }
    outb(network_port + 0x52, 0); /* Exit low-power mode. */
    outb(network_port + 0x37, 0x10); /* Reset, with bounded wait. */
    unsigned int wait;
    for (wait = 0; wait < 100000; wait++)
        if (!(inb(network_port + 0x37) & 0x10)) break;
    if (wait == 100000) { network_error = "RTL8139 reset timeout."; return 0; }
    unsigned int low = inl(network_port), high = inl(network_port + 4);
    for (int i = 0; i < 6; i++)
        network_mac[i] = (unsigned char)(i < 4 ? low >> (i * 8) : high >> ((i - 4) * 8));
    for (unsigned int i = 0; i < sizeof(rx_ring); i++) rx_ring[i] = 0;
    rx_offset = 0; tx_slot = 0;
    dma_barrier();
    outl(network_port + 0x30, rx_physical);
    outw(network_port + 0x38, 0xFFF0); /* CAPR = consumer offset - 16 */
    outw(network_port + 0x3C, 0); /* No IRQ handling; poll instead. */
    outw(network_port + 0x3E, 0xFFFF);
    outb(network_port + 0x37, 0x0C); /* Enable receiver and transmitter. */
    /* 8KiB wrapping ring, own MAC + broadcast, maximum FIFO/DMA thresholds. */
    outl(network_port + 0x44, (7U << 13) | (7U << 8) | 0x0A);
    outl(network_port + 0x40, (3U << 24) | (7U << 8));
    for (int i = 0; i < 4; i++)
        outl(network_port + 0x20 + i * 4, tx_physical[i]);
    network_ready = 1;
    network_error = "None";
    return 1;
}

static unsigned short ring_word(unsigned int offset)
{
    return (unsigned short)(rx_ring[offset % RX_RING_SIZE] |
                         ((unsigned short)rx_ring[(offset + 1) % RX_RING_SIZE] << 8));
}

static void network_poll(void)
{
    tcp_tick(); /* Timer sends only; never recursively polls RX. */
    if (!network_ready) return;
    unsigned short interrupt = inw(network_port + 0x3E);
    if (interrupt & 0x8050) { /* PCI system error / ring overflow / FIFO overflow */
        rx_errors++;
        network_initialize(); /* Reset DMA pointers, preserve software counters. */
        if (network_ready) network_error = "Receive overflow/system error; controller reset.";
        return;
    }
    for (int budget = 0; budget < 32 && !(inb(network_port + 0x37) & 1); budget++) {
        dma_barrier();
        unsigned short status = ring_word(rx_offset);
        unsigned short length = ring_word(rx_offset + 2); /* Includes four CRC bytes. */
        if (length == 0xFFF0) break; /* DMA packet is not complete yet. */
        if (length < 18 || length > FRAME_LIMIT + 4) {
            rx_errors++;
            network_initialize();
            if (network_ready) network_error = "Invalid receive length; controller reset.";
            return;
        }
        if ((status & 1) && !(status & 0x3E)) {
            last_frame_length = length - 4;
            for (unsigned int i = 0; i < last_frame_length; i++)
                last_frame[i] = rx_ring[(rx_offset + 4 + i) % RX_RING_SIZE];
            rx_packets++; rx_bytes += last_frame_length;
        } else {
            rx_errors++;
            network_error = "Receive frame rejected (CRC/alignment/runt/status error).";
        }
        rx_offset = ((rx_offset + length + 4 + 3) & ~3U) % RX_RING_SIZE;
        dma_barrier();
        outw(network_port + 0x38, (unsigned short)(rx_offset - 16));
        if ((status & 1) && !(status & 0x3E)) protocol_receive(last_frame, last_frame_length);
    }
    /* Do not clear TX events here; sender checks its descriptor independently. */
    outw(network_port + 0x3E, interrupt & 3);
}

/* Copy into a dedicated DMA descriptor; never poll RX recursively here. */
static int network_transmit(const unsigned char *data, unsigned int length)
{
    if (!network_ready || length < 14 || length > FRAME_LIMIT) { tx_errors++; return 0; }
    unsigned int slot = tx_slot;
    if (!(inl(network_port + 0x10 + slot * 4) & (1U << 13))) {
        tx_errors++; network_error = "Transmit descriptor busy."; return 0;
    }
    unsigned int size = length < 60 ? 60 : length;
    for (unsigned int i = 0; i < size; i++) tx_buffers[slot][i] = i < length ? data[i] : 0;
    dma_barrier();
    outl(network_port + 0x10 + slot * 4, (32U << 16) | size);
    unsigned int status = 0;
    for (unsigned int wait = 0; wait < 1000000; wait++) {
        status = inl(network_port + 0x10 + slot * 4);
        if (status & ((1U << 15) | (1U << 14) | (1U << 30) | (1U << 31))) break;
    }
    outw(network_port + 0x3E, 0x0C);
    if ((status & (1U << 15)) && !(status & ((1U << 14) | (1U << 30) | (1U << 31)))) {
        tx_packets++; tx_bytes += size; tx_slot = (slot + 1) % 4; return 1;
    }
    tx_errors++; network_error = "Transmit timeout or hardware error; controller stopped.";
    outb(network_port + 0x37, 0); network_ready = 0; return 0;
}
static void network_send(void)
{
    unsigned char frame[60] = {0};
    for (int i = 0; i < 6; i++) { frame[i] = 0xFF; frame[6+i] = network_mac[i]; }
    frame[12] = 0x88; frame[13] = 0xB5;
    const char *message = "OVICS 0.0.6 Ethernet test";
    for (int i = 0; message[i]; i++) frame[14+i] = (unsigned char)message[i];
    if (network_transmit(frame, 60)) print("Sent: 60-byte Ethernet test frame (hardware completion).\n");
    else { print("Send failed: "); print(network_error); print("\n"); }
}

static void network_statistics(void)
{
    network_poll();
    print("Network: "); print(network_ready ? "READY\n" : "UNAVAILABLE\n");
    print("TX packets: "); print_number(tx_packets); print("\nTX bytes: ");
    print_number(tx_bytes); print("\nTX errors: "); print_number(tx_errors);
    print("\nRX packets: "); print_number(rx_packets); print("\nRX bytes: ");
    print_number(rx_bytes); print("\nRX errors: "); print_number(rx_errors);
    print("\nLast error: "); print(network_error); print("\n");
}

static void network_receive(void)
{
    network_poll();
    if (!network_ready) { print("Receive unavailable: "); print(network_error); print("\n"); return; }
    if (!last_frame_length) { print("No Ethernet frame received.\n"); return; }
    print("Last received Ethernet frame: "); print_number(last_frame_length); print(" bytes\n");
    print("Destination: ");
    for (int i = 0; i < 6; i++) { if (i) print(":"); print_hex(last_frame[i], 2); }
    print("\nSource: ");
    for (int i = 6; i < 12; i++) { if (i != 6) print(":"); print_hex(last_frame[i], 2); }
    print("\nEtherType: 0x"); print_hex((last_frame[12] << 8) | last_frame[13], 4);
    print("\nPayload preview (first 64 bytes):\n");
    for (unsigned int i = 14; i < last_frame_length && i < 78; i++)
        putchar(last_frame[i] >= 32 && last_frame[i] <= 126 ? (char)last_frame[i] : '.');
    print("\n");
}
