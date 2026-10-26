static volatile unsigned short *screen =
    (unsigned short *)0xB8000;

static int cursor = 0;
static void (*console_sink)(char);
static void (*wait_service)(void);

#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25
#define SCREEN_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT)


/* -------------------------------------------------
   VGA SCREEN
   ------------------------------------------------- */

static void scroll_screen(void)
{
    /* Move every line up by one */
    for (int i = 0;
         i < SCREEN_WIDTH * (SCREEN_HEIGHT - 1);
         i++) {

        screen[i] = screen[i + SCREEN_WIDTH];
    }

    /* Clear last line */
    for (int i = SCREEN_WIDTH * (SCREEN_HEIGHT - 1);
         i < SCREEN_SIZE;
         i++) {

        screen[i] = (0x0F << 8) | ' ';
    }

    cursor -= SCREEN_WIDTH;

    if (cursor < 0) {
        cursor = 0;
    }
}


static void check_scroll(void)
{
    while (cursor >= SCREEN_SIZE) {
        scroll_screen();
    }
}


/* Forward declaration */
static void update_cursor(void);


static void putchar(char c)
{
    if(console_sink){console_sink(c);return;}
    if (c == '\n') {

        cursor =
            ((cursor / SCREEN_WIDTH) + 1)
            * SCREEN_WIDTH;

        check_scroll();
        update_cursor();

        return;
    }

    screen[cursor++] =
        (0x0F << 8) | c;

    check_scroll();
    update_cursor();
}


static void print(const char *s)
{
    while (*s) {
        putchar(*s++);
    }
}


static void clear_screen(void)
{
    for (int i = 0;
         i < SCREEN_SIZE;
         i++) {

        screen[i] =
            (0x0F << 8) | ' ';
    }

    cursor = 0;
    update_cursor();
}


/* -------------------------------------------------
   PORT I/O
   ------------------------------------------------- */

static unsigned char inb(unsigned short port)
{
    unsigned char value;

    __asm__ volatile (
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}


static void outb(
    unsigned short port,
    unsigned char value)
{
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(value),
          "Nd"(port)
    );
}


/* -------------------------------------------------
   VGA HARDWARE CURSOR
   ------------------------------------------------- */

static void update_cursor(void)
{
    unsigned short pos =
        (unsigned short)cursor;

    /* Low byte */
    outb(0x3D4, 0x0F);
    outb(
        0x3D5,
        (unsigned char)(pos & 0xFF)
    );

    /* High byte */
    outb(0x3D4, 0x0E);
    outb(
        0x3D5,
        (unsigned char)((pos >> 8) & 0xFF)
    );
}


/* -------------------------------------------------
   STRING FUNCTIONS
   ------------------------------------------------- */

static int strcmp(
    const char *a,
    const char *b)
{
    while (*a && (*a == *b)) {
        a++;
        b++;
    }

    return
        *(unsigned char *)a -
        *(unsigned char *)b;
}


static int starts_with(
    const char *text,
    const char *prefix)
{
    while (*prefix) {

        if (*text != *prefix) {
            return 0;
        }

        text++;
        prefix++;
    }

    return 1;
}


/* -------------------------------------------------
   KEYBOARD
   ------------------------------------------------- */

static char keyboard_map[128] = {

    0, 27,

    '1','2','3','4','5',
    '6','7','8','9','0',
    '-','=', '\b',

    '\t',

    'q','w','e','r','t',
    'y','u','i','o','p',
    '[',']','\n',0,

    'a','s','d','f','g',
    'h','j','k','l',
    ';','\'','`',

    0,'\\',

    'z','x','c','v','b',
    'n','m',
    ',','.','/',

    0,'*',0,' '
};


static char shifted_character(char c)
{
    if (c >= 'a' && c <= 'z') return (char)(c - 'a' + 'A');
    const char *normal = "1234567890-=[];\\,./`";
    const char *shifted = "!@#$%^&*()_+{}:|<>?~";
    for (int i = 0; normal[i]; i++) if (c == normal[i]) return shifted[i];
    if (c == 39) return 34;
    return c;
}

/* -------------------------------------------------
   COMMANDS
   ------------------------------------------------- */

#define RAM_FILE_COUNT 16
#define RAM_NAME_SIZE 32
#define RAM_CONTENT_SIZE 256

struct ram_file {
    int used;
    char name[RAM_NAME_SIZE];
    char content[RAM_CONTENT_SIZE];
};

static struct ram_file ram_files[RAM_FILE_COUNT];

static void init_ram_files(void)
{
    for (int i = 0; i < RAM_FILE_COUNT; i++) {
        ram_files[i].used = 0;
        ram_files[i].name[0] = 0;
        ram_files[i].content[0] = 0;
    }
}

/* Consume one filename token; leave the original command unchanged. */
static const char *read_filename(const char *args, char *name)
{
    int length = 0;
    while (*args == ' ') args++;
    while (*args && *args != ' ') {
        if (length >= RAM_NAME_SIZE - 1 ||
            (unsigned char)*args < 33 || *args == '/' || *args == '\\') {
            print("Invalid filename (1-31 characters, no spaces or paths).\n");
            return 0;
        }
        name[length++] = *args++;
    }
    name[length] = 0;
    if (!length) {
        print("Filename required.\n");
        return 0;
    }
    while (*args == ' ') args++;
    return args;
}

static int find_ram_file(const char *name)
{
    for (int i = 0; i < RAM_FILE_COUNT; i++)
        if (ram_files[i].used && strcmp(ram_files[i].name, name) == 0)
            return i;
    return -1;
}

static void copy_text(char *destination, const char *source)
{
    while ((*destination++ = *source++) != 0) { }
}

static void file_command(const char *operation, const char *args)
{
    char name[RAM_NAME_SIZE];
    const char *text = read_filename(args, name);
    if (!text) return;
    if (strcmp(operation, "write") != 0 && *text) {
        print("Expected one filename.\n");
        return;
    }
    int index = find_ram_file(name);
    if (strcmp(operation, "touch") == 0) {
        if (index >= 0) {
            print("File already exists.\n");
            return;
        }
        for (int i = 0; i < RAM_FILE_COUNT; i++) {
            if (!ram_files[i].used) {
                copy_text(ram_files[i].name, name);
                ram_files[i].content[0] = 0;
                ram_files[i].used = 1;
                print("File created.\n");
                return;
            }
        }
        print("RAM file storage full (16 files).\n");
        return;
    }
    if (index < 0) {
        print("File not found.\n");
        return;
    }
    if (strcmp(operation, "write") == 0) {
        int length = 0;
        while (text[length]) {
            if (++length >= RAM_CONTENT_SIZE) {
                print("Content too long (maximum 255 characters).\n");
                return;
            }
        }
        copy_text(ram_files[index].content, text);
        print("File saved.\n");
    } else if (strcmp(operation, "cat") == 0) {
        print(ram_files[index].content);
        print("\n");
    } else if (strcmp(operation, "rm") == 0) {
        ram_files[index].used = 0;
        ram_files[index].name[0] = 0;
        ram_files[index].content[0] = 0;
        print("File deleted.\n");
    }
}

/* End-of-text editor: work on a draft, commit only with Ctrl+S. */
static int editor_active = 0;
static int editor_index = -1;
static int editor_length = 0;
static int editor_dirty = 0;
static char editor_name[RAM_NAME_SIZE];
static char editor_text[RAM_CONTENT_SIZE];
static const char *editor_status;

static void editor_draw(void)
{
    clear_screen();
    print("OVICS EDIT - ");
    print(editor_name);
    if (editor_dirty) print(" *");

    int row = 0, column = 0;
    for (int i = 0; i < editor_length; i++) {
        if (editor_text[i] == '\n') { row++; column = 0; }
        else if (++column == SCREEN_WIDTH) { row++; column = 0; }
    }
    /* Reserve rows 0/1 for the header and row 24 for status. */
    int first_row = row > 21 ? row - 21 : 0;
    int end_column = column;
    int end_row = row;
    row = 0; column = 0;
    for (int i = 0; i < editor_length; i++) {
        char c = editor_text[i];
        if (c == '\n') { row++; column = 0; continue; }
        if (row >= first_row && row < first_row + 22)
            screen[(row - first_row + 2) * SCREEN_WIDTH + column] =
                (0x0F << 8) | (unsigned char)c;
        if (++column == SCREEN_WIDTH) { row++; column = 0; }
    }
    cursor = 24 * SCREEN_WIDTH;
    print(editor_status);
    cursor = (end_row - first_row + 2) * SCREEN_WIDTH + end_column;
    update_cursor();
}

static void start_editor(const char *args)
{
    const char *rest = read_filename(args, editor_name);
    if (!rest) return;
    if (*rest) { print("Expected one filename.\n"); return; }
    editor_index = find_ram_file(editor_name);
    if (editor_index < 0) {
        int free_slot = 0;
        for (int i = 0; i < RAM_FILE_COUNT; i++)
            if (!ram_files[i].used) free_slot = 1;
        if (!free_slot) { print("RAM file storage full (16 files).\n"); return; }
        editor_text[0] = 0;
    } else copy_text(editor_text, ram_files[editor_index].content);
    editor_length = 0;
    while (editor_text[editor_length]) editor_length++;
    editor_dirty = 0;
    editor_active = 1;
    editor_status = "Ctrl+S: save | Esc: exit (discard unsaved changes) | Limit: 255";
    editor_draw();
}

static void editor_key(char c, int control)
{
    if (c == 27) {
        editor_active = 0;
        clear_screen();
        print(editor_dirty ? "Editor closed. Unsaved changes discarded.\n" :
                            "Editor closed.\n");
        print("OVICS> ");
        return;
    }
    if (control) {
        if (c == 's') {
            if (editor_index < 0) {
                for (int i = 0; i < RAM_FILE_COUNT; i++)
                    if (!ram_files[i].used) { editor_index = i; break; }
            }
            if (editor_index < 0) {
                editor_status = "Save failed: RAM file storage full.";
            } else {
                copy_text(ram_files[editor_index].name, editor_name);
                copy_text(ram_files[editor_index].content, editor_text);
                ram_files[editor_index].used = 1;
                editor_dirty = 0;
                editor_status = "Saved to RAM. Ctrl+S: save | Esc: exit | Limit: 255";
            }
            editor_draw();
        }
        return;
    }
    if (c == '\b') {
        if (editor_length) editor_text[--editor_length] = 0;
        else return;
    } else if (c == '\n' || (unsigned char)c >= 32) {
        if (editor_length >= RAM_CONTENT_SIZE - 1) {
            editor_status = "Content full (255 characters). Backspace to make room.";
            editor_draw();
            return;
        }
        editor_text[editor_length++] = c;
        editor_text[editor_length] = 0;
    } else return;
    editor_dirty = 1;
    editor_status = "Unsaved. Ctrl+S: save | Esc: discard unsaved changes | Limit: 255";
    editor_draw();
}

/* PCI configuration mechanism 1 (legacy i386): no BAR probing/writes. */
static unsigned int inl(unsigned short port)
{
    unsigned int value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void outl(unsigned short port, unsigned int value)
{
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static unsigned int pci_read(unsigned int bus, unsigned int slot,
                             unsigned int function, unsigned int offset)
{
    outl(0xCF8, 0x80000000U | (bus << 16) | (slot << 11) |
                (function << 8) | (offset & 0xFC));
    return inl(0xCFC);
}

static void print_hex(unsigned int value, int digits)
{
    const char *hex = "0123456789ABCDEF";
    for (int i = digits - 1; i >= 0; i--)
        putchar(hex[(value >> (i * 4)) & 15]);
}

static void print_number(unsigned int value)
{
    char digits[10];
    int length = 0;
    do { digits[length++] = '0' + value % 10; value /= 10; } while (value);
    while (length) putchar(digits[--length]);
}

/* Freestanding compiler-generated aggregate initialization support. */
void *memset(void *destination, int value, unsigned int length)
{
    volatile unsigned char *p = (volatile unsigned char *)destination;
    for (unsigned int i = 0; i < length; i++) p[i] = (unsigned char)value;
    return destination;
}
static void protocol_receive(const unsigned char *frame, unsigned int length);
static void tcp_tick(void);
#include "memory.c"
#include "virtual_memory.h"
static void gfx_fault_console(void);
#include "exceptions.c"
#include "virtual_memory.c"
#include "timer.c"
extern unsigned char kernel_start[], kernel_end[];
#include "network.c"
#include "netconfig.h"
static void udp_receive_packet(unsigned int source, unsigned int destination, const unsigned char *packet, unsigned int length);
static void tcp_receive_packet(unsigned int source, unsigned int destination, const unsigned char *packet, unsigned int length);
static void ps2_poll(void);
#include "protocol.c"
#include "udp.c"
#include "dns.c"
#include "tcp.c"
#include "http.c"
#include "mouse.c"
static void cursor_graphics_begin(void);
static void cursor_graphics_end(void);
static void cursor_mode_leave(void);
static void wm_mode_leave(void);
#include "graphics.c"
#include "mouse_cursor.c"
#include "mouse_demo.c"
#include "window_manager.c"
#include "window_tests.c"
static void run_command(char *command);
#include "browser.h"
#include "desktop.c"
static void kernel_poll_keyboard(void);
#include "browser.c"
#include "desktop_tests.c"
#include "timer_tests.c"

static void netinfo(void)
{
    unsigned int count = 0;
    unsigned int card_bus = 0, card_slot = 0, card_function = 0;
    int found = 0;
    for (unsigned int bus = 0; bus < 256; bus++) {
        for (unsigned int slot = 0; slot < 32; slot++) {
            unsigned int id = pci_read(bus, slot, 0, 0);
            if ((id & 0xFFFF) == 0xFFFF) continue;
            int functions = (pci_read(bus, slot, 0, 0x0C) & 0x00800000) ? 8 : 1;
            for (int function = 0; function < functions; function++) {
                id = pci_read(bus, slot, (unsigned int)function, 0);
                if ((id & 0xFFFF) == 0xFFFF) continue;
                count++;
                if (!found && id == 0x813910ECU) {
                    card_bus = bus; card_slot = slot;
                    card_function = (unsigned int)function;
                    found = 1;
                }
            }
        }
    }
    print("PCI devices: "); print_number(count); print("\n");
    if (!found) {
        print("RTL8139 not found. Start QEMU with -nic user,model=rtl8139\n");
        return;
    }
    print("RTL8139 detected (10EC:8139)\nPCI: ");
    print_hex(card_bus, 2); print(":"); print_hex(card_slot, 2);
    print("."); print_hex(card_function, 1); print("\n");
    unsigned int bar = pci_read(card_bus, card_slot, card_function, 0x10);
    print("BAR0: 0x"); print_hex(bar, 8); print("\n");
    unsigned int base = bar & ~3U;
    /* Need the ID registers at base+0..7 inside the i386 port range. */
    if (!(bar & 1) || !base || base > 0xFFF8U) {
        print("Error: RTL8139 BAR0 is not a usable I/O address.\n");
        return;
    }
    if (!(pci_read(card_bus, card_slot, card_function, 4) & 1)) {
        print("Error: PCI I/O decoding is disabled.\n");
        return;
    }
    print("I/O base: 0x"); print_hex(base, 4); print("\n");
    /* IDR0..5: two aligned 32-bit reads; no reset or TX/RX setup. */
    unsigned int mac_low = inl((unsigned short)base);
    unsigned int mac_high = inl((unsigned short)(base + 4));
    print("MAC: ");
    for (int i = 0; i < 6; i++) {
        if (i) print(":");
        unsigned int part = i < 4 ? mac_low >> (i * 8) :
                                     mac_high >> ((i - 4) * 8);
        print_hex(part & 0xFF, 2);
    }
    print(network_ready ? "\nTX/RX: initialized (polling)\n" : "\nTX/RX: unavailable\n");
}

static void run_command(char *command)
{
    if (strcmp(command,"horizon")==0 || starts_with(command,"horizon ")) {
        browser_command(command[7]?command+8:"home:");
    } else if (strcmp(command, "timerinfo") == 0) {
        timer_print_info();
    } else if (strcmp(command, "uptime") == 0) {
        timer_print_uptime();
    } else if (strcmp(command, "timertest") == 0) {
        timer_test_command();
    } else if (strcmp(command, "desktop") == 0) {
        desktop_start();
    } else if (strcmp(command, "guiinfo") == 0) {
        gui_print_info();
    } else if (strcmp(command, "guitest") == 0) {
        gui_test_command();
    } else if (strcmp(command, "wminfo") == 0) {
        wm_print_info();
    } else if (strcmp(command, "wmtest") == 0) {
        wm_test_command();
    } else if (strcmp(command, "wmdemo") == 0) {
        wm_demo_start();
    } else if (strcmp(command, "mouseinfo") == 0) {
        mouse_print_info();
    } else if (strcmp(command, "mousetest") == 0) {
        mouse_test_command();
    } else if (strcmp(command, "mousedemo") == 0) {
        mouse_demo_start();
    } else if (strcmp(command, "gfxinfo") == 0) {
        gfx_print_info();
    } else if (strcmp(command, "gfxtest") == 0) {
        gfx_start(0);
    } else if (strcmp(command, "gfxdemo") == 0) {
        gfx_start(1);
    } else if (strcmp(command, "vmeminfo") == 0) {
        vm_print_info();
    } else if (strcmp(command, "vmemtest") == 0) {
        print(vm_selftest() ? "Virtual memory test PASS\n" : "Virtual memory test FAIL\n");
    } else if (strcmp(command, "meminfo") == 0) {
        print("Memory map: "); print(pm_stats.ready ? "ready\n" : "unavailable (allocation disabled)\n");
        print("Physical RAM (KiB, below 4GiB): "); print_number(pm_stats.ram_kib); print("\n");
        print("Available pages: "); print_number(pm_stats.available); print("\n");
        print("Used pages: "); print_number(pm_stats.used); print("\n");
        print("Free pages: "); print_number(pm_stats.free); print("\n");
        print("Invalid frees: "); print_number(pm_stats.errors); print("\n");
    } else if (strcmp(command, "memtest") == 0) {
        print(pm_selftest() ? "Memory test PASS\n" : "Memory test FAIL\n");
    } else if (strcmp(command, "help") == 0) {

        print("Commands:\n horizon [URL] - Native Horizon browser\n timerinfo / timertest / uptime - System timer\n desktop / guiinfo / guitest - Desktop environment (Esc returns)\n wminfo / wmtest / wmdemo - Window manager (Esc returns)\n mouseinfo / mousetest / mousedemo - PS/2 mouse (Esc returns)\n gfxinfo / gfxtest / gfxdemo - Graphics (Esc returns)\n vmeminfo - Paging statistics\n vmemtest - Safe mapping and ownership test\n meminfo - Physical page statistics\n memtest - Destructive test of free pages only\n");
        print(" httpget URL - Get HTTP text (up to 255 command characters)\n httpinfo - Last HTTP result\n");
        print(" tcpconnect IP PORT - Connect TCP\n tcpsend TEXT / tcprecv - TCP data\n tcpclose / tcpstat - Close / status\n");
        print(" udpsend IP PORT TEXT - Send UDP\n udprecv PORT - Wait for UDP\n nslookup NAME - Resolve IPv4\n");
        print(" ipconfig - Show IPv4 settings\n arp      - Show ARP cache\n ping IP  - Send ICMP Echo Requests\n");
        print(" help     - Show commands\n");
        print(" version  - Show OVICS version\n");
        print(" clear    - Clear screen\n");
        print(" echo     - Print text\n");
        print(" about    - About OVICS JUNK\n");
        print(" explore  - Explore junk tech\n");
        print(" project  - Show project status\n");
        print(" ls       - List RAM files\n");
        print(" touch NAME - Create RAM file\n");
        print(" write NAME TEXT - Replace file contents\n");
        print(" cat NAME - Show file contents\n");
        print(" rm NAME  - Delete RAM file\n");
        print(" edit NAME - Edit RAM file (Ctrl+S save, Esc exit)\n");
        print(" netinfo  - Detect RTL8139 and read MAC address\n");
        print(" network  - Network information (alias: netinfo)\n");
        print(" send     - Send test frame (alias: netsend)\n");
        print(" statistics - Network counters (alias: netstat)\n");
        print(" receive  - Show last received frame\n");
        print("RAM files disappear after restart.\n");
    }

    else if (
        strcmp(command, "version") == 0) {

        print("OVICS 0.2.2 Experimental\n");
    }

    else if (
        strcmp(command, "clear") == 0) {

        clear_screen();
    }

    else if (
        starts_with(command, "echo ")) {

        print(command + 5);
        print("\n");
    }

    else if (
        strcmp(command, "echo") == 0) {

        print("\n");
    }

    else if (
        strcmp(command, "about") == 0) {

        print("\n");
        print("OVICS JUNK\n");
        print("------------------------------\n");
        print("Exploring old computers,\n");
        print("junk devices and forgotten tech.\n");
        print("\n");
        print("Giving old hardware another chance.\n");
        print("\n");
    }

    else if (
        strcmp(command, "explore") == 0) {

        print("\n");
        print("EXPLORE\n");
        print("------------------------------\n");
        print("JUNK PC / RETRO PC / Linux\n");
        print("CUSTOM ROM / OLD DEVICE\n");
        print("\n");
    }

    else if (
        strcmp(command, "project") == 0) {

        print("\n");
        print("PROJECT STATUS\n");
        print("------------------------------\n");
        print("Self-made OS : OVICS\n");
        print("YouTube      : @OVICSJUNK\n");
        print("Status       : ONLINE\n");
        print("\n");
    }

    else if (strcmp(command, "ls") == 0) {
        int count = 0;
        for (int i = 0; i < RAM_FILE_COUNT; i++) {
            if (ram_files[i].used) {
                print(ram_files[i].name);
                print("\n");
                count++;
            }
        }
        if (!count) print("No files.\n");
    }
    else if (strcmp(command, "touch") == 0 || starts_with(command, "touch ")) {
        file_command("touch", command + 5);
    }
    else if (strcmp(command, "write") == 0 || starts_with(command, "write ")) {
        file_command("write", command + 5);
    }
    else if (strcmp(command, "cat") == 0 || starts_with(command, "cat ")) {
        file_command("cat", command + 3);
    }
    else if (strcmp(command, "rm") == 0 || starts_with(command, "rm ")) {
        file_command("rm", command + 2);
    }
    else if (strcmp(command, "edit") == 0 || starts_with(command, "edit ")) {
        start_editor(command + 4);
    }
    else if (strcmp(command, "netinfo") == 0 || strcmp(command, "network") == 0) {
        netinfo();
    }
    else if (strcmp(command, "netsend") == 0 || strcmp(command, "send") == 0) {
        network_send();
    }
    else if (strcmp(command, "netstat") == 0 || strcmp(command, "statistics") == 0) {
        network_statistics();
    }
    else if (strcmp(command, "receive") == 0) {
        network_receive();
    }
    else if (strcmp(command, "udpsend") == 0 || starts_with(command, "udpsend ")) { udp_send_command(command + 7); }
    else if (strcmp(command, "udprecv") == 0 || starts_with(command, "udprecv ")) { udp_receive_command(command + 7); }
    else if (strcmp(command, "nslookup") == 0 || starts_with(command, "nslookup ")) { dns_command(command + 8); }
    else if (strcmp(command, "httpget") == 0 || starts_with(command, "httpget ")) { http_get_command(command + 7); }
    else if (strcmp(command, "httpinfo") == 0) { http_information(); }
    else if (strcmp(command, "tcpconnect") == 0 || starts_with(command, "tcpconnect ")) { tcp_connect_command(command + 10); }
    else if (strcmp(command, "tcpsend") == 0 || starts_with(command, "tcpsend ")) { tcp_send_command(command + 7); }
    else if (strcmp(command, "tcprecv") == 0) { tcp_receive_command(); }
    else if (strcmp(command, "tcpclose") == 0) { tcp_close_command(); }
    else if (strcmp(command, "tcpstat") == 0) { tcp_statistics(); }
    else if (strcmp(command, "ipconfig") == 0) { ipconfig(); }
    else if (strcmp(command, "arp") == 0) { arp_display(); }
    else if (strcmp(command, "ping") == 0 || starts_with(command, "ping ")) { ping_command(command + 4); }
    else if (command[0] != 0) {

        print("Unknown command: ");
        print(command);
        print("\n");
    }
}


/* -------------------------------------------------
   KERNEL
   ------------------------------------------------- */

static char command[256];

static int command_pos = 0, command_overflow = 0;
static int shift_left = 0, shift_right = 0;
static int control_left = 0, control_right = 0;
static int extended = 0;
static void kernel_poll_keyboard(void) { do {
        /* Keyboard data available? */
        if(ps2_key_lost){shift_left=shift_right=control_left=control_right=extended=0;ps2_key_lost=0;}
        unsigned char scancode;
        if (ps2_key_read(&scancode)) {

            if (scancode == 0xE0) { extended = 1; continue; }
            if ((scancode & 0x7F) == 0x1D) {
                if (extended) control_right = !(scancode & 0x80);
                else control_left = !(scancode & 0x80);
                extended = 0;
                continue;
            }
            if (!extended && ((scancode & 0x7F) == 0x2A || (scancode & 0x7F) == 0x36)) {
                if ((scancode & 0x7F) == 0x2A) shift_left = !(scancode & 0x80);
                else shift_right = !(scancode & 0x80);
                continue;
            }
            if (extended) { extended = 0; continue; }

            /*
             * Ignore key-release scancodes.
             */
            if (scancode < 128) {

                char c =
                    keyboard_map[scancode];

                if ((shift_left || shift_right) && !(control_left || control_right)) c = shifted_character(c);

                if (gfx_stats.active) {
                    if(c==27){mouse_demo_active=0;gfx_leave();print("Graphics closed. VGA CUI restored.\nOVICS> ");}
                    else if(wm_session_active)wm_handle_key(c,control_left || control_right);
                    continue;
                }
                if (editor_active) {
                    editor_key(c, control_left || control_right);
                    continue;
                }
                if (control_left || control_right || c == 27) continue;

                /* ENTER */
                if (c == '\n') {

                    putchar('\n');

                    command[command_pos] = 0;

                    if (command_overflow) http_input_overflow();
                    else run_command(command);
                    command_overflow = 0;

                    command_pos = 0;

                    if (!editor_active && !gfx_stats.active) print("OVICS> ");
                }

                /* BACKSPACE */
                else if (c == '\b') {

                    if (command_pos > 0) {

                        command_pos--;

                        if (cursor > 0) {

                            cursor--;

                            screen[cursor] =
                                (0x0F << 8) | ' ';

                            update_cursor();
                        }
                    }
                }

                /* NORMAL CHARACTER */
                else if (c) {

                    int is_http = command_pos >= 8 && starts_with(command, "httpget ");
                    if (command_pos < 63 || (is_http && command_pos < 255)) {

                        command[command_pos++] =
                            c;

                        putchar(c);
                    } else if (is_http) command_overflow = 1;
                }
            }
        }

}while(0);}

void kernel_main(unsigned int magic, unsigned int info)
{
    pm_init(magic, info, (unsigned int)&kernel_start, (unsigned int)&kernel_end);
    exceptions_initialize();
    if(!vm_initialize()) {
        clear_screen();print("OVICS VM bootstrap failed: ");print(vm_last_error);print("\n");exception_stop();
    }
    timer_init();
#if OVICS_VM_FAULT_TEST
    vm_fault_probe();
#endif
    gfx_init();
    init_ram_files();
    network_initialize();
    clock_start();
    mouse_init();
    ps2_enable_idle_wake();
    wm_init();
    clear_screen();

    print("OVICS 0.2.2 Experimental\n");
    print("----------------\n");
    print(
        "Open World Interface Computer System\n\n"
    );

    print("OVICS> ");

    while (1) {

        ps2_poll();
        gfx_tick(clock_now());
        wm_tick();
        mouse_ui_tick();
        network_poll(); /* Silent polling preserves shell/editor layout. */

        kernel_poll_keyboard();
        browser_tick();
        /* Race-free idle: IRQ1/12 can wake us if IRQ0 stops. Never sleep while
           queued scancodes/events still need the single polling consumer. */
        if(ps2_idle_ready) {
            __asm__ volatile("cli":::"memory");
            if(!ps2_raw_count && !ps2_key_count && (!(wm_session_active || mouse_demo_active) || !mouse_state.queued) && !(inb(0x64)&1))
                __asm__ volatile("sti; hlt":::"memory");
            else __asm__ volatile("sti":::"memory");
        }
    }
}
