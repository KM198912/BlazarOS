#pragma once

#include <types.h>

#include <mm/vmm/vmm.h>

#include <lib/string/string.h>

typedef struct {
    char magic[4];
    u8 bits;
    u8 endian;
    u8 version;
    u8 abi;
    u64 padding;
    u16 type;
    u16 isa;
    u32 elf_version;
    u64 entry;
    u64 phoff;
    u64 shoff;
    u32 flags;
    u16 hdr_size;
    u16 entry_ph_size;
    u16 entry_ph_count;
    u16 entry_sh_size;
    u16 entry_sh_count;
    u16 sh_names;
} elf_header;

typedef struct {
    u32 type;
    u32 flags;
    u64 offset;
    u64 vaddr;
    u64 paddr;
    u64 file_size;
    u64 mem_size;
    u64 align;
} elf_ph;

u64 elf_load(char* addr, page_map* pm);