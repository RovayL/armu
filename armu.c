#include <assert.h>
#include <string.h>

#include "armu.h"
#include "disarm64.h"

#include "cpu.h"

static uint32_t
fetch(struct Armu* armu)
{
    assert(armu->mem != NULL);
    assert(armu->program_size <= armu->mem_capacity);
    assert(armu->pc + sizeof(uint32_t) <= armu->program_size);
    uint32_t insn = 0;
    memcpy(&insn, armu->mem + armu->pc, sizeof insn);
    return insn;
}

void
armu_run(struct Armu* armu)
{
    assert(armu->mem != NULL);
    assert(armu->program_size <= armu->mem_capacity);

#if !defined(__GNUC__)
#error "Computed goto dispatch requires GCC or Clang."
#endif

    enum { ARMU_DISPATCH_TABLE_SIZE = 0x7080 + 1 };

#define ARMU_OPCODE_LIST(OP)                                               \
    OP(DA64I_MOVZ, movz)                                                   \
    OP(DA64I_MOVN, movn)                                                   \
    OP(DA64I_MOVK, movk)                                                   \
    OP(DA64I_ADD_IMM, add_imm)                                             \
    OP(DA64I_ADDS_IMM, adds_imm)                                           \
    OP(DA64I_SUB_IMM, sub_imm)                                             \
    OP(DA64I_SUBS_IMM, subs_imm)                                           \
    OP(DA64I_ADD_SHIFT, add_shift)                                         \
    OP(DA64I_ADDS_SHIFT, adds_shift)                                       \
    OP(DA64I_SUB_SHIFT, sub_shift)                                         \
    OP(DA64I_SUBS_SHIFT, subs_shift)                                       \
    OP(DA64I_ADDS_EXT, adds_ext)                                           \
    OP(DA64I_SUB_EXT, sub_ext)                                             \
    OP(DA64I_SUBS_EXT, subs_ext)                                           \
    OP(DA64I_ADC, adc)                                                     \
    OP(DA64I_ADCS, adcs)                                                   \
    OP(DA64I_SBC, sbc)                                                     \
    OP(DA64I_SBCS, sbcs)                                                   \
    OP(DA64I_AND_IMM, and_imm)                                             \
    OP(DA64I_ANDS_IMM, ands_imm)                                           \
    OP(DA64I_ORR_IMM, orr_imm)                                             \
    OP(DA64I_EOR_IMM, eor_imm)                                             \
    OP(DA64I_AND_SHIFT, and_shift)                                         \
    OP(DA64I_BIC_SHIFT, bic_shift)                                         \
    OP(DA64I_ORR_SHIFT, orr_shift)                                         \
    OP(DA64I_ORN_SHIFT, orn_shift)                                         \
    OP(DA64I_EOR_SHIFT, eor_shift)                                         \
    OP(DA64I_EON_SHIFT, eon_shift)                                         \
    OP(DA64I_ANDS_SHIFT, ands_shift)                                       \
    OP(DA64I_BICS_SHIFT, bics_shift)                                       \
    OP(DA64I_CBZ, cbz)                                                     \
    OP(DA64I_CBNZ, cbnz)                                                   \
    OP(DA64I_TBZ, tbz)                                                     \
    OP(DA64I_TBNZ, tbnz)                                                   \
    OP(DA64I_BCOND, bcond)                                                 \
    OP(DA64I_B, b)                                                         \
    OP(DA64I_BL, bl)                                                       \
    OP(DA64I_BR, br)                                                       \
    OP(DA64I_BLR, blr)                                                     \
    OP(DA64I_RET, ret)                                                     \
    OP(DA64I_BRK, brk)                                                     \
    OP(DA64I_STR_IMM, str_imm)                                             \
    OP(DA64I_LDR_IMM, ldr_imm)                                             \
    OP(DA64I_STRW_IMM, strw_imm)                                           \
    OP(DA64I_LDRW_IMM, ldrw_imm)                                           \
    OP(DA64I_STRB_IMM, strb_imm)                                           \
    OP(DA64I_LDRB_IMM, ldrb_imm)                                           \
    OP(DA64I_LDRSB_IMM, ldrsb_imm)                                         \
    OP(DA64I_LDRSBW_IMM, ldrsbw_imm)                                       \
    OP(DA64I_STRH_IMM, strh_imm)                                           \
    OP(DA64I_LDRH_IMM, ldrh_imm)                                           \
    OP(DA64I_LDRSH_IMM, ldrsh_imm)                                         \
    OP(DA64I_LDRSHW_IMM, ldrshw_imm)                                       \
    OP(DA64I_LDRSW_IMM, ldrsw_imm)                                         \
    OP(DA64I_LSLV, lslv)                                                   \
    OP(DA64I_LSRV, lsrv)                                                   \
    OP(DA64I_ASRV, asrv)                                                   \
    OP(DA64I_RORV, rorv)                                                   \
    OP(DA64I_ADR, adr)                                                     \
    OP(DA64I_ADRP, adrp)                                                   \
    OP(DA64I_MADD, madd)                                                   \
    OP(DA64I_MSUB, msub)                                                   \
    OP(DA64I_SMADDL, smaddl)                                               \
    OP(DA64I_SMSUBL, smsubl)                                               \
    OP(DA64I_UMADDL, umaddl)                                               \
    OP(DA64I_UMSUBL, umsubl)                                               \
    OP(DA64I_SMULH, smulh)                                                 \
    OP(DA64I_UMULH, umulh)                                                 \
    OP(DA64I_CCMN_IMM, ccmn_imm)                                           \
    OP(DA64I_CCMP_IMM, ccmp_imm)                                           \
    OP(DA64I_CCMN_REG, ccmn_reg)                                           \
    OP(DA64I_CCMP_REG, ccmp_reg)                                           \
    OP(DA64I_CLZ, clz)                                                     \
    OP(DA64I_CLS, cls)                                                     \
    OP(DA64I_CSEL, csel)                                                   \
    OP(DA64I_CSINC, csinc)                                                 \
    OP(DA64I_CSINV, csinv)                                                 \
    OP(DA64I_CSNEG, csneg)                                                 \
    OP(DA64I_EXTR, extr)                                                   \
    OP(DA64I_UDIV, udiv)                                                   \
    OP(DA64I_SDIV, sdiv)                                                   \
    OP(DA64I_STURX, sturx)                                                 \
    OP(DA64I_STRX_POST, strx_post)                                         \
    OP(DA64I_STRX_PRE, strx_pre)                                           \
    OP(DA64I_LDURX, ldurx)                                                 \
    OP(DA64I_LDRX_POST, ldrx_post)                                         \
    OP(DA64I_LDRX_PRE, ldrx_pre)                                           \
    OP(DA64I_STURW, sturw)                                                 \
    OP(DA64I_STRW_POST, strw_post)                                         \
    OP(DA64I_STRW_PRE, strw_pre)                                           \
    OP(DA64I_LDURW, ldurw)                                                 \
    OP(DA64I_LDRW_POST, ldrw_post)                                         \
    OP(DA64I_LDRW_PRE, ldrw_pre)                                           \
    OP(DA64I_STURB, sturb)                                                 \
    OP(DA64I_STRB_POST, strb_post)                                         \
    OP(DA64I_STRB_PRE, strb_pre)                                           \
    OP(DA64I_LDURB, ldurb)                                                 \
    OP(DA64I_LDRB_POST, ldrb_post)                                         \
    OP(DA64I_LDRB_PRE, ldrb_pre)                                           \
    OP(DA64I_LDURSB, ldursb)                                               \
    OP(DA64I_LDRSB_POST, ldrsb_post)                                       \
    OP(DA64I_LDRSB_PRE, ldrsb_pre)                                         \
    OP(DA64I_LDURSBW, ldursbw)                                             \
    OP(DA64I_LDRSBW_POST, ldrsbw_post)                                     \
    OP(DA64I_LDRSBW_PRE, ldrsbw_pre)                                       \
    OP(DA64I_STURH, sturh)                                                 \
    OP(DA64I_STRH_POST, strh_post)                                         \
    OP(DA64I_STRH_PRE, strh_pre)                                           \
    OP(DA64I_LDURH, ldurh)                                                 \
    OP(DA64I_LDRH_POST, ldrh_post)                                         \
    OP(DA64I_LDRH_PRE, ldrh_pre)                                           \
    OP(DA64I_LDURSH, ldursh)                                               \
    OP(DA64I_LDRSH_POST, ldrsh_post)                                       \
    OP(DA64I_LDRSH_PRE, ldrsh_pre)                                         \
    OP(DA64I_LDURSHW, ldurshw)                                             \
    OP(DA64I_LDRSHW_POST, ldrshw_post)                                     \
    OP(DA64I_LDRSHW_PRE, ldrshw_pre)                                       \
    OP(DA64I_LDURSW, ldursw)                                               \
    OP(DA64I_STRB_REG, strb_reg)                                           \
    OP(DA64I_LDRB_REG, ldrb_reg)                                           \
    OP(DA64I_LDRSB_REG, ldrsb_reg)                                         \
    OP(DA64I_LDRSBW_REG, ldrsbw_reg)                                       \
    OP(DA64I_STRH_REG, strh_reg)                                           \
    OP(DA64I_LDRH_REG, ldrh_reg)                                           \
    OP(DA64I_LDRSH_REG, ldrsh_reg)                                         \
    OP(DA64I_LDRSHW_REG, ldrshw_reg)                                       \
    OP(DA64I_LDRSW_POST, ldrsw_post)                                       \
    OP(DA64I_LDRSW_PRE, ldrsw_pre)                                         \
    OP(DA64I_LDRSW_REG, ldrsw_reg)                                         \
    OP(DA64I_STPX_POST, stpx_post)                                         \
    OP(DA64I_STPX, stpx)                                                   \
    OP(DA64I_STPX_PRE, stpx_pre)                                           \
    OP(DA64I_LDPX_POST, ldpx_post)                                         \
    OP(DA64I_LDPX, ldpx)                                                   \
    OP(DA64I_LDPX_PRE, ldpx_pre)                                           \
    OP(DA64I_STPW_POST, stpw_post)                                         \
    OP(DA64I_STPW, stpw)                                                   \
    OP(DA64I_STPW_PRE, stpw_pre)                                           \
    OP(DA64I_LDPW_POST, ldpw_post)                                         \
    OP(DA64I_LDPW, ldpw)                                                   \
    OP(DA64I_LDPW_PRE, ldpw_pre)                                           \
    OP(DA64I_LDPSW_POST, ldpsw_post)                                       \
    OP(DA64I_LDPSW, ldpsw)                                                 \
    OP(DA64I_LDPSW_PRE, ldpsw_pre)                                         \
    OP(DA64I_STR_REG, str_reg)                                             \
    OP(DA64I_LDR_REG, ldr_reg)                                             \
    OP(DA64I_STRW_REG, strw_reg)                                           \
    OP(DA64I_LDRW_REG, ldrw_reg)                                           \
    OP(DA64I_ADD_EXT, add_ext)

    typedef void* ArmDispatchLabel;
    static const ArmDispatchLabel dispatch_table[ARMU_DISPATCH_TABLE_SIZE] = {
        [0 ... ARMU_DISPATCH_TABLE_SIZE - 1] = &&LABEL_unknown,
#define ARMU_DISPATCH_ENTRY(MNEM, FUNC) [MNEM] = &&LABEL_##FUNC,
        ARMU_OPCODE_LIST(ARMU_DISPATCH_ENTRY)
#undef ARMU_DISPATCH_ENTRY
    };

    uint32_t insn = 0;
    uint64_t pc_before = 0;
    struct Da64Inst dinst;

#define ARMU_DISPATCH()                                                       \
    do {                                                                      \
        if (armu->pc + sizeof(uint32_t) > armu->program_size) {               \
            goto HALT;                                                        \
        }                                                                     \
        pc_before = armu->pc;                                                 \
        insn = fetch(armu);                                                   \
        da64_decode(insn, &dinst);                                            \
        if (dinst.mnem >= ARMU_DISPATCH_TABLE_SIZE) {                         \
            goto LABEL_unknown;                                               \
        }                                                                     \
        goto *dispatch_table[dinst.mnem];                                     \
    } while (0)

#define ARMU_DISPATCH_NEXT()                                                  \
    do {                                                                      \
        if (armu->pc == pc_before) {                                          \
            armu->pc += sizeof(uint32_t);                                     \
        }                                                                     \
        ARMU_DISPATCH();                                                      \
    } while (0)

    ARMU_DISPATCH();

#define ARMU_DEFINE_HANDLER(MNEM, FUNC)                                       \
    LABEL_##FUNC:                                                             \
        FUNC(armu, &dinst);                                                   \
        ARMU_DISPATCH_NEXT();

    ARMU_OPCODE_LIST(ARMU_DEFINE_HANDLER)
#undef ARMU_DEFINE_HANDLER

LABEL_unknown:
    assert(!"unknown instruction");
    goto HALT;

HALT:
#undef ARMU_DISPATCH_NEXT
#undef ARMU_DISPATCH
#undef ARMU_OPCODE_LIST
    return;
}
