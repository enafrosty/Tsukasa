/*
 * elf.c - ELF32 executable loader.
 */

#include "elf.h"
#include <stddef.h>
#include <stdint.h>

int elf_verify(const void *buf)
{
    const unsigned char *p = (const unsigned char *)buf;
    if (!p || p[EI_MAG0] != ELFMAG0 || p[EI_MAG1] != ELFMAG1 ||
        p[EI_MAG2] != ELFMAG2 || p[EI_MAG3] != ELFMAG3)
        return -1;
    const struct elf32_ehdr *ehdr = (const struct elf32_ehdr *)buf;
    if (ehdr->e_type != ET_EXEC)
        return -1;
    return 0;
}

uint32_t elf_load(const void *buf)
{
    if (elf_verify(buf) != 0)
        return 0;
    const struct elf32_ehdr *ehdr = (const struct elf32_ehdr *)buf;
    const struct elf32_phdr *phdr = (const struct elf32_phdr *)
        ((const char *)buf + ehdr->e_phoff);
    uint32_t entry = ehdr->e_entry;

    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD)
            continue;
        const uint8_t *src = (const uint8_t *)buf + phdr[i].p_offset;
        uint8_t *dst = (uint8_t *)(uintptr_t)phdr[i].p_vaddr;
        for (uint32_t j = 0; j < phdr[i].p_filesz; j++)
            dst[j] = src[j];
    }
    return entry;
}
