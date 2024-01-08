#include <types.h>
#include <limine.h>

#include <main.h>

#include <lib/stdio/printf.h>
#include <lib/string/string.h>
#include <lib/log.h>

#include <dev/serial/serial.h>
#include <dev/ps2/keyboard.h>
#include <dev/timer/pit/pit.h>
#include <dev/timer/hpet/hpet.h>

#include <arch/gdt/gdt.h>
#include <arch/idt/idt.h>

#include <arch/acpi/acpi.h>
#include <arch/acpi/madt.h>
#include <arch/acpi/lapic.h>
#include <arch/acpi/ioapic.h>

#include <arch/smp/smp.h>

#include <flanterm/flanterm.h>
#include <flanterm/backends/fb.h>

#include <mm/pmm/pmm.h>
#include <mm/vmm/vmm.h>
#include <mm/heap/heap.h>

#include <sched/sched.h>

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

struct flanterm_context *ft_ctx;

// HHDM

struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0
};

u64 hhdm_offset;

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

void idle() {
    while (1);
}

void task1() {
    printf("Tasking!\n");
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

    struct limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[0];

    ft_ctx = flanterm_fb_simple_init(
        framebuffer->address, framebuffer->width,
        framebuffer->height, framebuffer->pitch
    );

    serial_init();
    log_ok("Serial Initialised.\n");

    gdt_init();
    log_ok("GDT Initialised.\n");

    idt_init();
    pic_disable();
    log_ok("IDT Initialised.\n");

    log_info("HHDM is %lx\n", hhdm_offset);

    pmm_init();
    log_ok("PMM Initialised.\n");

    vmm_init();
    log_ok("VMM Initialised.\n");

    u32 rsdt_addr = acpi_init();
    if (rsdt_addr > 0) {
        log_info("ACPI: RSDT found at %lx.\n", rsdt_addr);
    } else {
        panic("ACPI: Couldn't find RSDT/Bad RSDP. %d.\n", rsdt_addr);
        for (;;) ;
    }

    madt_init();
    lapic_init();
    log_ok("LAPIC Initialised.\n");

    ioapic_init();
    log_ok("IO/APIC Initialised.\n");

    log_info("CPU 0 lapic id: %x\n", lapic_get_id());
    smp_init();

    while (smp_cpu_started < smp_cpu_count - 1) {
        __asm__ ("nop");
    }

    log_ok("SMP Initialised.\n");

    keyboard_init();

    sched_init();
    pit_init();

    // We're done, just hang...
    for (;;)
        __asm__ ("hlt");
}