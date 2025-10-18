#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tests.h"
#include "armu.h"
#include "disarm64.h"
#include "elf_loader.h"

#ifndef FIB_ELF_AVAILABLE
#define FIB_ELF_AVAILABLE 0
#endif

#if FIB_ELF_AVAILABLE && !defined(FIB_ELF_PATH)
#define FIB_ELF_PATH "fibonacci_bare"
#endif

int
test_simple_arith_ldr_str(void)
{
    struct Armu armu = {0};
    static uint8_t memory[ARMU_MEM_SIZE];
    uint32_t program[13];
    size_t idx = 0;

    program[idx++] = de64_MOVZx(DA_GP(0), 0x1234);
    program[idx++] = de64_MOVKx_shift(DA_GP(0), 0xBEEF, 1);

    program[idx++] = de64_ADDxi(DA_GP(1), DA_GP(0), 5);
    program[idx++] = de64_SUBxi(DA_GP(2), DA_GP(1), 3);
    program[idx++] = de64_ANDxi(DA_GP(3), DA_GP(1), 0xFF);
    program[idx++] = de64_ORRxi(DA_GP(4), DA_GP(2), 0x100);
    program[idx++] = de64_EORxi(DA_GP(5), DA_GP(3), 0xFF);

    program[idx++] = de64_MOVZx(DA_GP(9), 0x400);
    program[idx++] = de64_STRxu(DA_GP(4), DA_GP(9), 0);
    program[idx++] = de64_LDRxu(DA_GP(6), DA_GP(9), 0);

    program[idx++] = de64_MOVZw(DA_GP(7), 0xFACE);
    program[idx++] = de64_STRwu(DA_GP(7), DA_GP(9), 8);
    program[idx++] = de64_LDRwu(DA_GP(8), DA_GP(9), 8);

    armu.mem = memory;
    armu.mem_capacity = sizeof(memory);
    armu.program_size = idx * sizeof(program[0]);
    memcpy(armu.mem, program, armu.program_size);
    armu.pc = 0;

    armu_run(&armu);

    int status = 0;
    const uint64_t expected_x0 = 0x00000000beef1234ULL;
    const uint64_t expected_x1 = expected_x0 + 5;
    const uint64_t expected_x2 = expected_x1 - 3;
    const uint64_t expected_x3 = expected_x1 & 0xff;
    const uint64_t expected_x4 = expected_x2 | 0x100;
    const uint64_t expected_x5 = expected_x3 ^ 0xff;
    const uint64_t expected_base = 0x400;
    const uint32_t expected_w7 = 0xface;

    if (armu.regs[0] != expected_x0) {
        fprintf(stderr, "[simple] X0 mismatch: got 0x%016lx expected 0x%016lx\n",
                armu.regs[0], expected_x0);
        status = 1;
    }
    if (armu.regs[1] != expected_x1) {
        fprintf(stderr, "[simple] X1 mismatch: got 0x%016lx expected 0x%016lx\n",
                armu.regs[1], expected_x1);
        status = 1;
    }
    if (armu.regs[2] != expected_x2) {
        fprintf(stderr, "[simple] X2 mismatch: got 0x%016lx expected 0x%016lx\n",
                armu.regs[2], expected_x2);
        status = 1;
    }
    if (armu.regs[3] != expected_x3) {
        fprintf(stderr, "[simple] X3 mismatch: got 0x%016lx expected 0x%016lx\n",
                armu.regs[3], expected_x3);
        status = 1;
    }
    if (armu.regs[4] != expected_x4) {
        fprintf(stderr, "[simple] X4 mismatch: got 0x%016lx expected 0x%016lx\n",
                armu.regs[4], expected_x4);
        status = 1;
    }
    if (armu.regs[5] != expected_x5) {
        fprintf(stderr, "[simple] X5 mismatch: got 0x%016lx expected 0x%016lx\n",
                armu.regs[5], expected_x5);
        status = 1;
    }
    if (armu.regs[6] != expected_x4) {
        fprintf(stderr, "[simple] X6 mismatch: got 0x%016lx expected 0x%016lx\n",
                armu.regs[6], expected_x4);
        status = 1;
    }
    if (armu.regs[8] != (uint64_t)expected_w7) {
        fprintf(stderr, "[simple] X8 mismatch: got 0x%016lx expected 0x%016lx\n",
                armu.regs[8], (uint64_t)expected_w7);
        status = 1;
    }

    uint64_t stored_x4 = 0;
    memcpy(&stored_x4, memory + expected_base, sizeof(stored_x4));
    if (stored_x4 != expected_x4) {
        fprintf(stderr, "[simple] Memory[0x%llx] mismatch: got 0x%016llx expected 0x%016llx\n",
                (unsigned long long)expected_base,
                (unsigned long long)stored_x4,
                (unsigned long long)expected_x4);
        status = 1;
    }

    uint32_t stored_w7 = 0;
    memcpy(&stored_w7, memory + expected_base + 8, sizeof(stored_w7));
    if (stored_w7 != expected_w7) {
        fprintf(stderr, "[simple] Memory[0x%llx] mismatch: got 0x%08x expected 0x%08x\n",
                (unsigned long long)(expected_base + 8),
                stored_w7,
                expected_w7);
        status = 1;
    }

    if (status == 0) {
        printf("simple_arith_ldr_str: pass\n");
    }

    return status;
}

int
test_load_store_addressing(void)
{
    struct Armu armu = {0};
    static uint8_t memory[ARMU_MEM_SIZE];

    const uint64_t val_x0 = 0x1111222233334444ULL;
    const uint64_t val_x1 = 0xAAAABBBBCCCCDDDDULL;
    const uint64_t val_x2 = 0x5555666677778888ULL;
    const uint64_t val_x3 = 0x9999AAAABBBB0001ULL;
    const uint32_t val_w3 = (uint32_t)val_x3;
    const uint8_t val_byte1 = 0xAB;
    const uint8_t val_byte_post_store = 0x33;
    const uint8_t val_byte_post_mem = 0x44;
    const uint16_t val_half_neg = 0xF234;
    const uint64_t expected_byte_signed = (uint64_t)(int8_t)val_byte1;
    const uint64_t expected_half_signed = (uint64_t)(int16_t)val_half_neg;

    uint32_t program[] = {
        de64_MOVZx(DA_GP(0), 0x4444),
        de64_MOVKx_shift(DA_GP(0), 0x3333, 1),
        de64_MOVKx_shift(DA_GP(0), 0x2222, 2),
        de64_MOVKx_shift(DA_GP(0), 0x1111, 3),

        de64_MOVZx(DA_GP(1), 0xDDDD),
        de64_MOVKx_shift(DA_GP(1), 0xCCCC, 1),
        de64_MOVKx_shift(DA_GP(1), 0xBBBB, 2),
        de64_MOVKx_shift(DA_GP(1), 0xAAAA, 3),

        de64_MOVZx(DA_GP(2), 0x8888),
        de64_MOVKx_shift(DA_GP(2), 0x7777, 1),
        de64_MOVKx_shift(DA_GP(2), 0x6666, 2),
        de64_MOVKx_shift(DA_GP(2), 0x5555, 3),

        de64_MOVZx(DA_GP(3), 0x0001),
        de64_MOVKx_shift(DA_GP(3), 0xBBBB, 1),
        de64_MOVKx_shift(DA_GP(3), 0xAAAA, 2),
        de64_MOVKx_shift(DA_GP(3), 0x9999, 3),

        de64_MOVZx(DA_GP(9), 0x0500),
        de64_MOVZx(DA_GP(10), 0x0600),
        de64_MOVZx(DA_GP(11), 0x0680),
        de64_MOVZx(DA_GP(12), 0x0002),
        de64_MOVZx(DA_GP(13), 0x0700),
        de64_MOVZw(DA_GP(14), 0x0003),

        de64_STURx(DA_GP(0), DA_GP(9), -8),
        de64_LDURx(DA_GP(5), DA_GP(9), -8),

        de64_STRx_pre(DA_GP(1), DA_GP(10), 16),
        de64_LDRx_post(DA_GP(6), DA_GP(10), 8),

        de64_STRxr_lsl(DA_GP(2), DA_GP(11), DA_GP(12), true),
        de64_LDRxr_lsl(DA_GP(7), DA_GP(11), DA_GP(12), true),

        de64_STRwr_uxtw(DA_GP(3), DA_GP(13), DA_GP(14), false),
        de64_LDRwr_uxtw(DA_GP(8), DA_GP(13), DA_GP(14), false),

        de64_MOVZx(DA_GP(15), 0x0800),
        de64_MOVZx(DA_GP(16), 0x00ab),
        de64_STRBu(DA_GP(16), DA_GP(15), 0),
        de64_LDRBu(DA_GP(17), DA_GP(15), 0),
        de64_LDRSBxu(DA_GP(18), DA_GP(15), 0),

        de64_MOVZx(DA_GP(19), 0x0810),
        de64_MOVZx(DA_GP(20), 0xF234),
        de64_STRHu(DA_GP(20), DA_GP(19), 2),
        de64_LDRHu(DA_GP(21), DA_GP(19), 2),
        de64_LDRSHxu(DA_GP(22), DA_GP(19), 2),

        de64_MOVZx(DA_GP(23), 0x0820),
        de64_MOVZx(DA_GP(24), 0x0044),
        de64_STRBu(DA_GP(24), DA_GP(23), 1),
        de64_MOVZx(DA_GP(24), 0x0033),
        de64_STRB_post(DA_GP(24), DA_GP(23), 1),
        de64_LDRB_post(DA_GP(25), DA_GP(23), 1),

        de64_MOVZx(DA_GP(26), 0x0830),
        de64_MOVZx(DA_GP(27), 0x0077),
        de64_MOVZx(DA_GP(28), 0x0002),
        de64_STRBr_uxtw(DA_GP(27), DA_GP(26), DA_GP(28), false),
        de64_LDRBr_uxtw(DA_GP(29), DA_GP(26), DA_GP(28), false),
    };

    armu.mem = memory;
    armu.mem_capacity = sizeof(memory);
    armu.program_size = sizeof(program);
    memcpy(armu.mem, program, sizeof(program));
    armu.pc = 0;

    armu_run(&armu);

    int status = 0;

    const uint64_t addr_unscaled = 0x0500 - 8;
    const uint64_t addr_pre = 0x0600 + 16;
    const uint64_t addr_reg64 = 0x0680 + (uint64_t)(0x0002ULL << 3);
    const uint64_t addr_reg32 = 0x0700 + (uint64_t)(0x0003ULL << 2);
    const uint64_t addr_byte = 0x0800;
    const uint64_t addr_half = 0x0810 + 2;
    const uint64_t addr_post_base = 0x0820;
    const uint64_t addr_regbyte = 0x0830 + 0x0002ULL;

    uint64_t mem64 = 0;
    memcpy(&mem64, memory + addr_unscaled, sizeof(mem64));
    if (mem64 != val_x0) {
        fprintf(stderr, "[ldst] mem 0x%llx mismatch: got 0x%016llx expected 0x%016llx\n",
                (unsigned long long)addr_unscaled,
                (unsigned long long)mem64,
                (unsigned long long)val_x0);
        status = 1;
    }

    memcpy(&mem64, memory + addr_pre, sizeof(mem64));
    if (mem64 != val_x1) {
        fprintf(stderr, "[ldst] mem 0x%llx mismatch: got 0x%016llx expected 0x%016llx\n",
                (unsigned long long)addr_pre,
                (unsigned long long)mem64,
                (unsigned long long)val_x1);
        status = 1;
    }

    memcpy(&mem64, memory + addr_reg64, sizeof(mem64));
    if (mem64 != val_x2) {
        fprintf(stderr, "[ldst] mem 0x%llx mismatch: got 0x%016llx expected 0x%016llx\n",
                (unsigned long long)addr_reg64,
                (unsigned long long)mem64,
                (unsigned long long)val_x2);
        status = 1;
    }

    uint32_t mem32 = 0;
    memcpy(&mem32, memory + addr_reg32, sizeof(mem32));
    if (mem32 != val_w3) {
        fprintf(stderr, "[ldst] mem 0x%llx mismatch: got 0x%08x expected 0x%08x\n",
                (unsigned long long)addr_reg32,
                mem32,
                val_w3);
        status = 1;
    }

    uint8_t mem8 = 0;
    memcpy(&mem8, memory + addr_byte, sizeof(mem8));
    if (mem8 != val_byte1) {
        fprintf(stderr, "[ldst] mem byte 0x%llx mismatch: got 0x%02x expected 0x%02x\n",
                (unsigned long long)addr_byte, mem8, val_byte1);
        status = 1;
    }

    uint16_t mem16 = 0;
    memcpy(&mem16, memory + addr_half, sizeof(mem16));
    if (mem16 != val_half_neg) {
        fprintf(stderr, "[ldst] mem half 0x%llx mismatch: got 0x%04x expected 0x%04x\n",
                (unsigned long long)addr_half, mem16, val_half_neg);
        status = 1;
    }

    memcpy(&mem8, memory + addr_post_base, sizeof(mem8));
    if (mem8 != val_byte_post_store) {
        fprintf(stderr, "[ldst] mem post base 0x%llx mismatch: got 0x%02x expected 0x%02x\n",
                (unsigned long long)addr_post_base, mem8, val_byte_post_store);
        status = 1;
    }

    memcpy(&mem8, memory + addr_post_base + 1, sizeof(mem8));
    if (mem8 != val_byte_post_mem) {
        fprintf(stderr, "[ldst] mem post base+1 0x%llx mismatch: got 0x%02x expected 0x%02x\n",
                (unsigned long long)(addr_post_base + 1), mem8, val_byte_post_mem);
        status = 1;
    }

    memcpy(&mem8, memory + addr_regbyte, sizeof(mem8));
    if (mem8 != 0x77) {
        fprintf(stderr, "[ldst] mem reg byte 0x%llx mismatch: got 0x%02x expected 0x77\n",
                (unsigned long long)addr_regbyte, mem8);
        status = 1;
    }

    if (armu.regs[5] != val_x0) {
        fprintf(stderr, "[ldst] X5 mismatch: got 0x%016lx expected 0x%016lx\n",
                armu.regs[5], val_x0);
        status = 1;
    }
    if (armu.regs[6] != val_x1) {
        fprintf(stderr, "[ldst] X6 mismatch: got 0x%016lx expected 0x%016lx\n",
                armu.regs[6], val_x1);
        status = 1;
    }
    if (armu.regs[7] != val_x2) {
        fprintf(stderr, "[ldst] X7 mismatch: got 0x%016lx expected 0x%016lx\n",
                armu.regs[7], val_x2);
        status = 1;
    }
    if (armu.regs[8] != (uint64_t)val_w3) {
        fprintf(stderr, "[ldst] X8 mismatch: got 0x%016lx expected 0x%016lx\n",
                armu.regs[8], (uint64_t)val_w3);
        status = 1;
    }

    if (armu.regs[17] != (uint64_t)val_byte1) {
        fprintf(stderr, "[ldst] X17 mismatch: got 0x%016lx expected 0x%016lx\n",
                armu.regs[17], (uint64_t)val_byte1);
        status = 1;
    }
    if (armu.regs[18] != expected_byte_signed) {
        fprintf(stderr, "[ldst] X18 mismatch: got 0x%016lx expected 0x%016lx\n",
                armu.regs[18], expected_byte_signed);
        status = 1;
    }
    if (armu.regs[21] != (uint64_t)val_half_neg) {
        fprintf(stderr, "[ldst] X21 mismatch: got 0x%016lx expected 0x%016lx\n",
                armu.regs[21], (uint64_t)val_half_neg);
        status = 1;
    }
    if (armu.regs[22] != expected_half_signed) {
        fprintf(stderr, "[ldst] X22 mismatch: got 0x%016lx expected 0x%016lx\n",
                armu.regs[22], expected_half_signed);
        status = 1;
    }
    if (armu.regs[25] != (uint64_t)val_byte_post_mem) {
        fprintf(stderr, "[ldst] X25 mismatch: got 0x%016lx expected 0x%016lx\n",
                armu.regs[25], (uint64_t)val_byte_post_mem);
        status = 1;
    }
    if (armu.regs[29] != 0x77ULL) {
        fprintf(stderr, "[ldst] X29 mismatch: got 0x%016lx expected 0x0000000000000077\n",
                armu.regs[29]);
        status = 1;
    }

    if (armu.regs[10] != 0x0618ULL) {
        fprintf(stderr, "[ldst] X10 mismatch: got 0x%016lx expected 0x0000000000000618\n",
                armu.regs[10]);
        status = 1;
    }
    if (armu.regs[11] != 0x0680ULL) {
        fprintf(stderr, "[ldst] X11 mismatch: got 0x%016lx expected 0x0000000000000680\n",
                armu.regs[11]);
        status = 1;
    }
    if (armu.regs[13] != 0x0700ULL) {
        fprintf(stderr, "[ldst] X13 mismatch: got 0x%016lx expected 0x0000000000000700\n",
                armu.regs[13]);
        status = 1;
    }
    if (armu.regs[23] != 0x0822ULL) {
        fprintf(stderr, "[ldst] X23 mismatch: got 0x%016lx expected 0x0000000000000822\n",
                armu.regs[23]);
        status = 1;
    }
    if (armu.regs[26] != 0x0830ULL) {
        fprintf(stderr, "[ldst] X26 mismatch: got 0x%016lx expected 0x0000000000000830\n",
                armu.regs[26]);
        status = 1;
    }

    if (status == 0) {
        printf("load_store_addressing: pass\n");
    }

    return status;
}

int
test_flags_and_sp(void)
{
    int status = 0;
    static uint8_t memory[ARMU_MEM_SIZE];

    /* Scenario A: ADDS setting carry and zero, followed by ADC consuming carry. */
    {
        struct Armu armu = {0};
        memset(memory, 0, sizeof(memory));
        uint32_t program[] = {
            de64_MOVNx(DA_GP(0), 0x0),
            de64_ADDSxi(DA_GP(1), DA_GP(0), 1),
            de64_ADCx(DA_GP(2), DA_GP(31), DA_GP(31)),
        };

        armu.mem = memory;
        armu.mem_capacity = sizeof(memory);
        armu.program_size = sizeof(program);
        memcpy(armu.mem, program, sizeof(program));

        armu_run(&armu);

        if (armu.regs[1] != 0) {
            fprintf(stderr, "[flags] X1 mismatch: got 0x%016lx expected 0\n", armu.regs[1]);
            status = 1;
        }
        if (armu.regs[2] != 1) {
            fprintf(stderr, "[flags] X2 mismatch: got 0x%016lx expected 0x0000000000000001\n", armu.regs[2]);
            status = 1;
        }
        if (armu.flags.n || !armu.flags.z || !armu.flags.c || armu.flags.v) {
            fprintf(stderr, "[flags] NZCV mismatch after ADDS: N=%d Z=%d C=%d V=%d expected 0,1,1,0\n",
                    armu.flags.n, armu.flags.z, armu.flags.c, armu.flags.v);
            status = 1;
        }
    }

    /* Scenario B: Overflowing ADDS followed by SBCS to test borrow handling. */
    {
        struct Armu armu = {0};
        memset(memory, 0, sizeof(memory));
        uint32_t program[] = {
            de64_MOVZx(DA_GP(3), 0xFFFF),
            de64_MOVKx_shift(DA_GP(3), 0xFFFF, 1),
            de64_MOVKx_shift(DA_GP(3), 0xFFFF, 2),
            de64_MOVKx_shift(DA_GP(3), 0x7FFF, 3),
            de64_ADDSxi(DA_GP(4), DA_GP(3), 1),
            de64_SBCx(DA_GP(5), DA_GP(31), DA_GP(31)),
            de64_SBCSx(DA_GP(6), DA_GP(31), DA_GP(31)),
        };

        armu.mem = memory;
        armu.mem_capacity = sizeof(memory);
        armu.program_size = sizeof(program);
        memcpy(armu.mem, program, sizeof(program));

        armu_run(&armu);

        if (armu.regs[4] != 0x8000000000000000ULL) {
            fprintf(stderr, "[flags] X4 mismatch: got 0x%016lx expected 0x8000000000000000\n", armu.regs[4]);
            status = 1;
        }
        if (armu.regs[5] != UINT64_MAX || armu.regs[6] != UINT64_MAX) {
            fprintf(stderr, "[flags] SBC results mismatch: X5=0x%016lx X6=0x%016lx expected 0xffffffffffffffff\n",
                    armu.regs[5], armu.regs[6]);
            status = 1;
        }
        if (!armu.flags.n || armu.flags.z || armu.flags.c || armu.flags.v) {
            fprintf(stderr, "[flags] NZCV mismatch after SBCS: N=%d Z=%d C=%d V=%d expected 1,0,0,0\n",
                    armu.flags.n, armu.flags.z, armu.flags.c, armu.flags.v);
            status = 1;
        }
    }

    /* Scenario C: SUBS without borrow. */
    {
        struct Armu armu = {0};
        memset(memory, 0, sizeof(memory));
        uint32_t program[] = {
            de64_MOVZx(DA_GP(7), 5),
            de64_SUBSxi(DA_GP(8), DA_GP(7), 5),
        };

        armu.mem = memory;
        armu.mem_capacity = sizeof(memory);
        armu.program_size = sizeof(program);
        memcpy(armu.mem, program, sizeof(program));

        armu_run(&armu);

        if (armu.regs[8] != 0) {
            fprintf(stderr, "[flags] X8 mismatch: got 0x%016lx expected 0\n", armu.regs[8]);
            status = 1;
        }
        if (armu.flags.n || !armu.flags.z || !armu.flags.c || armu.flags.v) {
            fprintf(stderr, "[flags] NZCV mismatch after SUBS: N=%d Z=%d C=%d V=%d expected 0,1,1,0\n",
                    armu.flags.n, armu.flags.z, armu.flags.c, armu.flags.v);
            status = 1;
        }
    }

    /* Scenario D: SP updates and general register reads from SP. */
    {
        struct Armu armu = {0};
        memset(memory, 0, sizeof(memory));
        armu.regs[31] = 0x100;
        uint32_t program[] = {
            de64_ADDxi(DA_SP, DA_SP, 16),
            de64_SUBxi(DA_SP, DA_SP, 8),
            de64_ADDxi(DA_GP(9), DA_SP, 0),
        };

        armu.mem = memory;
        armu.mem_capacity = sizeof(memory);
        armu.program_size = sizeof(program);
        memcpy(armu.mem, program, sizeof(program));

        armu_run(&armu);

        if (armu.regs[31] != 0x108ULL) {
            fprintf(stderr, "[flags] SP mismatch: got 0x%016lx expected 0x0000000000000108\n", armu.regs[31]);
            status = 1;
        }
        if (armu.regs[9] != 0x108ULL) {
            fprintf(stderr, "[flags] X9 mismatch: got 0x%016lx expected 0x0000000000000108\n", armu.regs[9]);
            status = 1;
        }
    }

    if (status == 0) {
        printf("flags_and_sp: pass\n");
    }

    return status;
}

int
test_fibonacci_elf(void)
{
#if !FIB_ELF_AVAILABLE
    printf("fibonacci_elf: skipped (aarch64-linux-gnu-gcc unavailable)\n");
    return 0;
#else
    struct Armu armu = {0};
    static uint8_t memory[ARMU_MEM_SIZE];
    memset(memory, 0, sizeof(memory));
    armu.mem = memory;
    armu.mem_capacity = sizeof(memory);

    struct ArmuElfImageInfo info = {0};
    if (armu_load_elf(FIB_ELF_PATH, &armu, &info) != 0) {
        return 1;
    }

    armu.pc = info.entry;
    uint64_t stack_top = ARMU_MEM_SIZE - 0x1000;
    stack_top &= ~(uint64_t)0xF;
    armu.regs[31] = stack_top;

    armu_run(&armu);

    int status = 0;
    const uint64_t expected = 6765;
    if (armu.regs[0] != expected) {
        fprintf(stderr, "[fibonacci] X0 mismatch: got 0x%016lx expected 0x%016llx\n",
                armu.regs[0], (unsigned long long)expected);
        status = 1;
    }
    if (armu.pc != armu.program_size) {
        fprintf(stderr, "[fibonacci] PC mismatch: got 0x%016lx expected 0x%016zx\n",
                armu.pc, armu.program_size);
        status = 1;
    }

    if (status == 0) {
        printf("fibonacci_elf: pass\n");
    }

    return status;
#endif
}

int
test_branching(void)
{
    struct Armu armu = {0};
    static uint8_t memory[ARMU_MEM_SIZE];

    enum {
        I_MOVZ_X0_ZERO,
        I_CBZ_TO_SETX1,
        I_MOVZ_X1_FAIL,
        I_SET_X1,
        I_B_SKIP_TO_X2,
        I_MOVZ_X2_FAIL,
        I_SET_X2,
        I_MOVZ_X0_FIVE,
        I_CBNZ_TO_SETX3,
        I_MOVZ_X3_FAIL,
        I_SET_X3,
        I_MOVZ_X4_ZERO,
        I_TBZ_TO_SETX5,
        I_MOVZ_X5_FAIL,
        I_SET_X5,
        I_MOVZ_X4_ONE,
        I_TBNZ_TO_SETX6,
        I_MOVZ_X6_FAIL,
        I_SET_X6,
        I_MOVZ_X7_ONE,
        I_SUBS_X7,
        I_BCOND_EQ_TO_SETX8,
        I_MOVZ_X8_FAIL,
        I_SET_X8,
        I_BL_DIRECT,
        I_MOVZ_X10_AFTER_BL,
        I_MOVZ_X12_BRANCH_TARGET,
        I_BR_X12,
        I_BRANCH_TARGET,
        I_MOVZ_X13_SUBADDR,
        I_BLR_X13,
        I_MOVZ_X11_AFTER_BLR,
        I_B_TO_END,
        I_SUBROUTINE_START,
        I_SUBROUTINE_SET_X14,
        I_SUBROUTINE_RET,
        I_END,
        BRANCH_PROGRAM_LEN
    };

    memset(memory, 0, sizeof(memory));
    uint32_t program[BRANCH_PROGRAM_LEN];
    memset(program, 0, sizeof(program));

    program[I_MOVZ_X0_ZERO] = de64_MOVZx(DA_GP(0), 0);
    program[I_CBZ_TO_SETX1] = de64_CBZw(DA_GP(0), I_SET_X1 - I_CBZ_TO_SETX1);
    program[I_MOVZ_X1_FAIL] = de64_MOVZx(DA_GP(1), 0x1111);
    program[I_SET_X1] = de64_MOVZx(DA_GP(1), 0xAAAA);
    program[I_B_SKIP_TO_X2] = de64_B(I_SET_X2 - I_B_SKIP_TO_X2);
    program[I_MOVZ_X2_FAIL] = de64_MOVZx(DA_GP(2), 0xBBBB);
    program[I_SET_X2] = de64_MOVZx(DA_GP(2), 0x2222);
    program[I_MOVZ_X0_FIVE] = de64_MOVZx(DA_GP(0), 5);
    program[I_CBNZ_TO_SETX3] = de64_CBNZw(DA_GP(0), I_SET_X3 - I_CBNZ_TO_SETX3);
    program[I_MOVZ_X3_FAIL] = de64_MOVZx(DA_GP(3), 0xCCCC);
    program[I_SET_X3] = de64_MOVZx(DA_GP(3), 0x3333);
    program[I_MOVZ_X4_ZERO] = de64_MOVZx(DA_GP(4), 0);
    program[I_TBZ_TO_SETX5] = de64_TBZ(DA_GP(4), 0, I_SET_X5 - I_TBZ_TO_SETX5);
    program[I_MOVZ_X5_FAIL] = de64_MOVZx(DA_GP(5), 0xDDDD);
    program[I_SET_X5] = de64_MOVZx(DA_GP(5), 0x5555);
    program[I_MOVZ_X4_ONE] = de64_MOVZx(DA_GP(4), 1);
    program[I_TBNZ_TO_SETX6] = de64_TBNZ(DA_GP(4), 0, I_SET_X6 - I_TBNZ_TO_SETX6);
    program[I_MOVZ_X6_FAIL] = de64_MOVZx(DA_GP(6), 0xEEEE);
    program[I_SET_X6] = de64_MOVZx(DA_GP(6), 0x6666);
    program[I_MOVZ_X7_ONE] = de64_MOVZx(DA_GP(7), 1);
    program[I_SUBS_X7] = de64_SUBSxi(DA_GP(7), DA_GP(7), 1);
    program[I_BCOND_EQ_TO_SETX8] = de64_BCOND(DA_EQ, I_SET_X8 - I_BCOND_EQ_TO_SETX8);
    program[I_MOVZ_X8_FAIL] = de64_MOVZx(DA_GP(8), 0x7777);
    program[I_SET_X8] = de64_MOVZx(DA_GP(8), 0x8888);
    program[I_BL_DIRECT] = de64_BL(I_SUBROUTINE_START - I_BL_DIRECT);
    program[I_MOVZ_X10_AFTER_BL] = de64_MOVZx(DA_GP(10), 0x1010);
    program[I_MOVZ_X12_BRANCH_TARGET] = de64_MOVZx(DA_GP(12), I_BRANCH_TARGET * 4);
    program[I_BR_X12] = de64_BR(DA_GP(12));
    program[I_BRANCH_TARGET] = de64_MOVZx(DA_GP(9), 0x9999);
    program[I_MOVZ_X13_SUBADDR] = de64_MOVZx(DA_GP(13), I_SUBROUTINE_START * 4);
    program[I_BLR_X13] = de64_BLR(DA_GP(13));
    program[I_MOVZ_X11_AFTER_BLR] = de64_MOVZx(DA_GP(11), 0x1111);
    program[I_B_TO_END] = de64_B(I_END - I_B_TO_END);
    program[I_SUBROUTINE_START] = de64_MOVZx(DA_GP(14), 0);
    program[I_SUBROUTINE_SET_X14] = de64_MOVZx(DA_GP(14), 0xABCD);
    program[I_SUBROUTINE_RET] = de64_RET(DA_GP(30));
    program[I_END] = de64_MOVZx(DA_GP(15), 0xEEEE);
    armu.mem = memory;
    armu.mem_capacity = sizeof(memory);
    armu.program_size = sizeof(program);
    memcpy(armu.mem, program, sizeof(program));
    armu.pc = 0;

    armu_run(&armu);

    int status = 0;
    if (armu.regs[1] != 0xAAAAULL) {
        fprintf(stderr, "[branch] X1 mismatch: got 0x%016lx expected 0x000000000000AAAA\n", armu.regs[1]);
        status = 1;
    }
    if (armu.regs[2] != 0x2222ULL) {
        fprintf(stderr, "[branch] X2 mismatch: got 0x%016lx expected 0x0000000000002222\n", armu.regs[2]);
        status = 1;
    }
    if (armu.regs[3] != 0x3333ULL) {
        fprintf(stderr, "[branch] X3 mismatch: got 0x%016lx expected 0x0000000000003333\n", armu.regs[3]);
        status = 1;
    }
    if (armu.regs[5] != 0x5555ULL) {
        fprintf(stderr, "[branch] X5 mismatch: got 0x%016lx expected 0x0000000000005555\n", armu.regs[5]);
        status = 1;
    }
    if (armu.regs[6] != 0x6666ULL) {
        fprintf(stderr, "[branch] X6 mismatch: got 0x%016lx expected 0x0000000000006666\n", armu.regs[6]);
        status = 1;
    }
    if (armu.regs[7] != 0ULL) {
        fprintf(stderr, "[branch] X7 mismatch: got 0x%016lx expected 0\n", armu.regs[7]);
        status = 1;
    }
    if (armu.regs[8] != 0x8888ULL) {
        fprintf(stderr, "[branch] X8 mismatch: got 0x%016lx expected 0x0000000000008888\n", armu.regs[8]);
        status = 1;
    }
    if (armu.regs[9] != 0x9999ULL) {
        fprintf(stderr, "[branch] X9 mismatch: got 0x%016lx expected 0x0000000000009999\n", armu.regs[9]);
        status = 1;
    }
    if (armu.regs[10] != 0x1010ULL) {
        fprintf(stderr, "[branch] X10 mismatch: got 0x%016lx expected 0x0000000000001010\n", armu.regs[10]);
        status = 1;
    }
    if (armu.regs[11] != 0x1111ULL) {
        fprintf(stderr, "[branch] X11 mismatch: got 0x%016lx expected 0x0000000000001111\n", armu.regs[11]);
        status = 1;
    }
    if (armu.regs[12] != (uint64_t)(I_BRANCH_TARGET * 4)) {
        fprintf(stderr, "[branch] X12 mismatch: got 0x%016lx expected 0x%016lx\n",
                armu.regs[12], (uint64_t)(I_BRANCH_TARGET * 4));
        status = 1;
    }
    if (armu.regs[13] != (uint64_t)(I_SUBROUTINE_START * 4)) {
        fprintf(stderr, "[branch] X13 mismatch: got 0x%016lx expected 0x%016lx\n",
                armu.regs[13], (uint64_t)(I_SUBROUTINE_START * 4));
        status = 1;
    }
    if (armu.regs[14] != 0xABCDULL) {
        fprintf(stderr, "[branch] X14 mismatch: got 0x%016lx expected 0x000000000000ABCD\n", armu.regs[14]);
        status = 1;
    }
    if (armu.regs[15] != 0xEEEEULL) {
        fprintf(stderr, "[branch] X15 mismatch: got 0x%016lx expected 0x000000000000EEEE\n", armu.regs[15]);
        status = 1;
    }
    if (armu.regs[30] != (uint64_t)((I_MOVZ_X11_AFTER_BLR) * 4)) {
        fprintf(stderr, "[branch] X30 mismatch: got 0x%016lx expected 0x%016lx\n",
                armu.regs[30], (uint64_t)((I_MOVZ_X11_AFTER_BLR) * 4));
        status = 1;
    }

    if (status == 0) {
        printf("branching: pass\n");
    }

    return status;
}
