#ifndef TSUKASA_X64_BOOT_PARSER_H
#define TSUKASA_X64_BOOT_PARSER_H

#include "include/boot_info.h"
#include "boot/limine.h"

extern volatile struct limine_smp_request smp_request;

const struct tsukasa_boot_info *tsukasa_boot_info_get(void);
void tsukasa_x64_entry(void);

#endif