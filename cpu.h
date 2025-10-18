#include <stdint.h>
#include <string.h>
#include <assert.h>

static inline int op_is_sp(const struct Da64Op* op) {
    return op && op->type == DA_OP_REGSP;
}

static uint64_t get_reg_value(struct Armu* armu, int reg, int is_64bit, int treat_as_sp) {
    if (reg == 31 && !treat_as_sp) {
        return 0;
    }
    uint64_t value = armu->regs[reg];
    return is_64bit ? value : (uint32_t)value;
}

static void set_reg_value(struct Armu* armu, int reg, int is_64bit, int treat_as_sp, uint64_t value) {
    if (reg == 31 && !treat_as_sp) {
        return;
    }
    if (!is_64bit) {
        value &= 0xFFFFFFFFu;
    }
    armu->regs[reg] = value;
}

static void write_memory(struct Armu* armu, uint64_t address, const void* data, size_t size) {
    assert(armu->mem != NULL);
    assert(address + size <= armu->mem_capacity);
    memcpy(armu->mem + address, data, size);
}

static void read_memory(struct Armu* armu, uint64_t address, void* out, size_t size) {
    assert(armu->mem != NULL);
    assert(address + size <= armu->mem_capacity);
    memcpy(out, armu->mem + address, size);
}

static inline unsigned value_width(int is_64bit) {
    return is_64bit ? 64u : 32u;
}

static inline uint64_t mask_for_width(unsigned width) {
    return width >= 64 ? UINT64_MAX : ((UINT64_C(1) << width) - 1);
}

static inline uint64_t zero_extend_bits(uint64_t value, unsigned width) {
    return value & mask_for_width(width);
}

static inline uint64_t sign_extend_bits(uint64_t value, unsigned width) {
    if (width >= 64) {
        return value;
    }
    uint64_t mask = (UINT64_C(1) << width) - 1;
    uint64_t sign_bit = UINT64_C(1) << (width - 1);
    value &= mask;
    return (value ^ sign_bit) - sign_bit;
}

static inline uint64_t logical_shift_left(uint64_t value, unsigned shift, unsigned width) {
    if (shift >= width) {
        return 0;
    }
    uint64_t mask = mask_for_width(width);
    return (value << shift) & mask;
}

static inline uint64_t logical_shift_right(uint64_t value, unsigned shift, unsigned width) {
    if (shift >= width) {
        return 0;
    }
    return zero_extend_bits(value, width) >> shift;
}

static inline uint64_t arithmetic_shift_right(uint64_t value, unsigned shift, unsigned width) {
    uint64_t sign_mask = UINT64_C(1) << (width - 1);
    uint64_t masked = zero_extend_bits(value, width);
    if (shift >= width) {
        return (masked & sign_mask) ? mask_for_width(width) : 0;
    }
    if (masked & sign_mask) {
        uint64_t fill = mask_for_width(width) << (width - shift);
        return (masked >> shift) | fill;
    }
    return masked >> shift;
}

static inline uint64_t rotate_right(uint64_t value, unsigned shift, unsigned width) {
    if (width == 0) {
        return value;
    }
    shift %= width;
    uint64_t masked = zero_extend_bits(value, width);
    if (shift == 0) {
        return masked;
    }
    return ((masked >> shift) | (masked << (width - shift))) & mask_for_width(width);
}

static uint64_t add_with_carry(uint64_t x, uint64_t y, uint64_t carry_in, int is_64bit, uint8_t* carry_out, uint8_t* overflow_out) {
    unsigned width = value_width(is_64bit);
    uint64_t mask = mask_for_width(width);
    __uint128_t sum = (__uint128_t)(x & mask) + (y & mask) + (carry_in & 1);
    uint64_t result = (uint64_t)sum & mask;

    if (carry_out) {
        *carry_out = (sum >> width) & 1;
    }
    if (overflow_out) {
        uint64_t sx = ((x & mask) >> (width - 1)) & 1;
        uint64_t sy = ((y & mask) >> (width - 1)) & 1;
        uint64_t sr = (result >> (width - 1)) & 1;
        *overflow_out = ((sx == sy) && (sx != sr));
    }
    return result;
}

static void update_nzcv(struct Armu* armu, int is_64bit, uint64_t result, uint8_t carry_out, uint8_t overflow_out) {
    unsigned width = value_width(is_64bit);
    uint64_t masked = zero_extend_bits(result, width);
    armu->flags.n = (masked >> (width - 1)) & 1;
    armu->flags.z = (masked == 0);
    armu->flags.c = carry_out;
    armu->flags.v = overflow_out;
}

static uint64_t apply_shift_operand(struct Armu* armu, const struct Da64Op* op, int dest_is_64bit) {
    assert(op->type == DA_OP_REGGPEXT);
    int operand_is_64 = op->reggpext.sf;
    unsigned src_width = value_width(operand_is_64);
    uint64_t value = get_reg_value(armu, op->reg, operand_is_64, 0);
    value = zero_extend_bits(value, src_width);

    unsigned shift = op->reggpext.shift;
    switch (op->reggpext.ext) {
    case DA_EXT_LSL:
        value = logical_shift_left(value, shift, src_width);
        break;
    case DA_EXT_LSR:
        value = logical_shift_right(value, shift, src_width);
        break;
    case DA_EXT_ASR:
        value = arithmetic_shift_right(value, shift, src_width);
        break;
    case DA_EXT_ROR:
        value = rotate_right(value, shift, src_width);
        break;
    default:
        assert(!"unsupported shift extension");
    }
    return zero_extend_bits(value, value_width(dest_is_64bit));
}

static uint64_t apply_extend_operand(struct Armu* armu, const struct Da64Op* op, int dest_is_64bit) {
    assert(op->type == DA_OP_REGGPEXT);
    unsigned bits = 0;
    int sign = 0;
    switch (op->reggpext.ext) {
    case DA_EXT_UXTB: bits = 8; sign = 0; break;
    case DA_EXT_UXTH: bits = 16; sign = 0; break;
    case DA_EXT_UXTW: bits = 32; sign = 0; break;
    case DA_EXT_UXTX: bits = 64; sign = 0; break;
    case DA_EXT_SXTB: bits = 8; sign = 1; break;
    case DA_EXT_SXTH: bits = 16; sign = 1; break;
    case DA_EXT_SXTW: bits = 32; sign = 1; break;
    case DA_EXT_SXTX: bits = 64; sign = 1; break;
    default:
        assert(!"unsupported extend mode");
    }

    uint64_t value = get_reg_value(armu, op->reg, op->reggpext.sf, 0);
    value = sign ? sign_extend_bits(value, bits) : zero_extend_bits(value, bits);
    value <<= op->reggpext.shift;
    return zero_extend_bits(value, value_width(dest_is_64bit));
}

static uint64_t execute_add_with_carry(struct Armu* armu, uint64_t lhs, uint64_t rhs, uint64_t carry_in, int is_64bit, int update_flags) {
    uint8_t carry_out = 0;
    uint8_t overflow_out = 0;
    uint64_t result = add_with_carry(lhs, rhs, carry_in, is_64bit,
                                     update_flags ? &carry_out : NULL,
                                     update_flags ? &overflow_out : NULL);
    if (update_flags) {
        update_nzcv(armu, is_64bit, result, carry_out, overflow_out);
    }
    return result;
}

static void update_nz(struct Armu* armu, int is_64bit, uint64_t result) {
    unsigned width = value_width(is_64bit);
    uint64_t masked = zero_extend_bits(result, width);
    armu->flags.n = (masked >> (width - 1)) & 1;
    armu->flags.z = (masked == 0);
}

static int condition_passed(const struct Armu* armu, unsigned cond) {
    switch (cond & 0xf) {
    case DA_EQ: return armu->flags.z;
    case DA_NE: return !armu->flags.z;
    case DA_CS: return armu->flags.c;
    case DA_CC: return !armu->flags.c;
    case DA_MI: return armu->flags.n;
    case DA_PL: return !armu->flags.n;
    case DA_VS: return armu->flags.v;
    case DA_VC: return !armu->flags.v;
    case DA_HI: return armu->flags.c && !armu->flags.z;
    case DA_LS: return !armu->flags.c || armu->flags.z;
    case DA_GE: return armu->flags.n == armu->flags.v;
    case DA_LT: return armu->flags.n != armu->flags.v;
    case DA_GT: return !armu->flags.z && (armu->flags.n == armu->flags.v);
    case DA_LE: return armu->flags.z || (armu->flags.n != armu->flags.v);
    case DA_AL: return 1;
    case DA_NV: return 0;
    default: return 0;
    }
}

static uint64_t read_base_address(struct Armu* armu, const struct Da64Op* mem_op, int* base_idx_out, int* base_is_sp_out) {
    int rn_idx = mem_op->reg;
    int base_is_sp = (rn_idx == 31);
    if (base_idx_out) {
        *base_idx_out = rn_idx;
    }
    if (base_is_sp_out) {
        *base_is_sp_out = base_is_sp;
    }
    return get_reg_value(armu, rn_idx, 1, base_is_sp);
}

static void write_back_base(struct Armu* armu, int rn_idx, int base_is_sp, uint64_t value) {
    set_reg_value(armu, rn_idx, 1, base_is_sp, value);
}

static uint64_t compute_memreg_offset(struct Armu* armu, const struct Da64Op* mem_op) {
    enum Da64Ext ext = (enum Da64Ext)mem_op->memreg.ext;
    unsigned shift = mem_op->memreg.shift;
    int offreg = mem_op->memreg.offreg;
    unsigned bits = 64;
    int sign = 0;

    switch (ext) {
    case DA_EXT_UXTB: bits = 8; sign = 0; break;
    case DA_EXT_UXTH: bits = 16; sign = 0; break;
    case DA_EXT_UXTW: bits = 32; sign = 0; break;
    case DA_EXT_UXTX: bits = 64; sign = 0; break;
    case DA_EXT_SXTB: bits = 8; sign = 1; break;
    case DA_EXT_SXTH: bits = 16; sign = 1; break;
    case DA_EXT_SXTW: bits = 32; sign = 1; break;
    case DA_EXT_SXTX: bits = 64; sign = 1; break;
    default: break;
    }

    int operand_is_64 = bits > 32;
    uint64_t value = get_reg_value(armu, offreg, operand_is_64, 0);
    value = sign ? sign_extend_bits(value, bits) : zero_extend_bits(value, bits);
    value <<= shift;
    return value;
}

static uint64_t get_store_reg_scalar(struct Armu* armu, const struct Da64Op* reg_op, unsigned size) {
    int is_64bit = reg_op->reggp.sf;
    uint64_t value = get_reg_value(armu, reg_op->reg, is_64bit, op_is_sp(reg_op));
    return zero_extend_bits(value, size * 8);
}

static void maybe_writeback(struct Armu* armu, const struct Da64Op* mem_op, int base_idx, int base_is_sp, uint64_t writeback) {
    switch (mem_op->type) {
    case DA_OP_MEMSOFFPRE:
    case DA_OP_MEMSOFFPOST:
    case DA_OP_MEMREGPOST:
    case DA_OP_MEMINC:
        write_back_base(armu, base_idx, base_is_sp, writeback);
        break;
    default:
        break;
    }
}

static void store_scalar_value(struct Armu* armu, uint64_t address, unsigned size, uint64_t value) {
    switch (size) {
    case 1: {
        uint8_t tmp = (uint8_t)value;
        write_memory(armu, address, &tmp, sizeof(tmp));
        break;
    }
    case 2: {
        uint16_t tmp = (uint16_t)value;
        write_memory(armu, address, &tmp, sizeof(tmp));
        break;
    }
    case 4: {
        uint32_t tmp = (uint32_t)value;
        write_memory(armu, address, &tmp, sizeof(tmp));
        break;
    }
    case 8: {
        uint64_t tmp = value;
        write_memory(armu, address, &tmp, sizeof(tmp));
        break;
    }
    default:
        assert(!"unsupported store size");
    }
}

static uint64_t load_scalar_value(struct Armu* armu, uint64_t address, unsigned size, int sign_extend, int dest_is_64bit) {
    uint64_t raw = 0;
    switch (size) {
    case 1: {
        uint8_t tmp = 0;
        read_memory(armu, address, &tmp, sizeof(tmp));
        raw = tmp;
        break;
    }
    case 2: {
        uint16_t tmp = 0;
        read_memory(armu, address, &tmp, sizeof(tmp));
        raw = tmp;
        break;
    }
    case 4: {
        uint32_t tmp = 0;
        read_memory(armu, address, &tmp, sizeof(tmp));
        raw = tmp;
        break;
    }
    case 8: {
        uint64_t tmp = 0;
        read_memory(armu, address, &tmp, sizeof(tmp));
        raw = tmp;
        break;
    }
    default:
        assert(!"unsupported load size");
    }

    unsigned bits = size * 8;
    if (sign_extend) {
        raw = sign_extend_bits(raw, bits);
    } else {
        raw = zero_extend_bits(raw, bits);
    }
    return zero_extend_bits(raw, value_width(dest_is_64bit));
}

static uint64_t compute_address_general(struct Armu* armu, const struct Da64Op* mem_op, unsigned access_size, int* base_idx_out, int* base_is_sp_out, uint64_t* writeback_out) {
    int base_idx;
    int base_is_sp;
    uint64_t base = read_base_address(armu, mem_op, &base_idx, &base_is_sp);
    uint64_t address = 0;
    uint64_t writeback = base;

    switch (mem_op->type) {
    case DA_OP_MEMUOFF:
        address = base + mem_op->uimm16;
        break;
    case DA_OP_MEMSOFF:
        address = base + mem_op->simm16;
        break;
    case DA_OP_MEMSOFFPRE:
        address = base + mem_op->simm16;
        writeback = address;
        break;
    case DA_OP_MEMSOFFPOST:
        address = base;
        writeback = base + mem_op->simm16;
        break;
    case DA_OP_MEMREG:
        address = base + compute_memreg_offset(armu, mem_op);
        break;
    case DA_OP_MEMREGPOST: {
        uint64_t offset = compute_memreg_offset(armu, mem_op);
        address = base + offset;
        writeback = address;
        break;
    }
    case DA_OP_MEMINC: {
        address = base;
        uint64_t offset = mem_op->uimm16;
        if (!mem_op->memreg.sc) {
            offset *= access_size;
        }
        writeback = base + offset;
        break;
    }
    default:
        assert(!"unsupported addressing mode");
    }

    if (base_idx_out) {
        *base_idx_out = base_idx;
    }
    if (base_is_sp_out) {
        *base_is_sp_out = base_is_sp;
    }
    if (writeback_out) {
        *writeback_out = writeback;
    }
    return address;
}

static void store_pair_common(struct Armu* armu, struct Da64Inst* dinst, unsigned elem_size) {
    const struct Da64Op* rt0 = &dinst->ops[0];
    const struct Da64Op* rt1 = &dinst->ops[1];
    const struct Da64Op* mem = &dinst->ops[2];

    uint64_t value0 = get_store_reg_scalar(armu, rt0, elem_size);
    uint64_t value1 = get_store_reg_scalar(armu, rt1, elem_size);

    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, mem, elem_size * 2, &base_idx, &base_is_sp, &writeback);

    store_scalar_value(armu, address, elem_size, value0);
    store_scalar_value(armu, address + elem_size, elem_size, value1);

    maybe_writeback(armu, mem, base_idx, base_is_sp, writeback);
}

static void load_pair_common(struct Armu* armu, struct Da64Inst* dinst, unsigned elem_size, int sign_extend) {
    const struct Da64Op* rd0 = &dinst->ops[0];
    const struct Da64Op* rd1 = &dinst->ops[1];
    const struct Da64Op* mem = &dinst->ops[2];

    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, mem, elem_size * 2, &base_idx, &base_is_sp, &writeback);

    int dest0_is_64 = sign_extend ? 1 : rd0->reggp.sf;
    int dest1_is_64 = sign_extend ? 1 : rd1->reggp.sf;

    uint64_t value0 = load_scalar_value(armu, address, elem_size, sign_extend, dest0_is_64);
    uint64_t value1 = load_scalar_value(armu, address + elem_size, elem_size, sign_extend, dest1_is_64);

    set_reg_value(armu, rd0->reg, dest0_is_64, op_is_sp(rd0), value0);
    set_reg_value(armu, rd1->reg, dest1_is_64, op_is_sp(rd1), value1);

    maybe_writeback(armu, mem, base_idx, base_is_sp, writeback);
}

static void
udf(struct Armu* armu, struct Da64Inst* dinst)
{

}
static void
adc(struct Armu* armu, struct Da64Inst* dinst)
{
    int rd_idx = dinst->ops[0].reg;
    int is_64bit = dinst->ops[0].reggp.sf;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int rn_idx = dinst->ops[1].reg;
    int rn_is_sp = op_is_sp(&dinst->ops[1]);
    int rm_idx = dinst->ops[2].reg;
    int rm_is_sp = op_is_sp(&dinst->ops[2]);

    uint64_t lhs = get_reg_value(armu, rn_idx, is_64bit, rn_is_sp);
    uint64_t rhs = get_reg_value(armu, rm_idx, is_64bit, rm_is_sp);
    uint64_t carry_in = armu->flags.c ? 1 : 0;
    uint64_t result = execute_add_with_carry(armu, lhs, rhs, carry_in, is_64bit, 0);

    set_reg_value(armu, rd_idx, is_64bit, rd_is_sp, result);
}
static void
adcs(struct Armu* armu, struct Da64Inst* dinst)
{
    int rd_idx = dinst->ops[0].reg;
    int is_64bit = dinst->ops[0].reggp.sf;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int rn_idx = dinst->ops[1].reg;
    int rn_is_sp = op_is_sp(&dinst->ops[1]);
    int rm_idx = dinst->ops[2].reg;
    int rm_is_sp = op_is_sp(&dinst->ops[2]);

    uint64_t lhs = get_reg_value(armu, rn_idx, is_64bit, rn_is_sp);
    uint64_t rhs = get_reg_value(armu, rm_idx, is_64bit, rm_is_sp);
    uint64_t carry_in = armu->flags.c ? 1 : 0;
    uint64_t result = execute_add_with_carry(armu, lhs, rhs, carry_in, is_64bit, 1);

    set_reg_value(armu, rd_idx, is_64bit, rd_is_sp, result);
}
static void
sbc(struct Armu* armu, struct Da64Inst* dinst)
{
    int rd_idx = dinst->ops[0].reg;
    int is_64bit = dinst->ops[0].reggp.sf;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int rn_idx = dinst->ops[1].reg;
    int rn_is_sp = op_is_sp(&dinst->ops[1]);
    int rm_idx = dinst->ops[2].reg;
    int rm_is_sp = op_is_sp(&dinst->ops[2]);

    uint64_t lhs = get_reg_value(armu, rn_idx, is_64bit, rn_is_sp);
    uint64_t rhs = get_reg_value(armu, rm_idx, is_64bit, rm_is_sp);
    uint64_t carry_in = armu->flags.c ? 1 : 0;
    uint64_t result = execute_add_with_carry(armu, lhs, ~rhs, carry_in, is_64bit, 0);

    set_reg_value(armu, rd_idx, is_64bit, rd_is_sp, result);
}
static void
sbcs(struct Armu* armu, struct Da64Inst* dinst)
{
    int rd_idx = dinst->ops[0].reg;
    int is_64bit = dinst->ops[0].reggp.sf;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int rn_idx = dinst->ops[1].reg;
    int rn_is_sp = op_is_sp(&dinst->ops[1]);
    int rm_idx = dinst->ops[2].reg;
    int rm_is_sp = op_is_sp(&dinst->ops[2]);

    uint64_t lhs = get_reg_value(armu, rn_idx, is_64bit, rn_is_sp);
    uint64_t rhs = get_reg_value(armu, rm_idx, is_64bit, rm_is_sp);
    uint64_t carry_in = armu->flags.c ? 1 : 0;
    uint64_t result = execute_add_with_carry(armu, lhs, ~rhs, carry_in, is_64bit, 1);

    set_reg_value(armu, rd_idx, is_64bit, rd_is_sp, result);
}
static void
add_ext(struct Armu* armu, struct Da64Inst* dinst)
{
    int rd_idx = dinst->ops[0].reg;
    int is_64bit = dinst->ops[0].reggp.sf;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int rn_idx = dinst->ops[1].reg;
    int rn_is_sp = op_is_sp(&dinst->ops[1]);

    uint64_t lhs = get_reg_value(armu, rn_idx, is_64bit, rn_is_sp);
    uint64_t operand = apply_extend_operand(armu, &dinst->ops[2], is_64bit);
    uint64_t result = execute_add_with_carry(armu, lhs, operand, 0, is_64bit, 0);

    set_reg_value(armu, rd_idx, is_64bit, rd_is_sp, result);
}
static void
adds_ext(struct Armu* armu, struct Da64Inst* dinst)
{
    int rd_idx = dinst->ops[0].reg;
    int is_64bit = dinst->ops[0].reggp.sf;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int rn_idx = dinst->ops[1].reg;
    int rn_is_sp = op_is_sp(&dinst->ops[1]);

    uint64_t lhs = get_reg_value(armu, rn_idx, is_64bit, rn_is_sp);
    uint64_t operand = apply_extend_operand(armu, &dinst->ops[2], is_64bit);
    uint64_t result = execute_add_with_carry(armu, lhs, operand, 0, is_64bit, 1);

    set_reg_value(armu, rd_idx, is_64bit, rd_is_sp, result);
}
static void
sub_ext(struct Armu* armu, struct Da64Inst* dinst)
{
    int rd_idx = dinst->ops[0].reg;
    int is_64bit = dinst->ops[0].reggp.sf;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int rn_idx = dinst->ops[1].reg;
    int rn_is_sp = op_is_sp(&dinst->ops[1]);

    uint64_t lhs = get_reg_value(armu, rn_idx, is_64bit, rn_is_sp);
    uint64_t operand = apply_extend_operand(armu, &dinst->ops[2], is_64bit);
    uint64_t result = execute_add_with_carry(armu, lhs, ~operand, 1, is_64bit, 0);

    set_reg_value(armu, rd_idx, is_64bit, rd_is_sp, result);
}
static void
subs_ext(struct Armu* armu, struct Da64Inst* dinst)
{
    int rd_idx = dinst->ops[0].reg;
    int is_64bit = dinst->ops[0].reggp.sf;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int rn_idx = dinst->ops[1].reg;
    int rn_is_sp = op_is_sp(&dinst->ops[1]);

    uint64_t lhs = get_reg_value(armu, rn_idx, is_64bit, rn_is_sp);
    uint64_t operand = apply_extend_operand(armu, &dinst->ops[2], is_64bit);
    uint64_t result = execute_add_with_carry(armu, lhs, ~operand, 1, is_64bit, 1);

    set_reg_value(armu, rd_idx, is_64bit, rd_is_sp, result);
}
static void
add_imm(struct Armu* armu, struct Da64Inst* dinst)
{
    int rd_idx = dinst->ops[0].reg;
    int is_64bit = dinst->ops[0].reggp.sf;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int rn_idx = dinst->ops[1].reg;
    int rn_is_sp = op_is_sp(&dinst->ops[1]);
    uint64_t imm = dinst->ops[2].uimm16;
    uint64_t shift = dinst->ops[2].immshift.shift;

    uint64_t lhs = get_reg_value(armu, rn_idx, is_64bit, rn_is_sp);
    uint64_t rhs = imm << shift;
    uint64_t result = execute_add_with_carry(armu, lhs, rhs, 0, is_64bit, 0);

    set_reg_value(armu, rd_idx, is_64bit, rd_is_sp, result);
}
static void
adds_imm(struct Armu* armu, struct Da64Inst* dinst)
{
    int rd_idx = dinst->ops[0].reg;
    int is_64bit = dinst->ops[0].reggp.sf;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int rn_idx = dinst->ops[1].reg;
    int rn_is_sp = op_is_sp(&dinst->ops[1]);
    uint64_t imm = dinst->ops[2].uimm16;
    uint64_t shift = dinst->ops[2].immshift.shift;

    uint64_t lhs = get_reg_value(armu, rn_idx, is_64bit, rn_is_sp);
    uint64_t rhs = imm << shift;
    uint64_t result = execute_add_with_carry(armu, lhs, rhs, 0, is_64bit, 1);

    set_reg_value(armu, rd_idx, is_64bit, rd_is_sp, result);
}
static void
sub_imm(struct Armu* armu, struct Da64Inst* dinst)
{
    int rd_idx = dinst->ops[0].reg;
    int is_64bit = dinst->ops[0].reggp.sf;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int rn_idx = dinst->ops[1].reg;
    int rn_is_sp = op_is_sp(&dinst->ops[1]);
    uint64_t imm = dinst->ops[2].uimm16;
    uint64_t shift = dinst->ops[2].immshift.shift;

    uint64_t lhs = get_reg_value(armu, rn_idx, is_64bit, rn_is_sp);
    uint64_t rhs = imm << shift;
    uint64_t result = execute_add_with_carry(armu, lhs, ~rhs, 1, is_64bit, 0);

    set_reg_value(armu, rd_idx, is_64bit, rd_is_sp, result);
}
static void
subs_imm(struct Armu* armu, struct Da64Inst* dinst)
{
    int rd_idx = dinst->ops[0].reg;
    int is_64bit = dinst->ops[0].reggp.sf;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int rn_idx = dinst->ops[1].reg;
    int rn_is_sp = op_is_sp(&dinst->ops[1]);
    uint64_t imm = dinst->ops[2].uimm16;
    uint64_t shift = dinst->ops[2].immshift.shift;

    uint64_t lhs = get_reg_value(armu, rn_idx, is_64bit, rn_is_sp);
    uint64_t rhs = imm << shift;
    uint64_t result = execute_add_with_carry(armu, lhs, ~rhs, 1, is_64bit, 1);

    set_reg_value(armu, rd_idx, is_64bit, rd_is_sp, result);
}
static void
add_shift(struct Armu* armu, struct Da64Inst* dinst)
{
    int rd_idx = dinst->ops[0].reg;
    int is_64bit = dinst->ops[0].reggp.sf;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int rn_idx = dinst->ops[1].reg;
    int rn_is_sp = op_is_sp(&dinst->ops[1]);

    uint64_t lhs = get_reg_value(armu, rn_idx, is_64bit, rn_is_sp);
    uint64_t operand = apply_shift_operand(armu, &dinst->ops[2], is_64bit);
    uint64_t result = execute_add_with_carry(armu, lhs, operand, 0, is_64bit, 0);

    set_reg_value(armu, rd_idx, is_64bit, rd_is_sp, result);
}
static void
adds_shift(struct Armu* armu, struct Da64Inst* dinst)
{
    int rd_idx = dinst->ops[0].reg;
    int is_64bit = dinst->ops[0].reggp.sf;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int rn_idx = dinst->ops[1].reg;
    int rn_is_sp = op_is_sp(&dinst->ops[1]);

    uint64_t lhs = get_reg_value(armu, rn_idx, is_64bit, rn_is_sp);
    uint64_t operand = apply_shift_operand(armu, &dinst->ops[2], is_64bit);
    uint64_t result = execute_add_with_carry(armu, lhs, operand, 0, is_64bit, 1);

    set_reg_value(armu, rd_idx, is_64bit, rd_is_sp, result);
}
static void
sub_shift(struct Armu* armu, struct Da64Inst* dinst)
{
    int rd_idx = dinst->ops[0].reg;
    int is_64bit = dinst->ops[0].reggp.sf;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int rn_idx = dinst->ops[1].reg;
    int rn_is_sp = op_is_sp(&dinst->ops[1]);

    uint64_t lhs = get_reg_value(armu, rn_idx, is_64bit, rn_is_sp);
    uint64_t operand = apply_shift_operand(armu, &dinst->ops[2], is_64bit);
    uint64_t result = execute_add_with_carry(armu, lhs, ~operand, 1, is_64bit, 0);

    set_reg_value(armu, rd_idx, is_64bit, rd_is_sp, result);
}
static void
subs_shift(struct Armu* armu, struct Da64Inst* dinst)
{
    int rd_idx = dinst->ops[0].reg;
    int is_64bit = dinst->ops[0].reggp.sf;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int rn_idx = dinst->ops[1].reg;
    int rn_is_sp = op_is_sp(&dinst->ops[1]);

    uint64_t lhs = get_reg_value(armu, rn_idx, is_64bit, rn_is_sp);
    uint64_t operand = apply_shift_operand(armu, &dinst->ops[2], is_64bit);
    uint64_t result = execute_add_with_carry(armu, lhs, ~operand, 1, is_64bit, 1);

    set_reg_value(armu, rd_idx, is_64bit, rd_is_sp, result);
}
static void
adr(struct Armu* armu, struct Da64Inst* dinst)
{

}
static void
adrp(struct Armu* armu, struct Da64Inst* dinst)
{

}
static void
and_imm(struct Armu* armu, struct Da64Inst* dinst)
{
    int rd_idx = dinst->ops[0].reg;
    int is_64bit = dinst->ops[0].reggp.sf;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int rn_idx = dinst->ops[1].reg;
    int rn_is_sp = op_is_sp(&dinst->ops[1]);

    uint64_t lhs = get_reg_value(armu, rn_idx, is_64bit, rn_is_sp);
    uint64_t imm = dinst->imm64;
    if (!is_64bit) {
        lhs &= 0xFFFFFFFFu;
        imm &= 0xFFFFFFFFu;
    }

    uint64_t result = lhs & imm;
    set_reg_value(armu, rd_idx, is_64bit, rd_is_sp, result);
}
static void
orr_imm(struct Armu* armu, struct Da64Inst* dinst)
{
    int rd_idx = dinst->ops[0].reg;
    int is_64bit = dinst->ops[0].reggp.sf;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int rn_idx = dinst->ops[1].reg;
    int rn_is_sp = op_is_sp(&dinst->ops[1]);

    uint64_t lhs = get_reg_value(armu, rn_idx, is_64bit, rn_is_sp);
    uint64_t imm = dinst->imm64;
    if (!is_64bit) {
        lhs &= 0xFFFFFFFFu;
        imm &= 0xFFFFFFFFu;
    }

    uint64_t result = lhs | imm;
    set_reg_value(armu, rd_idx, is_64bit, rd_is_sp, result);
}
static void
eor_imm(struct Armu* armu, struct Da64Inst* dinst)
{
    int rd_idx = dinst->ops[0].reg;
    int is_64bit = dinst->ops[0].reggp.sf;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int rn_idx = dinst->ops[1].reg;
    int rn_is_sp = op_is_sp(&dinst->ops[1]);

    uint64_t lhs = get_reg_value(armu, rn_idx, is_64bit, rn_is_sp);
    uint64_t imm = dinst->imm64;
    if (!is_64bit) {
        lhs &= 0xFFFFFFFFu;
        imm &= 0xFFFFFFFFu;
    }

    uint64_t result = lhs ^ imm;
    set_reg_value(armu, rd_idx, is_64bit, rd_is_sp, result);
}
static void
ands_imm(struct Armu* armu, struct Da64Inst* dinst)
{
    int rd_idx = dinst->ops[0].reg;
    int is_64bit = dinst->ops[0].reggp.sf;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int rn_idx = dinst->ops[1].reg;
    int rn_is_sp = op_is_sp(&dinst->ops[1]);

    uint64_t lhs = get_reg_value(armu, rn_idx, is_64bit, rn_is_sp);
    uint64_t imm = dinst->imm64;
    if (!is_64bit) {
        lhs &= 0xFFFFFFFFu;
        imm &= 0xFFFFFFFFu;
    }

    uint64_t result = lhs & imm;
    update_nz(armu, is_64bit, result);
    set_reg_value(armu, rd_idx, is_64bit, rd_is_sp, result);
}
static void
and_shift(struct Armu* armu, struct Da64Inst* dinst)
{
    int rd_idx = dinst->ops[0].reg;
    int is_64bit = dinst->ops[0].reggp.sf;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int rn_idx = dinst->ops[1].reg;
    int rn_is_sp = op_is_sp(&dinst->ops[1]);

    uint64_t lhs = get_reg_value(armu, rn_idx, is_64bit, rn_is_sp);
    uint64_t operand = apply_shift_operand(armu, &dinst->ops[2], is_64bit);
    uint64_t result = lhs & operand;

    set_reg_value(armu, rd_idx, is_64bit, rd_is_sp, result);
}
static void
bic_shift(struct Armu* armu, struct Da64Inst* dinst)
{
    int rd_idx = dinst->ops[0].reg;
    int is_64bit = dinst->ops[0].reggp.sf;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int rn_idx = dinst->ops[1].reg;
    int rn_is_sp = op_is_sp(&dinst->ops[1]);

    uint64_t lhs = get_reg_value(armu, rn_idx, is_64bit, rn_is_sp);
    uint64_t operand = apply_shift_operand(armu, &dinst->ops[2], is_64bit);
    uint64_t result = lhs & ~operand;

    set_reg_value(armu, rd_idx, is_64bit, rd_is_sp, result);
}
static void
orr_shift(struct Armu* armu, struct Da64Inst* dinst)
{
    int rd_idx = dinst->ops[0].reg;
    int is_64bit = dinst->ops[0].reggp.sf;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int rn_idx = dinst->ops[1].reg;
    int rn_is_sp = op_is_sp(&dinst->ops[1]);

    uint64_t lhs = get_reg_value(armu, rn_idx, is_64bit, rn_is_sp);
    uint64_t operand = apply_shift_operand(armu, &dinst->ops[2], is_64bit);
    uint64_t result = lhs | operand;

    set_reg_value(armu, rd_idx, is_64bit, rd_is_sp, result);
}
static void
orn_shift(struct Armu* armu, struct Da64Inst* dinst)
{
    int rd_idx = dinst->ops[0].reg;
    int is_64bit = dinst->ops[0].reggp.sf;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int rn_idx = dinst->ops[1].reg;
    int rn_is_sp = op_is_sp(&dinst->ops[1]);

    uint64_t lhs = get_reg_value(armu, rn_idx, is_64bit, rn_is_sp);
    uint64_t operand = apply_shift_operand(armu, &dinst->ops[2], is_64bit);
    uint64_t result = lhs | ~operand;

    set_reg_value(armu, rd_idx, is_64bit, rd_is_sp, result);
}
static void
eor_shift(struct Armu* armu, struct Da64Inst* dinst)
{
    int rd_idx = dinst->ops[0].reg;
    int is_64bit = dinst->ops[0].reggp.sf;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int rn_idx = dinst->ops[1].reg;
    int rn_is_sp = op_is_sp(&dinst->ops[1]);

    uint64_t lhs = get_reg_value(armu, rn_idx, is_64bit, rn_is_sp);
    uint64_t operand = apply_shift_operand(armu, &dinst->ops[2], is_64bit);
    uint64_t result = lhs ^ operand;

    set_reg_value(armu, rd_idx, is_64bit, rd_is_sp, result);
}
static void
eon_shift(struct Armu* armu, struct Da64Inst* dinst)
{
    int rd_idx = dinst->ops[0].reg;
    int is_64bit = dinst->ops[0].reggp.sf;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int rn_idx = dinst->ops[1].reg;
    int rn_is_sp = op_is_sp(&dinst->ops[1]);

    uint64_t lhs = get_reg_value(armu, rn_idx, is_64bit, rn_is_sp);
    uint64_t operand = apply_shift_operand(armu, &dinst->ops[2], is_64bit);
    uint64_t result = lhs ^ ~operand;

    set_reg_value(armu, rd_idx, is_64bit, rd_is_sp, result);
}
static void
ands_shift(struct Armu* armu, struct Da64Inst* dinst)
{
    int rd_idx = dinst->ops[0].reg;
    int is_64bit = dinst->ops[0].reggp.sf;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int rn_idx = dinst->ops[1].reg;
    int rn_is_sp = op_is_sp(&dinst->ops[1]);

    uint64_t lhs = get_reg_value(armu, rn_idx, is_64bit, rn_is_sp);
    uint64_t operand = apply_shift_operand(armu, &dinst->ops[2], is_64bit);
    uint64_t result = lhs & operand;

    update_nz(armu, is_64bit, result);
    set_reg_value(armu, rd_idx, is_64bit, rd_is_sp, result);
}
static void
bics_shift(struct Armu* armu, struct Da64Inst* dinst)
{
    int rd_idx = dinst->ops[0].reg;
    int is_64bit = dinst->ops[0].reggp.sf;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int rn_idx = dinst->ops[1].reg;
    int rn_is_sp = op_is_sp(&dinst->ops[1]);

    uint64_t lhs = get_reg_value(armu, rn_idx, is_64bit, rn_is_sp);
    uint64_t operand = apply_shift_operand(armu, &dinst->ops[2], is_64bit);
    uint64_t result = lhs & ~operand;

    update_nz(armu, is_64bit, result);
    set_reg_value(armu, rd_idx, is_64bit, rd_is_sp, result);
}
static void
lslv(struct Armu* armu, struct Da64Inst* dinst)
{
    int rd_idx = dinst->ops[0].reg;
    int is_64bit = dinst->ops[0].reggp.sf;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int rn_idx = dinst->ops[1].reg;
    int rn_is_sp = op_is_sp(&dinst->ops[1]);
    int rm_idx = dinst->ops[2].reg;

    unsigned width = value_width(is_64bit);
    uint64_t value = get_reg_value(armu, rn_idx, is_64bit, rn_is_sp);
    uint64_t shift = get_reg_value(armu, rm_idx, 1, 0) & (width - 1);
    uint64_t result = logical_shift_left(value, shift, width);

    set_reg_value(armu, rd_idx, is_64bit, rd_is_sp, result);
}
static void
lsrv(struct Armu* armu, struct Da64Inst* dinst)
{
    int rd_idx = dinst->ops[0].reg;
    int is_64bit = dinst->ops[0].reggp.sf;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int rn_idx = dinst->ops[1].reg;
    int rn_is_sp = op_is_sp(&dinst->ops[1]);
    int rm_idx = dinst->ops[2].reg;

    unsigned width = value_width(is_64bit);
    uint64_t value = get_reg_value(armu, rn_idx, is_64bit, rn_is_sp);
    uint64_t shift = get_reg_value(armu, rm_idx, 1, 0) & (width - 1);
    uint64_t result = logical_shift_right(value, shift, width);

    set_reg_value(armu, rd_idx, is_64bit, rd_is_sp, result);
}
static void
asrv(struct Armu* armu, struct Da64Inst* dinst)
{
    int rd_idx = dinst->ops[0].reg;
    int is_64bit = dinst->ops[0].reggp.sf;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int rn_idx = dinst->ops[1].reg;
    int rn_is_sp = op_is_sp(&dinst->ops[1]);
    int rm_idx = dinst->ops[2].reg;

    unsigned width = value_width(is_64bit);
    uint64_t value = get_reg_value(armu, rn_idx, is_64bit, rn_is_sp);
    uint64_t shift = get_reg_value(armu, rm_idx, 1, 0) & (width - 1);
    uint64_t result = arithmetic_shift_right(value, shift, width);

    set_reg_value(armu, rd_idx, is_64bit, rd_is_sp, result);
}
static void
rorv(struct Armu* armu, struct Da64Inst* dinst)
{
    int rd_idx = dinst->ops[0].reg;
    int is_64bit = dinst->ops[0].reggp.sf;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int rn_idx = dinst->ops[1].reg;
    int rn_is_sp = op_is_sp(&dinst->ops[1]);
    int rm_idx = dinst->ops[2].reg;

    unsigned width = value_width(is_64bit);
    uint64_t value = get_reg_value(armu, rn_idx, is_64bit, rn_is_sp);
    uint64_t shift = get_reg_value(armu, rm_idx, 1, 0) & (width - 1);
    uint64_t result = rotate_right(value, shift, width);

    set_reg_value(armu, rd_idx, is_64bit, rd_is_sp, result);
}
static void
madd(struct Armu* armu, struct Da64Inst* dinst)
{

}
static void
msub(struct Armu* armu, struct Da64Inst* dinst)
{

}
static void
smaddl(struct Armu* armu, struct Da64Inst* dinst)
{

}
static void
smsubl(struct Armu* armu, struct Da64Inst* dinst)
{

}
static void
umaddl(struct Armu* armu, struct Da64Inst* dinst)
{

}
static void
umsubl(struct Armu* armu, struct Da64Inst* dinst)
{

}
static void
smulh(struct Armu* armu, struct Da64Inst* dinst)
{

}
static void
umulh(struct Armu* armu, struct Da64Inst* dinst)
{

}
static void
bcond(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned cond = dinst->ops[0].cond;
    if (condition_passed(armu, cond)) {
        armu->pc = armu->pc + (int64_t)dinst->imm64;
    }
}
static void
b(struct Armu* armu, struct Da64Inst* dinst)
{
    armu->pc = armu->pc + (int64_t)dinst->imm64;
}
static void
bl(struct Armu* armu, struct Da64Inst* dinst)
{
    uint64_t return_address = armu->pc + sizeof(uint32_t);
    set_reg_value(armu, 30, 1, 0, return_address);
    armu->pc = armu->pc + (int64_t)dinst->imm64;
}
static void
sbfm(struct Armu* armu, struct Da64Inst* dinst)
{

}
static void
bfm(struct Armu* armu, struct Da64Inst* dinst)
{

}
static void
ubfm(struct Armu* armu, struct Da64Inst* dinst)
{

}
static void
br(struct Armu* armu, struct Da64Inst* dinst)
{
    int rn_idx = dinst->ops[0].reg;
    uint64_t target = get_reg_value(armu, rn_idx, 1, 0);
    armu->pc = target;
}
static void
blr(struct Armu* armu, struct Da64Inst* dinst)
{
    uint64_t return_address = armu->pc + sizeof(uint32_t);
    set_reg_value(armu, 30, 1, 0, return_address);
    int rn_idx = dinst->ops[0].reg;
    uint64_t target = get_reg_value(armu, rn_idx, 1, 0);
    armu->pc = target;
}
static void
ret(struct Armu* armu, struct Da64Inst* dinst)
{
    int rn_idx = dinst->ops[0].reg;
    uint64_t target = get_reg_value(armu, rn_idx, 1, 0);
    armu->pc = target;
}
static void
brk(struct Armu* armu, struct Da64Inst* dinst)
{
    (void)dinst;
    armu->pc = armu->program_size;
}
static void
cbz(struct Armu* armu, struct Da64Inst* dinst)
{
    int is_64bit = dinst->ops[0].reggp.sf;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);

    uint64_t value = get_reg_value(armu, rd_idx, is_64bit, rd_is_sp);
    unsigned width = value_width(is_64bit);
    value = zero_extend_bits(value, width);
    if (value == 0) {
        armu->pc = armu->pc + (int64_t)dinst->imm64;
    }
}
static void
cbnz(struct Armu* armu, struct Da64Inst* dinst)
{
    int is_64bit = dinst->ops[0].reggp.sf;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);

    uint64_t value = get_reg_value(armu, rd_idx, is_64bit, rd_is_sp);
    unsigned width = value_width(is_64bit);
    value = zero_extend_bits(value, width);
    if (value != 0) {
        armu->pc = armu->pc + (int64_t)dinst->imm64;
    }
}
static void
tbz(struct Armu* armu, struct Da64Inst* dinst)
{
    int is_64bit = dinst->ops[0].reggp.sf;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    unsigned bit = dinst->ops[1].uimm16;
    unsigned width = value_width(is_64bit);
    bit &= (width - 1);

    uint64_t value = get_reg_value(armu, rd_idx, is_64bit, rd_is_sp);
    value = zero_extend_bits(value, width);
    if (((value >> bit) & 1) == 0) {
        armu->pc = armu->pc + (int64_t)dinst->imm64;
    }
}
static void
tbnz(struct Armu* armu, struct Da64Inst* dinst)
{
    int is_64bit = dinst->ops[0].reggp.sf;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    unsigned bit = dinst->ops[1].uimm16;
    unsigned width = value_width(is_64bit);
    bit &= (width - 1);

    uint64_t value = get_reg_value(armu, rd_idx, is_64bit, rd_is_sp);
    value = zero_extend_bits(value, width);
    if (((value >> bit) & 1) != 0) {
        armu->pc = armu->pc + (int64_t)dinst->imm64;
    }
}
static void
ccmn_imm(struct Armu* armu, struct Da64Inst* dinst)
{

}
static void
ccmp_imm(struct Armu* armu, struct Da64Inst* dinst)
{

}
static void
ccmn_reg(struct Armu* armu, struct Da64Inst* dinst)
{

}
static void
ccmp_reg(struct Armu* armu, struct Da64Inst* dinst)
{

}
static void
clz(struct Armu* armu, struct Da64Inst* dinst)
{

}
static void
cls(struct Armu* armu, struct Da64Inst* dinst)
{

}
static void
csel(struct Armu* armu, struct Da64Inst* dinst)
{

}
static void
csinc(struct Armu* armu, struct Da64Inst* dinst)
{

}
static void
csinv(struct Armu* armu, struct Da64Inst* dinst)
{

}
static void
csneg(struct Armu* armu, struct Da64Inst* dinst)
{

}
static void
extr(struct Armu* armu, struct Da64Inst* dinst)
{

}

// Implementation for MOVZ (Move Wide with Zero)
static void movz(struct Armu* armu, struct Da64Inst* dinst) {
    int rd_idx = dinst->ops[0].reg;
    int is_64bit = dinst->ops[0].reggp.sf;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    uint64_t imm16 = dinst->ops[1].uimm16;
    uint64_t shift_amount = dinst->ops[1].immshift.shift;

    uint64_t value = imm16 << shift_amount;
    set_reg_value(armu, rd_idx, is_64bit, rd_is_sp, value);
}

static void movk(struct Armu* armu, struct Da64Inst* dinst) {
    int rd_idx = dinst->ops[0].reg;
    int is_64bit = dinst->ops[0].reggp.sf;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    uint64_t imm16 = dinst->ops[1].uimm16;
    uint64_t shift_amount = dinst->ops[1].immshift.shift;

    uint64_t mask = 0xFFFFull << shift_amount;
    uint64_t current_value = get_reg_value(armu, rd_idx, is_64bit, rd_is_sp);
    uint64_t value = (current_value & ~mask) | ((imm16 << shift_amount) & mask);

    set_reg_value(armu, rd_idx, is_64bit, rd_is_sp, value);
}

static void movn(struct Armu* armu, struct Da64Inst* dinst) {
    int rd_idx = dinst->ops[0].reg;
    int is_64bit = dinst->ops[0].reggp.sf;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    uint64_t imm16 = dinst->ops[1].uimm16;

    uint64_t shift_amount = dinst->ops[1].immshift.shift;
    uint64_t value = ~(imm16 << shift_amount);

    set_reg_value(armu, rd_idx, is_64bit, rd_is_sp, value);
}

static void
msr(struct Armu* armu, struct Da64Inst* dinst)
{

}
static void
mrs(struct Armu* armu, struct Da64Inst* dinst)
{

}
static void
rbit(struct Armu* armu, struct Da64Inst* dinst)
{

}
static void
rev16(struct Armu* armu, struct Da64Inst* dinst)
{

}
static void
rev(struct Armu* armu, struct Da64Inst* dinst)
{

}
static void
rev32(struct Armu* armu, struct Da64Inst* dinst)
{

}
static void
rev64(struct Armu* armu, struct Da64Inst* dinst)
{

}
static void
udiv(struct Armu* armu, struct Da64Inst* dinst)
{

}
static void
sdiv(struct Armu* armu, struct Da64Inst* dinst)
{

}
static void
stpw_post(struct Armu* armu, struct Da64Inst* dinst)
{
    store_pair_common(armu, dinst, 4);
}
static void
ldpw_post(struct Armu* armu, struct Da64Inst* dinst)
{
    load_pair_common(armu, dinst, 4, 0);
}
static void
stpw(struct Armu* armu, struct Da64Inst* dinst)
{
    store_pair_common(armu, dinst, 4);
}
static void
ldpw(struct Armu* armu, struct Da64Inst* dinst)
{
    load_pair_common(armu, dinst, 4, 0);
}
static void
stpw_pre(struct Armu* armu, struct Da64Inst* dinst)
{
    store_pair_common(armu, dinst, 4);
}
static void
ldpw_pre(struct Armu* armu, struct Da64Inst* dinst)
{
    load_pair_common(armu, dinst, 4, 0);
}
static void
ldpsw_post(struct Armu* armu, struct Da64Inst* dinst)
{
    load_pair_common(armu, dinst, 4, 1);
}
static void
ldpsw(struct Armu* armu, struct Da64Inst* dinst)
{
    load_pair_common(armu, dinst, 4, 1);
}
static void
ldpsw_pre(struct Armu* armu, struct Da64Inst* dinst)
{
    load_pair_common(armu, dinst, 4, 1);
}
static void
stpx_post(struct Armu* armu, struct Da64Inst* dinst)
{
    store_pair_common(armu, dinst, 8);
}
static void
ldpx_post(struct Armu* armu, struct Da64Inst* dinst)
{
    load_pair_common(armu, dinst, 8, 0);
}
static void
stpx(struct Armu* armu, struct Da64Inst* dinst)
{
    store_pair_common(armu, dinst, 8);
}
static void
ldpx(struct Armu* armu, struct Da64Inst* dinst)
{
    load_pair_common(armu, dinst, 8, 0);
}
static void
stpx_pre(struct Armu* armu, struct Da64Inst* dinst)
{
    store_pair_common(armu, dinst, 8);
}
static void
ldpx_pre(struct Armu* armu, struct Da64Inst* dinst)
{
    load_pair_common(armu, dinst, 8, 0);
}
static void
sturb(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 1;
    uint64_t value = get_store_reg_scalar(armu, &dinst->ops[0], size);
    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    store_scalar_value(armu, address, size, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
strb_post(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 1;
    uint64_t value = get_store_reg_scalar(armu, &dinst->ops[0], size);
    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    store_scalar_value(armu, address, size, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
strb_pre(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 1;
    uint64_t value = get_store_reg_scalar(armu, &dinst->ops[0], size);
    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    store_scalar_value(armu, address, size, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
ldurb(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 1;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int dest_is_64 = dinst->ops[0].reggp.sf;
    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    uint64_t value = load_scalar_value(armu, address, size, 0, dest_is_64);
    set_reg_value(armu, rd_idx, dest_is_64, rd_is_sp, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
ldrb_post(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 1;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int dest_is_64 = dinst->ops[0].reggp.sf;
    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    uint64_t value = load_scalar_value(armu, address, size, 0, dest_is_64);
    set_reg_value(armu, rd_idx, dest_is_64, rd_is_sp, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
ldrb_pre(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 1;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int dest_is_64 = dinst->ops[0].reggp.sf;
    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    uint64_t value = load_scalar_value(armu, address, size, 0, dest_is_64);
    set_reg_value(armu, rd_idx, dest_is_64, rd_is_sp, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
ldursb(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 1;
    int dest_is_64 = dinst->ops[0].reggp.sf;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    uint64_t value = load_scalar_value(armu, address, size, 1, dest_is_64);
    set_reg_value(armu, rd_idx, dest_is_64, rd_is_sp, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
ldrsb_post(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 1;
    int dest_is_64 = dinst->ops[0].reggp.sf;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    uint64_t value = load_scalar_value(armu, address, size, 1, dest_is_64);
    set_reg_value(armu, rd_idx, dest_is_64, rd_is_sp, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
ldrsb_pre(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 1;
    int dest_is_64 = dinst->ops[0].reggp.sf;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    uint64_t value = load_scalar_value(armu, address, size, 1, dest_is_64);
    set_reg_value(armu, rd_idx, dest_is_64, rd_is_sp, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
ldursbw(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 1;
    int dest_is_64 = dinst->ops[0].reggp.sf;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    uint64_t value = load_scalar_value(armu, address, size, 1, dest_is_64);
    set_reg_value(armu, rd_idx, dest_is_64, rd_is_sp, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
ldrsbw_post(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 1;
    int dest_is_64 = dinst->ops[0].reggp.sf;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    uint64_t value = load_scalar_value(armu, address, size, 1, dest_is_64);
    set_reg_value(armu, rd_idx, dest_is_64, rd_is_sp, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
ldrsbw_pre(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 1;
    int dest_is_64 = dinst->ops[0].reggp.sf;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    uint64_t value = load_scalar_value(armu, address, size, 1, dest_is_64);
    set_reg_value(armu, rd_idx, dest_is_64, rd_is_sp, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
sturh(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 2;
    uint64_t value = get_store_reg_scalar(armu, &dinst->ops[0], size);
    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    store_scalar_value(armu, address, size, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
strh_post(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 2;
    uint64_t value = get_store_reg_scalar(armu, &dinst->ops[0], size);
    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    store_scalar_value(armu, address, size, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
strh_pre(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 2;
    uint64_t value = get_store_reg_scalar(armu, &dinst->ops[0], size);
    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    store_scalar_value(armu, address, size, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
ldurh(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 2;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int dest_is_64 = dinst->ops[0].reggp.sf;
    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    uint64_t value = load_scalar_value(armu, address, size, 0, dest_is_64);
    set_reg_value(armu, rd_idx, dest_is_64, rd_is_sp, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
ldrh_post(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 2;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int dest_is_64 = dinst->ops[0].reggp.sf;
    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    uint64_t value = load_scalar_value(armu, address, size, 0, dest_is_64);
    set_reg_value(armu, rd_idx, dest_is_64, rd_is_sp, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
ldrh_pre(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 2;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int dest_is_64 = dinst->ops[0].reggp.sf;
    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    uint64_t value = load_scalar_value(armu, address, size, 0, dest_is_64);
    set_reg_value(armu, rd_idx, dest_is_64, rd_is_sp, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
ldursh(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 2;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int dest_is_64 = dinst->ops[0].reggp.sf;
    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    uint64_t value = load_scalar_value(armu, address, size, 1, dest_is_64);
    set_reg_value(armu, rd_idx, dest_is_64, rd_is_sp, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
ldrsh_post(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 2;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int dest_is_64 = dinst->ops[0].reggp.sf;
    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    uint64_t value = load_scalar_value(armu, address, size, 1, dest_is_64);
    set_reg_value(armu, rd_idx, dest_is_64, rd_is_sp, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
ldrsh_pre(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 2;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int dest_is_64 = dinst->ops[0].reggp.sf;
    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    uint64_t value = load_scalar_value(armu, address, size, 1, dest_is_64);
    set_reg_value(armu, rd_idx, dest_is_64, rd_is_sp, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
ldurshw(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 2;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int dest_is_64 = dinst->ops[0].reggp.sf;
    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    uint64_t value = load_scalar_value(armu, address, size, 1, dest_is_64);
    set_reg_value(armu, rd_idx, dest_is_64, rd_is_sp, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
ldrshw_post(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 2;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int dest_is_64 = dinst->ops[0].reggp.sf;
    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    uint64_t value = load_scalar_value(armu, address, size, 1, dest_is_64);
    set_reg_value(armu, rd_idx, dest_is_64, rd_is_sp, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
ldrshw_pre(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 2;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int dest_is_64 = dinst->ops[0].reggp.sf;
    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    uint64_t value = load_scalar_value(armu, address, size, 1, dest_is_64);
    set_reg_value(armu, rd_idx, dest_is_64, rd_is_sp, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
sturw(struct Armu* armu, struct Da64Inst* dinst)
{
    int rt_idx = dinst->ops[0].reg;
    int rt_is_sp = op_is_sp(&dinst->ops[0]);
    uint32_t value = (uint32_t)get_reg_value(armu, rt_idx, 0, rt_is_sp);

    int base_idx;
    int base_is_sp;
    uint64_t base = read_base_address(armu, &dinst->ops[1], &base_idx, &base_is_sp);
    int64_t offset = dinst->ops[1].simm16;

    uint64_t address = base + offset;
    write_memory(armu, address, &value, sizeof(value));
}
static void
strw_post(struct Armu* armu, struct Da64Inst* dinst)
{
    int rt_idx = dinst->ops[0].reg;
    int rt_is_sp = op_is_sp(&dinst->ops[0]);
    uint32_t value = (uint32_t)get_reg_value(armu, rt_idx, 0, rt_is_sp);

    int base_idx;
    int base_is_sp;
    uint64_t base = read_base_address(armu, &dinst->ops[1], &base_idx, &base_is_sp);
    int64_t offset = dinst->ops[1].simm16;

    uint64_t address = base;
    write_memory(armu, address, &value, sizeof(value));

    uint64_t new_base = base + offset;
    write_back_base(armu, base_idx, base_is_sp, new_base);
}
static void
strw_pre(struct Armu* armu, struct Da64Inst* dinst)
{
    int rt_idx = dinst->ops[0].reg;
    int rt_is_sp = op_is_sp(&dinst->ops[0]);
    uint32_t value = (uint32_t)get_reg_value(armu, rt_idx, 0, rt_is_sp);

    int base_idx;
    int base_is_sp;
    uint64_t base = read_base_address(armu, &dinst->ops[1], &base_idx, &base_is_sp);
    int64_t offset = dinst->ops[1].simm16;

    uint64_t address = base + offset;
    write_memory(armu, address, &value, sizeof(value));
    write_back_base(armu, base_idx, base_is_sp, address);
}
static void
ldurw(struct Armu* armu, struct Da64Inst* dinst)
{
    int rt_idx = dinst->ops[0].reg;
    int rt_is_sp = op_is_sp(&dinst->ops[0]);

    int base_idx;
    int base_is_sp;
    uint64_t base = read_base_address(armu, &dinst->ops[1], &base_idx, &base_is_sp);
    int64_t offset = dinst->ops[1].simm16;

    uint64_t address = base + offset;
    uint32_t value = 0;
    read_memory(armu, address, &value, sizeof(value));
    set_reg_value(armu, rt_idx, 0, rt_is_sp, value);
}
static void
ldrw_post(struct Armu* armu, struct Da64Inst* dinst)
{
    int rt_idx = dinst->ops[0].reg;
    int rt_is_sp = op_is_sp(&dinst->ops[0]);

    int base_idx;
    int base_is_sp;
    uint64_t base = read_base_address(armu, &dinst->ops[1], &base_idx, &base_is_sp);
    int64_t offset = dinst->ops[1].simm16;

    uint64_t address = base;
    uint32_t value = 0;
    read_memory(armu, address, &value, sizeof(value));
    set_reg_value(armu, rt_idx, 0, rt_is_sp, value);

    uint64_t new_base = base + offset;
    write_back_base(armu, base_idx, base_is_sp, new_base);
}
static void
ldrw_pre(struct Armu* armu, struct Da64Inst* dinst)
{
    int rt_idx = dinst->ops[0].reg;
    int rt_is_sp = op_is_sp(&dinst->ops[0]);

    int base_idx;
    int base_is_sp;
    uint64_t base = read_base_address(armu, &dinst->ops[1], &base_idx, &base_is_sp);
    int64_t offset = dinst->ops[1].simm16;

    uint64_t address = base + offset;
    uint32_t value = 0;
    read_memory(armu, address, &value, sizeof(value));
    set_reg_value(armu, rt_idx, 0, rt_is_sp, value);

    write_back_base(armu, base_idx, base_is_sp, address);
}
static void
ldursw(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 4;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);

    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    uint64_t value = load_scalar_value(armu, address, size, 1, 1);
    set_reg_value(armu, rd_idx, 1, rd_is_sp, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
ldrsw_post(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 4;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);

    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    uint64_t value = load_scalar_value(armu, address, size, 1, 1);
    set_reg_value(armu, rd_idx, 1, rd_is_sp, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
ldrsw_pre(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 4;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);

    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    uint64_t value = load_scalar_value(armu, address, size, 1, 1);
    set_reg_value(armu, rd_idx, 1, rd_is_sp, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
sturx(struct Armu* armu, struct Da64Inst* dinst)
{
    int rt_idx = dinst->ops[0].reg;
    int rt_is_sp = op_is_sp(&dinst->ops[0]);
    uint64_t value = get_reg_value(armu, rt_idx, 1, rt_is_sp);

    int base_idx;
    int base_is_sp;
    uint64_t base = read_base_address(armu, &dinst->ops[1], &base_idx, &base_is_sp);
    int64_t offset = dinst->ops[1].simm16;

    uint64_t address = base + offset;
    write_memory(armu, address, &value, sizeof(value));
}
static void
strx_post(struct Armu* armu, struct Da64Inst* dinst)
{
    int rt_idx = dinst->ops[0].reg;
    int rt_is_sp = op_is_sp(&dinst->ops[0]);
    uint64_t value = get_reg_value(armu, rt_idx, 1, rt_is_sp);

    int base_idx;
    int base_is_sp;
    uint64_t base = read_base_address(armu, &dinst->ops[1], &base_idx, &base_is_sp);
    int64_t offset = dinst->ops[1].simm16;

    uint64_t address = base;
    write_memory(armu, address, &value, sizeof(value));

    uint64_t new_base = base + offset;
    write_back_base(armu, base_idx, base_is_sp, new_base);
}
static void
strx_pre(struct Armu* armu, struct Da64Inst* dinst)
{
    int rt_idx = dinst->ops[0].reg;
    int rt_is_sp = op_is_sp(&dinst->ops[0]);
    uint64_t value = get_reg_value(armu, rt_idx, 1, rt_is_sp);

    int base_idx;
    int base_is_sp;
    uint64_t base = read_base_address(armu, &dinst->ops[1], &base_idx, &base_is_sp);
    int64_t offset = dinst->ops[1].simm16;

    uint64_t address = base + offset;
    write_memory(armu, address, &value, sizeof(value));
    write_back_base(armu, base_idx, base_is_sp, address);
}
static void
ldurx(struct Armu* armu, struct Da64Inst* dinst)
{
    int rt_idx = dinst->ops[0].reg;
    int rt_is_sp = op_is_sp(&dinst->ops[0]);

    int base_idx;
    int base_is_sp;
    uint64_t base = read_base_address(armu, &dinst->ops[1], &base_idx, &base_is_sp);
    int64_t offset = dinst->ops[1].simm16;

    uint64_t address = base + offset;
    uint64_t value = 0;
    read_memory(armu, address, &value, sizeof(value));
    set_reg_value(armu, rt_idx, 1, rt_is_sp, value);
}
static void
ldrx_post(struct Armu* armu, struct Da64Inst* dinst)
{
    int rt_idx = dinst->ops[0].reg;
    int rt_is_sp = op_is_sp(&dinst->ops[0]);

    int base_idx;
    int base_is_sp;
    uint64_t base = read_base_address(armu, &dinst->ops[1], &base_idx, &base_is_sp);
    int64_t offset = dinst->ops[1].simm16;

    uint64_t address = base;
    uint64_t value = 0;
    read_memory(armu, address, &value, sizeof(value));
    set_reg_value(armu, rt_idx, 1, rt_is_sp, value);

    uint64_t new_base = base + offset;
    write_back_base(armu, base_idx, base_is_sp, new_base);
}
static void
ldrx_pre(struct Armu* armu, struct Da64Inst* dinst)
{
    int rt_idx = dinst->ops[0].reg;
    int rt_is_sp = op_is_sp(&dinst->ops[0]);

    int base_idx;
    int base_is_sp;
    uint64_t base = read_base_address(armu, &dinst->ops[1], &base_idx, &base_is_sp);
    int64_t offset = dinst->ops[1].simm16;

    uint64_t address = base + offset;
    uint64_t value = 0;
    read_memory(armu, address, &value, sizeof(value));
    set_reg_value(armu, rt_idx, 1, rt_is_sp, value);

    write_back_base(armu, base_idx, base_is_sp, address);
}
static void
strb_imm(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 1;
    uint64_t value = get_store_reg_scalar(armu, &dinst->ops[0], size);
    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    store_scalar_value(armu, address, size, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
ldrb_imm(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 1;
    int dest_is_64 = dinst->ops[0].reggp.sf;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);

    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    uint64_t value = load_scalar_value(armu, address, size, 0, dest_is_64);
    set_reg_value(armu, rd_idx, dest_is_64, rd_is_sp, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
ldrsb_imm(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 1;
    int dest_is_64 = dinst->ops[0].reggp.sf;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);

    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    uint64_t value = load_scalar_value(armu, address, size, 1, dest_is_64);
    set_reg_value(armu, rd_idx, dest_is_64, rd_is_sp, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
ldrsbw_imm(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 1;
    int dest_is_64 = dinst->ops[0].reggp.sf;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);

    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    uint64_t value = load_scalar_value(armu, address, size, 1, dest_is_64);
    set_reg_value(armu, rd_idx, dest_is_64, rd_is_sp, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
strh_imm(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 2;
    uint64_t value = get_store_reg_scalar(armu, &dinst->ops[0], size);
    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    store_scalar_value(armu, address, size, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
ldrh_imm(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 2;
    int dest_is_64 = dinst->ops[0].reggp.sf;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);

    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    uint64_t value = load_scalar_value(armu, address, size, 0, dest_is_64);
    set_reg_value(armu, rd_idx, dest_is_64, rd_is_sp, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
ldrsh_imm(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 2;
    int dest_is_64 = dinst->ops[0].reggp.sf;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);

    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    uint64_t value = load_scalar_value(armu, address, size, 1, dest_is_64);
    set_reg_value(armu, rd_idx, dest_is_64, rd_is_sp, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
ldrshw_imm(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 2;
    int dest_is_64 = dinst->ops[0].reggp.sf;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);

    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    uint64_t value = load_scalar_value(armu, address, size, 1, dest_is_64);
    set_reg_value(armu, rd_idx, dest_is_64, rd_is_sp, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
strw_imm(struct Armu* armu, struct Da64Inst* dinst)
{
    int rt_idx = dinst->ops[0].reg;
    int rt_is_sp = op_is_sp(&dinst->ops[0]);
    uint32_t value = (uint32_t)get_reg_value(armu, rt_idx, 0, rt_is_sp);

    int rn_idx = dinst->ops[1].reg;
    int rn_is_sp = dinst->ops[1].reg == 31;
    uint64_t base = get_reg_value(armu, rn_idx, 1, rn_is_sp);
    uint64_t offset = dinst->ops[1].uimm16;
    uint64_t address = base + offset;

    write_memory(armu, address, &value, sizeof(value));
}
static void
ldrw_imm(struct Armu* armu, struct Da64Inst* dinst)
{
    int rt_idx = dinst->ops[0].reg;
    int rt_is_sp = op_is_sp(&dinst->ops[0]);
    int rn_idx = dinst->ops[1].reg;
    int rn_is_sp = dinst->ops[1].reg == 31;

    uint64_t base = get_reg_value(armu, rn_idx, 1, rn_is_sp);
    uint64_t offset = dinst->ops[1].uimm16;
    uint64_t address = base + offset;

    uint32_t value = 0;
    read_memory(armu, address, &value, sizeof(value));
    set_reg_value(armu, rt_idx, 0, rt_is_sp, value);
}
static void
ldrsw_imm(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 4;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int dest_is_64 = 1; /* LDRSW always writes to X register */

    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    uint64_t value = load_scalar_value(armu, address, size, 1, dest_is_64);
    set_reg_value(armu, rd_idx, dest_is_64, rd_is_sp, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
str_imm(struct Armu* armu, struct Da64Inst* dinst)
{
    int rt_idx = dinst->ops[0].reg;
    int rt_is_sp = op_is_sp(&dinst->ops[0]);
    int value_is_64 = dinst->ops[0].reggp.sf;
    uint64_t value = get_reg_value(armu, rt_idx, value_is_64, rt_is_sp);

    int rn_idx = dinst->ops[1].reg;
    int rn_is_sp = dinst->ops[1].reg == 31;
    uint64_t base = get_reg_value(armu, rn_idx, 1, rn_is_sp);
    uint64_t offset = dinst->ops[1].uimm16;
    uint64_t address = base + offset;

    write_memory(armu, address, &value, sizeof(value));
}
static void
ldr_imm(struct Armu* armu, struct Da64Inst* dinst)
{
    int rt_idx = dinst->ops[0].reg;
    int rt_is_sp = op_is_sp(&dinst->ops[0]);
    int rn_idx = dinst->ops[1].reg;
    int rn_is_sp = dinst->ops[1].reg == 31;

    uint64_t base = get_reg_value(armu, rn_idx, 1, rn_is_sp);
    uint64_t offset = dinst->ops[1].uimm16;
    uint64_t address = base + offset;

    uint64_t value = 0;
    read_memory(armu, address, &value, sizeof(value));
    set_reg_value(armu, rt_idx, 1, rt_is_sp, value);
}
static void
strb_reg(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 1;
    uint64_t value = get_store_reg_scalar(armu, &dinst->ops[0], size);
    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    store_scalar_value(armu, address, size, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
ldrb_reg(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 1;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int dest_is_64 = dinst->ops[0].reggp.sf;
    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    uint64_t value = load_scalar_value(armu, address, size, 0, dest_is_64);
    set_reg_value(armu, rd_idx, dest_is_64, rd_is_sp, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
ldrsb_reg(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 1;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int dest_is_64 = dinst->ops[0].reggp.sf;
    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    uint64_t value = load_scalar_value(armu, address, size, 1, dest_is_64);
    set_reg_value(armu, rd_idx, dest_is_64, rd_is_sp, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
ldrsbw_reg(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 1;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int dest_is_64 = dinst->ops[0].reggp.sf;
    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    uint64_t value = load_scalar_value(armu, address, size, 1, dest_is_64);
    set_reg_value(armu, rd_idx, dest_is_64, rd_is_sp, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
strh_reg(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 2;
    uint64_t value = get_store_reg_scalar(armu, &dinst->ops[0], size);
    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    store_scalar_value(armu, address, size, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
ldrh_reg(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 2;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int dest_is_64 = dinst->ops[0].reggp.sf;
    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    uint64_t value = load_scalar_value(armu, address, size, 0, dest_is_64);
    set_reg_value(armu, rd_idx, dest_is_64, rd_is_sp, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
ldrsh_reg(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 2;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int dest_is_64 = dinst->ops[0].reggp.sf;
    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    uint64_t value = load_scalar_value(armu, address, size, 1, dest_is_64);
    set_reg_value(armu, rd_idx, dest_is_64, rd_is_sp, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
ldrshw_reg(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 2;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int dest_is_64 = dinst->ops[0].reggp.sf;
    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    uint64_t value = load_scalar_value(armu, address, size, 1, dest_is_64);
    set_reg_value(armu, rd_idx, dest_is_64, rd_is_sp, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
strw_reg(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 4;
    uint64_t value = get_store_reg_scalar(armu, &dinst->ops[0], size);
    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    store_scalar_value(armu, address, size, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
ldrw_reg(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 4;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    uint64_t value = load_scalar_value(armu, address, size, 0, dinst->ops[0].reggp.sf);
    set_reg_value(armu, rd_idx, dinst->ops[0].reggp.sf, rd_is_sp, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
ldrsw_reg(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 4;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    uint64_t value = load_scalar_value(armu, address, size, 1, 1);
    set_reg_value(armu, rd_idx, 1, rd_is_sp, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
str_reg(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 8;
    uint64_t value = get_store_reg_scalar(armu, &dinst->ops[0], size);
    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    store_scalar_value(armu, address, size, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
static void
ldr_reg(struct Armu* armu, struct Da64Inst* dinst)
{
    unsigned size = 8;
    int rd_idx = dinst->ops[0].reg;
    int rd_is_sp = op_is_sp(&dinst->ops[0]);
    int base_idx;
    int base_is_sp;
    uint64_t writeback;
    uint64_t address = compute_address_general(armu, &dinst->ops[1], size, &base_idx, &base_is_sp, &writeback);
    uint64_t value = load_scalar_value(armu, address, size, 0, 1);
    set_reg_value(armu, rd_idx, 1, rd_is_sp, value);
    maybe_writeback(armu, &dinst->ops[1], base_idx, base_is_sp, writeback);
}
