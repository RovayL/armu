#pragma once

#include <stdint.h>

#include "armu.h"
#include "elf.h"

struct ArmuElfImageInfo {
    uint64_t entry;
    uint64_t min_vaddr;
    uint64_t max_vaddr;
};

int armu_load_elf(const char* path, struct Armu* armu, struct ArmuElfImageInfo* out_info);
