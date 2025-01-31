#include <types.h>
#include <limine.h>

#include <main.h>

#include <lib/stdio/printf.h>
#include <lib/string/string.h>
#include <lib/log.h>

#include <dev/serial/serial.h>

#include <dev/ps2/keyboard.h>
#include <dev/ps2/mouse.h>

#include <dev/timer/pit/pit.h>
#include <dev/timer/hpet/hpet.h>

#include <dev/initrd/blazfs.h>

#include <dev/storage/ata.h>

#include <arch/gdt/gdt.h>
#include <arch/idt/idt.h>

#include <arch/acpi/acpi.h>
#include <arch/acpi/madt.h>
#include <arch/acpi/lapic.h>
#include <arch/acpi/ioapic.h>

#include <arch/smp/smp.h>

#include <mm/pmm/pmm.h>
#include <mm/vmm/vmm.h>
#include <mm/heap/heap.h>

#include <sched/sched.h>

#include <fs/mbr.h>
#include <fs/fat32.h>

#include <lib/hashmap.h>
#include <lib/atomic.h>

#include <dev/timer/rtc/rtc.h>

#include <video/framebuffer.h>
#include <video/libs/tga.h>
#include <video/vbe.h>

// Set the base revision to 1, this is recommended as this is the latest
// base revision described by the Limine boot protocol specification.
// See specification for further info.

LIMINE_BASE_REVISION(1)

// The Limine requests can be placed anywhere, but it is important that
// the compiler does not optimise them away, so, in C, they should
// NOT be made "static".

struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};
struct limine_framebuffer *framebuffer = NULL;
struct flanterm_context *ft_ctx = NULL;

// HHDM

struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0
};

u64 hhdm_offset = 0;

// Modules

struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST,
    .revision = 0
};

// Halt and catch fire function.
static void hcf(void) {
    __asm__ ("cli");
    for (;;) {
        __asm__ ("hlt");
    }
}

void putchar_(char c) {
    char str[] = {c};
    flanterm_write(ft_ctx, str, 1);
}

void* get_mod_addr(int pos) {
    return module_request.response->modules[pos]->address;
}

void vbe_task() {
    while (1) {
        fb_clear(vbe, 0xFFFFFFFF);
        vbe_swap();
    }
}

// The following will be our kernel's entry point.
// If renaming _start() to something else, make sure to change the
// linker script accordingly.
void _start(void) {
    // Ensure the bootloader actually understands our base revision (see spec).
    if (LIMINE_BASE_REVISION_SUPPORTED == false) {
        hcf();
    }

    // Ensure we got a framebuffer.
    if (framebuffer_request.response == NULL
     || framebuffer_request.response->framebuffer_count < 1) {
        hcf();
    }

    hhdm_offset = hhdm_request.response->offset;

    framebuffer = framebuffer_request.response->framebuffers[0];

    ft_ctx = flanterm_fb_simple_init(
        framebuffer->address, framebuffer->width,
        framebuffer->height, framebuffer->pitch
    );

    serial_init();
    serial_printf("Serial Initialised.\n");

    gdt_init();
    serial_printf("GDT Initialised.\n");

    idt_init();
    pic_disable();
    serial_printf("IDT Initialised.\n");

    log_info("HHDM is %lx\n", hhdm_offset);

    pmm_init();
    serial_printf("PMM Initialised.\n");

    vmm_init();
    serial_printf("VMM Initialised.\n");

    blazfs_init(get_mod_addr(0));

    ata_init();
    mbr_init();

    u64 sdt_addr = acpi_init();
    if (sdt_addr > 0) {
        serial_printf("ACPI: SDT found at %lx.\n", sdt_addr);
    } else {
        panic("ACPI: Couldn't find SDT/Bad SDP. %d.\n", sdt_addr);
        for (;;) ;
    }

    madt_init();
    lapic_init();
    serial_printf("LAPIC Initialised.\n");

    ioapic_init();
    serial_printf("IO/APIC Initialised.\n");

    log_info("CPU 0 lapic id: %x\n", lapic_get_id());
    smp_init();

    serial_printf("SMP Initialised.\n");

    mouse_init();
    keyboard_init();

    vbe_init(framebuffer);

    // Idea: make a black on white terminal

    hpet_init();
    sched_init();
    sched_new_proc(vbe_task, 1, PROC_PR_HIGH);
    irq_register(SCHED_INT_VEC - 32, sched_switch);

    // We're done, just hang...
    while (true) {
        __asm__ volatile ("nop");
    }
}
