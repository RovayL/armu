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

    while (armu->pc + sizeof(uint32_t) <= armu->program_size) {
        uint64_t pc_before = armu->pc;
        uint32_t insn = fetch(armu);
        struct Da64Inst dinst;
        da64_decode(insn, &dinst);

        switch (dinst.mnem) {
        case DA64I_MOVZ:
            movz(armu, &dinst);
            break;
        case DA64I_MOVN:
            movn(armu, &dinst);
            break;
        case DA64I_MOVK:
            movk(armu, &dinst);
            break;
        case DA64I_ADD_IMM:
            add_imm(armu, &dinst);
            break;
        case DA64I_ADDS_IMM:
            adds_imm(armu, &dinst);
            break;
        case DA64I_SUB_IMM:
            sub_imm(armu, &dinst);
            break;
        case DA64I_SUBS_IMM:
            subs_imm(armu, &dinst);
            break;
        case DA64I_ADD_SHIFT:
            add_shift(armu, &dinst);
            break;
        case DA64I_ADDS_SHIFT:
            adds_shift(armu, &dinst);
            break;
        case DA64I_SUB_SHIFT:
            sub_shift(armu, &dinst);
            break;
        case DA64I_SUBS_SHIFT:
            subs_shift(armu, &dinst);
            break;
        case DA64I_ADDS_EXT:
            adds_ext(armu, &dinst);
            break;
        case DA64I_SUB_EXT:
            sub_ext(armu, &dinst);
            break;
        case DA64I_SUBS_EXT:
            subs_ext(armu, &dinst);
            break;
        case DA64I_ADC:
            adc(armu, &dinst);
            break;
        case DA64I_ADCS:
            adcs(armu, &dinst);
            break;
        case DA64I_SBC:
            sbc(armu, &dinst);
            break;
        case DA64I_SBCS:
            sbcs(armu, &dinst);
            break;
        case DA64I_AND_IMM:
            and_imm(armu, &dinst);
            break;
        case DA64I_ANDS_IMM:
            ands_imm(armu, &dinst);
            break;
        case DA64I_ORR_IMM:
            orr_imm(armu, &dinst);
            break;
        case DA64I_EOR_IMM:
            eor_imm(armu, &dinst);
            break;
        case DA64I_AND_SHIFT:
            and_shift(armu, &dinst);
            break;
        case DA64I_BIC_SHIFT:
            bic_shift(armu, &dinst);
            break;
        case DA64I_ORR_SHIFT:
            orr_shift(armu, &dinst);
            break;
        case DA64I_ORN_SHIFT:
            orn_shift(armu, &dinst);
            break;
        case DA64I_EOR_SHIFT:
            eor_shift(armu, &dinst);
            break;
        case DA64I_EON_SHIFT:
            eon_shift(armu, &dinst);
            break;
        case DA64I_ANDS_SHIFT:
            ands_shift(armu, &dinst);
            break;
        case DA64I_BICS_SHIFT:
            bics_shift(armu, &dinst);
            break;
        case DA64I_CBZ:
            cbz(armu, &dinst);
            break;
        case DA64I_CBNZ:
            cbnz(armu, &dinst);
            break;
        case DA64I_TBZ:
            tbz(armu, &dinst);
            break;
        case DA64I_TBNZ:
            tbnz(armu, &dinst);
            break;
        case DA64I_BCOND:
            bcond(armu, &dinst);
            break;
        case DA64I_B:
            b(armu, &dinst);
            break;
        case DA64I_BL:
            bl(armu, &dinst);
            break;
        case DA64I_BR:
            br(armu, &dinst);
            break;
        case DA64I_BLR:
            blr(armu, &dinst);
            break;
        case DA64I_RET:
            ret(armu, &dinst);
            break;
        case DA64I_BRK:
            brk(armu, &dinst);
            break;
        case DA64I_STR_IMM:
            str_imm(armu, &dinst);
            break;
        case DA64I_LDR_IMM:
            ldr_imm(armu, &dinst);
            break;
        case DA64I_STRW_IMM:
            strw_imm(armu, &dinst);
            break;
        case DA64I_LDRW_IMM:
            ldrw_imm(armu, &dinst);
            break;
        case DA64I_STRB_IMM:
            strb_imm(armu, &dinst);
            break;
        case DA64I_LDRB_IMM:
            ldrb_imm(armu, &dinst);
            break;
        case DA64I_LDRSB_IMM:
            ldrsb_imm(armu, &dinst);
            break;
        case DA64I_LDRSBW_IMM:
            ldrsbw_imm(armu, &dinst);
            break;
        case DA64I_STRH_IMM:
            strh_imm(armu, &dinst);
            break;
        case DA64I_LDRH_IMM:
            ldrh_imm(armu, &dinst);
            break;
        case DA64I_LDRSH_IMM:
            ldrsh_imm(armu, &dinst);
            break;
        case DA64I_LDRSHW_IMM:
            ldrshw_imm(armu, &dinst);
            break;
        case DA64I_LDRSW_IMM:
            ldrsw_imm(armu, &dinst);
            break;
        case DA64I_LSLV:
            lslv(armu, &dinst);
            break;
        case DA64I_LSRV:
            lsrv(armu, &dinst);
            break;
        case DA64I_ASRV:
            asrv(armu, &dinst);
            break;
        case DA64I_RORV:
            rorv(armu, &dinst);
            break;
        case DA64I_STURX:
            sturx(armu, &dinst);
            break;
        case DA64I_STRX_POST:
            strx_post(armu, &dinst);
            break;
        case DA64I_STRX_PRE:
            strx_pre(armu, &dinst);
            break;
        case DA64I_LDURX:
            ldurx(armu, &dinst);
            break;
        case DA64I_LDRX_POST:
            ldrx_post(armu, &dinst);
            break;
        case DA64I_LDRX_PRE:
            ldrx_pre(armu, &dinst);
            break;
        case DA64I_STURW:
            sturw(armu, &dinst);
            break;
        case DA64I_STRW_POST:
            strw_post(armu, &dinst);
            break;
        case DA64I_STRW_PRE:
            strw_pre(armu, &dinst);
            break;
        case DA64I_LDURW:
            ldurw(armu, &dinst);
            break;
        case DA64I_LDRW_POST:
            ldrw_post(armu, &dinst);
            break;
        case DA64I_LDRW_PRE:
            ldrw_pre(armu, &dinst);
            break;
        case DA64I_STURB:
            sturb(armu, &dinst);
            break;
        case DA64I_STRB_POST:
            strb_post(armu, &dinst);
            break;
        case DA64I_STRB_PRE:
            strb_pre(armu, &dinst);
            break;
        case DA64I_LDURB:
            ldurb(armu, &dinst);
            break;
        case DA64I_LDRB_POST:
            ldrb_post(armu, &dinst);
            break;
        case DA64I_LDRB_PRE:
            ldrb_pre(armu, &dinst);
            break;
        case DA64I_LDURSB:
            ldursb(armu, &dinst);
            break;
        case DA64I_LDRSB_POST:
            ldrsb_post(armu, &dinst);
            break;
        case DA64I_LDRSB_PRE:
            ldrsb_pre(armu, &dinst);
            break;
        case DA64I_LDURSBW:
            ldursbw(armu, &dinst);
            break;
        case DA64I_LDRSBW_POST:
            ldrsbw_post(armu, &dinst);
            break;
        case DA64I_LDRSBW_PRE:
            ldrsbw_pre(armu, &dinst);
            break;
        case DA64I_STURH:
            sturh(armu, &dinst);
            break;
        case DA64I_STRH_POST:
            strh_post(armu, &dinst);
            break;
        case DA64I_STRH_PRE:
            strh_pre(armu, &dinst);
            break;
        case DA64I_LDURH:
            ldurh(armu, &dinst);
            break;
        case DA64I_LDRH_POST:
            ldrh_post(armu, &dinst);
            break;
        case DA64I_LDRH_PRE:
            ldrh_pre(armu, &dinst);
            break;
        case DA64I_LDURSH:
            ldursh(armu, &dinst);
            break;
        case DA64I_LDRSH_POST:
            ldrsh_post(armu, &dinst);
            break;
        case DA64I_LDRSH_PRE:
            ldrsh_pre(armu, &dinst);
            break;
        case DA64I_LDURSHW:
            ldurshw(armu, &dinst);
            break;
        case DA64I_LDRSHW_POST:
            ldrshw_post(armu, &dinst);
            break;
        case DA64I_LDRSHW_PRE:
            ldrshw_pre(armu, &dinst);
            break;
        case DA64I_LDURSW:
            ldursw(armu, &dinst);
            break;
        case DA64I_STRB_REG:
            strb_reg(armu, &dinst);
            break;
        case DA64I_LDRB_REG:
            ldrb_reg(armu, &dinst);
            break;
        case DA64I_LDRSB_REG:
            ldrsb_reg(armu, &dinst);
            break;
        case DA64I_LDRSBW_REG:
            ldrsbw_reg(armu, &dinst);
            break;
        case DA64I_STRH_REG:
            strh_reg(armu, &dinst);
            break;
        case DA64I_LDRH_REG:
            ldrh_reg(armu, &dinst);
            break;
        case DA64I_LDRSH_REG:
            ldrsh_reg(armu, &dinst);
            break;
        case DA64I_LDRSHW_REG:
            ldrshw_reg(armu, &dinst);
            break;
        case DA64I_LDRSW_POST:
            ldrsw_post(armu, &dinst);
            break;
        case DA64I_LDRSW_PRE:
            ldrsw_pre(armu, &dinst);
            break;
        case DA64I_LDRSW_REG:
            ldrsw_reg(armu, &dinst);
            break;
        case DA64I_STPX_POST:
            stpx_post(armu, &dinst);
            break;
        case DA64I_STPX:
            stpx(armu, &dinst);
            break;
        case DA64I_STPX_PRE:
            stpx_pre(armu, &dinst);
            break;
        case DA64I_LDPX_POST:
            ldpx_post(armu, &dinst);
            break;
        case DA64I_LDPX:
            ldpx(armu, &dinst);
            break;
        case DA64I_LDPX_PRE:
            ldpx_pre(armu, &dinst);
            break;
        case DA64I_STPW_POST:
            stpw_post(armu, &dinst);
            break;
        case DA64I_STPW:
            stpw(armu, &dinst);
            break;
        case DA64I_STPW_PRE:
            stpw_pre(armu, &dinst);
            break;
        case DA64I_LDPW_POST:
            ldpw_post(armu, &dinst);
            break;
        case DA64I_LDPW:
            ldpw(armu, &dinst);
            break;
        case DA64I_LDPW_PRE:
            ldpw_pre(armu, &dinst);
            break;
        case DA64I_LDPSW_POST:
            ldpsw_post(armu, &dinst);
            break;
        case DA64I_LDPSW:
            ldpsw(armu, &dinst);
            break;
        case DA64I_LDPSW_PRE:
            ldpsw_pre(armu, &dinst);
            break;
        case DA64I_STR_REG:
            str_reg(armu, &dinst);
            break;
        case DA64I_LDR_REG:
            ldr_reg(armu, &dinst);
            break;
        case DA64I_STRW_REG:
            strw_reg(armu, &dinst);
            break;
        case DA64I_LDRW_REG:
            ldrw_reg(armu, &dinst);
            break;
        case DA64I_ADD_EXT:
            add_ext(armu, &dinst);
            break;
        default:
            assert(!"unknown instruction");
            return;
        }

        if (armu->pc == pc_before) {
            armu->pc += sizeof(uint32_t);
        }
    }
}
