#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifndef ARMU_MEM_SIZE
#define ARMU_MEM_SIZE (8 * 1024 * 1024)
#endif  // ARMU_MEM_SIZE

struct Flags {
    bool n;
    bool z;
    bool c;
    bool v;
};

struct Armu {
    uint64_t regs[32];
    uint64_t pc;
    size_t program_size;
    uint8_t* mem;
    size_t mem_capacity;
    struct Flags flags;
};

void armu_run(struct Armu* armu);
