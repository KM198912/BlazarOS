#include <arch/acpi/ioapic.h>
#include <arch/smp/smp.h>

void ioapic_write(madt_ioapic* ioapic, u8 reg, u32 val) {
    mmio_write32(HIGHER_HALF(ioapic->apic_addr) + IOAPIC_REGSEL, reg);
    mmio_write32(HIGHER_HALF(ioapic->apic_addr) + IOAPIC_IOWIN, val);
}

u32 ioapic_read(madt_ioapic* ioapic, u8 reg) {
    mmio_write32(HIGHER_HALF(ioapic->apic_addr) + IOAPIC_REGSEL, reg);
    return mmio_read32(HIGHER_HALF(ioapic->apic_addr) + IOAPIC_IOWIN);
}

void ioapic_set_entry(madt_ioapic* ioapic, u8 idx, u64 data) {
    ioapic_write(ioapic, (u8)(IOAPIC_REDTBL + idx * 2), (u32)data);
    ioapic_write(ioapic, (u8)(IOAPIC_REDTBL + idx * 2 + 1), (u32)(data >> 32));
}

u64 ioapic_gsi_count(madt_ioapic* ioapic) {
    return (ioapic_read(ioapic, 1) & 0xff0000) >> 16;
}

madt_ioapic* ioapic_get_gsi(u32 gsi) {
    for (u64 i = 0; i < madt_ioapic_len; i++) {
        madt_ioapic* ioapic = madt_ioapic_list[i];
        if (ioapic->gsi_base <= gsi && ioapic->gsi_base + ioapic_gsi_count(ioapic) > gsi)
            return ioapic;
    }
    return NULL;
}

void ioapic_redirect_gsi(u32 lapic_id, u8 vec, u32 gsi, u16 flags, bool mask) {
    madt_ioapic* ioapic = ioapic_get_gsi(gsi);

    u64 redirect = vec;

    if ((flags & (1 << 1)) != 0) {
        redirect |= (1 << 13);
    }

    if ((flags & (1 << 3)) != 0) {
        redirect |= (1 << 15);
    }

    if (mask) redirect |= (1 << 16);
    else redirect &= ~(1 << 16);

    redirect |= (uint64_t)lapic_id << 56;

    u32 redir_table = (gsi - ioapic->gsi_base) * 2 + 16;
    ioapic_write(ioapic, redir_table, (u32)redirect);
    ioapic_write(ioapic, redir_table + 1, (u32)(redirect >> 32));
}

void ioapic_redirect_irq(u32 lapic_id, u8 vec, u8 irq, bool mask) {
    u8 idx = 0;
    madt_iso* iso;

    while (idx < madt_iso_len) {
        iso = madt_iso_list[idx];
        if (iso->irq_src == irq) {
            ioapic_redirect_gsi(lapic_id, vec, iso->gsi, iso->flags, mask);
            return;
        }
        idx++;
    }

    ioapic_redirect_gsi(lapic_id, vec, irq, 0, mask);
}

u32 ioapic_get_redirect_irq(u8 irq) {
    u8 idx = 0;
    madt_iso* iso;

    while (idx < madt_iso_len) {
        iso = madt_iso_list[idx];
        if (iso->irq_src == irq) {
            return iso->gsi;
        }
        idx++;
    }

    return irq;
}

void ioapic_init() {
    madt_ioapic* ioapic = madt_ioapic_list[0];
    
    u32 val = ioapic_read(ioapic, IOAPIC_VER);
    u32 count = ((val >> 16) & 0xFF);

    if ((ioapic_read(ioapic, 0) >> 24) != ioapic->apic_id) {
        log_bad("IOAPIC: Not the MP!\n");
    }

    for (u32 i = 0; i <= count; ++i) {
        ioapic_write(ioapic, IOAPIC_REDTBL+2*i, 0x00010000 | (32 + i));
        ioapic_write(ioapic, IOAPIC_REDTBL+2*i+1, 0); // redir cpu
    }
    // We disable all entries by default
}
