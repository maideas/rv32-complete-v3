/*******************************************************************************
 * RISC-V RV32C Random Opcode Generator
 * 
 * Generates valid, random RV32C (Compressed) 16-bit instructions with all 
 * fields randomized within legal bounds.
 *
 * The InstrType table produces canonical (non-HINT) encodings only.
 * Compressed HINTs (rd = x0 forms of C.ADDI/C.LI/C.LUI/C.MV/C.ADD/C.SLLI
 * and the shift-by-zero C.SLLI64/C.SRLI64/C.SRAI64 forms) are valid
 * NOP-semantics encodings provided separately via generate_hint() and
 * generate_mixed(), for decoder-robustness stimulus.
 * 
 * Note: This generates RV32C instructions only (not RV64C).
 * C.JAL is RV32-only, C.LD/C.SD/C.LDSP/C.SDSP are RV64-only (not included).
 ******************************************************************************/

#ifndef RISCV_RV32C_OPGEN_HPP
#define RISCV_RV32C_OPGEN_HPP

#include <cstdint>
#include <random>
#include <vector>

namespace riscv {
namespace rv32c {
namespace opgen {

// ============================================================================
// Random Number Generator
// ============================================================================

class RNG {
    std::mt19937 gen;
    
public:
    explicit RNG(uint32_t seed = std::random_device{}()) : gen(seed) {}
    
    void seed(uint32_t s) { gen.seed(s); }
    
    // Full register (0-31)
    uint32_t reg() {
        return std::uniform_int_distribution<uint32_t>(0, 31)(gen);
    }
    
    // Non-zero register (1-31)
    uint32_t reg_nz() {
        return std::uniform_int_distribution<uint32_t>(1, 31)(gen);
    }
    
    // Compressed register (x8-x15, encoded as 0-7)
    uint32_t creg() {
        return std::uniform_int_distribution<uint32_t>(0, 7)(gen);
    }
    
    // 6-bit signed immediate (-32 to 31)
    int32_t imm6() {
        return std::uniform_int_distribution<int32_t>(-32, 31)(gen);
    }
    
    // 6-bit unsigned immediate (0-63)
    uint32_t uimm6() {
        return std::uniform_int_distribution<uint32_t>(0, 63)(gen);
    }
    
    // Non-zero 6-bit signed immediate
    int32_t imm6_nz() {
        int32_t v;
        do {
            v = imm6();
        } while (v == 0);
        return v;
    }
    
    // Non-zero 6-bit unsigned immediate
    uint32_t uimm6_nz() {
        return std::uniform_int_distribution<uint32_t>(1, 63)(gen);
    }
    
    // Shift amount (1-31 for RV32C)
    uint32_t shamt() {
        return std::uniform_int_distribution<uint32_t>(1, 31)(gen);
    }
    
    // Random in range
    uint32_t range(uint32_t lo, uint32_t hi) {
        return std::uniform_int_distribution<uint32_t>(lo, hi)(gen);
    }
    
    int32_t srange(int32_t lo, int32_t hi) {
        return std::uniform_int_distribution<int32_t>(lo, hi)(gen);
    }
    
    bool chance(double p = 0.5) {
        return std::uniform_real_distribution<double>(0.0, 1.0)(gen) < p;
    }
};

// ============================================================================
// Instruction Encoders
// ============================================================================

// Quadrant 0: op = 00
// Quadrant 1: op = 01
// Quadrant 2: op = 10

// C.ADDI4SPN: 000 | nzuimm[5:4|9:6|2|3] | rd' | 00
// nzuimm is scaled by 4, must be non-zero
inline uint16_t gen_c_addi4spn(RNG& rng) {
    uint32_t rd = rng.creg();
    // nzuimm must be non-zero, scaled by 4, range 4-1020
    uint32_t nzuimm = rng.range(1, 255) * 4;
    // Encoding: nzuimm[5:4] -> bits[12:11], nzuimm[9:6] -> bits[10:7]
    //           nzuimm[2] -> bit[6], nzuimm[3] -> bit[5]
    uint16_t instr = 0b00;  // quadrant 0
    instr |= (rd & 0x7) << 2;
    instr |= ((nzuimm >> 3) & 0x1) << 5;   // nzuimm[3]
    instr |= ((nzuimm >> 2) & 0x1) << 6;   // nzuimm[2]
    instr |= ((nzuimm >> 6) & 0xF) << 7;   // nzuimm[9:6]
    instr |= ((nzuimm >> 4) & 0x3) << 11;  // nzuimm[5:4]
    instr |= 0b000 << 13;  // funct3
    return instr;
}

// C.LW: 010 | uimm[5:3] | rs1' | uimm[2|6] | rd' | 00
inline uint16_t gen_c_lw(RNG& rng) {
    uint32_t rd = rng.creg();
    uint32_t rs1 = rng.creg();
    // uimm is word-aligned (scaled by 4), range 0-124
    uint32_t uimm = rng.range(0, 31) * 4;
    uint16_t instr = 0b00;
    instr |= (rd & 0x7) << 2;
    instr |= ((uimm >> 6) & 0x1) << 5;   // uimm[6]
    instr |= ((uimm >> 2) & 0x1) << 6;   // uimm[2]
    instr |= (rs1 & 0x7) << 7;
    instr |= ((uimm >> 3) & 0x7) << 10;  // uimm[5:3]
    instr |= 0b010 << 13;
    return instr;
}

// C.SW: 110 | uimm[5:3] | rs1' | uimm[2|6] | rs2' | 00
inline uint16_t gen_c_sw(RNG& rng) {
    uint32_t rs2 = rng.creg();
    uint32_t rs1 = rng.creg();
    uint32_t uimm = rng.range(0, 31) * 4;
    uint16_t instr = 0b00;
    instr |= (rs2 & 0x7) << 2;
    instr |= ((uimm >> 6) & 0x1) << 5;
    instr |= ((uimm >> 2) & 0x1) << 6;
    instr |= (rs1 & 0x7) << 7;
    instr |= ((uimm >> 3) & 0x7) << 10;
    instr |= 0b110 << 13;
    return instr;
}

// C.NOP: 000 | 0 | 00000 | 00000 | 01
inline uint16_t gen_c_nop(RNG& rng) {
    (void)rng;
    return 0x0001;
}

// C.ADDI: 000 | nzimm[5] | rs1/rd!=0 | nzimm[4:0] | 01
inline uint16_t gen_c_addi(RNG& rng) {
    uint32_t rd = rng.reg_nz();
    int32_t nzimm = rng.imm6_nz();
    uint16_t instr = 0b01;
    instr |= (nzimm & 0x1F) << 2;
    instr |= (rd & 0x1F) << 7;
    instr |= ((nzimm >> 5) & 0x1) << 12;
    instr |= 0b000 << 13;
    return instr;
}

// C.JAL: 001 | imm[11|4|9:8|10|6|7|3:1|5] | 01 (RV32 only)
inline uint16_t gen_c_jal(RNG& rng) {
    // Full signed range: imm[11:1] in [-2048, +2046], step 2
    int32_t imm = rng.srange(-1024, 1023) * 2;
    
    uint16_t instr = 0b01;
    instr |= ((imm >> 5) & 0x1) << 2;    // imm[5]
    instr |= ((imm >> 1) & 0x7) << 3;    // imm[3:1]
    instr |= ((imm >> 7) & 0x1) << 6;    // imm[7]
    instr |= ((imm >> 6) & 0x1) << 7;    // imm[6]
    instr |= ((imm >> 10) & 0x1) << 8;   // imm[10]
    instr |= ((imm >> 8) & 0x3) << 9;    // imm[9:8]
    instr |= ((imm >> 4) & 0x1) << 11;   // imm[4]
    instr |= ((imm >> 11) & 0x1) << 12;  // imm[11]
    instr |= 0b001 << 13;
    return instr;
}

// C.LI: 010 | imm[5] | rd!=0 | imm[4:0] | 01
inline uint16_t gen_c_li(RNG& rng) {
    uint32_t rd = rng.reg_nz();
    int32_t imm = rng.imm6();
    uint16_t instr = 0b01;
    instr |= (imm & 0x1F) << 2;
    instr |= (rd & 0x1F) << 7;
    instr |= ((imm >> 5) & 0x1) << 12;
    instr |= 0b010 << 13;
    return instr;
}

// C.ADDI16SP: 011 | nzimm[9] | 00010 | nzimm[4|6|8:7|5] | 01
inline uint16_t gen_c_addi16sp(RNG& rng) {
    // nzimm is signed, scaled by 16, full valid range -512 to 496
    // (non-zero). Draw the multiplier uniformly from [-32, 31] so the
    // -512 extreme is reachable, and reject the reserved 0 encoding.
    int32_t nzimm;
    do {
        nzimm = rng.srange(-32, 31) * 16;
    } while (nzimm == 0);
    
    uint16_t instr = 0b01;
    instr |= ((nzimm >> 5) & 0x1) << 2;   // nzimm[5]
    instr |= ((nzimm >> 7) & 0x3) << 3;   // nzimm[8:7]
    instr |= ((nzimm >> 6) & 0x1) << 5;   // nzimm[6]
    instr |= ((nzimm >> 4) & 0x1) << 6;   // nzimm[4]
    instr |= 2 << 7;  // rd = x2 (sp)
    instr |= ((nzimm >> 9) & 0x1) << 12;  // nzimm[9]
    instr |= 0b011 << 13;
    return instr;
}

// C.LUI: 011 | nzimm[17] | rd!={0,2} | nzimm[16:12] | 01
inline uint16_t gen_c_lui(RNG& rng) {
    uint32_t rd;
    do {
        rd = rng.reg_nz();
    } while (rd == 2);
    
    // nzimm is in upper 20 bits, non-zero
    int32_t nzimm;
    do {
        nzimm = rng.imm6();
    } while (nzimm == 0);
    
    uint16_t instr = 0b01;
    instr |= (nzimm & 0x1F) << 2;        // nzimm[16:12]
    instr |= (rd & 0x1F) << 7;
    instr |= ((nzimm >> 5) & 0x1) << 12; // nzimm[17]
    instr |= 0b011 << 13;
    return instr;
}

// C.SRLI: 100 | nzuimm[5] | 00 | rs1'/rd' | nzuimm[4:0] | 01
inline uint16_t gen_c_srli(RNG& rng) {
    uint32_t rd = rng.creg();
    uint32_t shamt = rng.shamt();
    uint16_t instr = 0b01;
    instr |= (shamt & 0x1F) << 2;
    instr |= (rd & 0x7) << 7;
    instr |= 0b00 << 10;
    instr |= ((shamt >> 5) & 0x1) << 12;
    instr |= 0b100 << 13;
    return instr;
}

// C.SRAI: 100 | nzuimm[5] | 01 | rs1'/rd' | nzuimm[4:0] | 01
inline uint16_t gen_c_srai(RNG& rng) {
    uint32_t rd = rng.creg();
    uint32_t shamt = rng.shamt();
    uint16_t instr = 0b01;
    instr |= (shamt & 0x1F) << 2;
    instr |= (rd & 0x7) << 7;
    instr |= 0b01 << 10;
    instr |= ((shamt >> 5) & 0x1) << 12;
    instr |= 0b100 << 13;
    return instr;
}

// C.ANDI: 100 | imm[5] | 10 | rs1'/rd' | imm[4:0] | 01
inline uint16_t gen_c_andi(RNG& rng) {
    uint32_t rd = rng.creg();
    int32_t imm = rng.imm6();
    uint16_t instr = 0b01;
    instr |= (imm & 0x1F) << 2;
    instr |= (rd & 0x7) << 7;
    instr |= 0b10 << 10;
    instr |= ((imm >> 5) & 0x1) << 12;
    instr |= 0b100 << 13;
    return instr;
}

// C.SUB: 100 | 0 | 11 | rs1'/rd' | 00 | rs2' | 01
inline uint16_t gen_c_sub(RNG& rng) {
    uint32_t rd = rng.creg();
    uint32_t rs2 = rng.creg();
    uint16_t instr = 0b01;
    instr |= (rs2 & 0x7) << 2;
    instr |= 0b00 << 5;
    instr |= (rd & 0x7) << 7;
    instr |= 0b11 << 10;
    instr |= 0 << 12;
    instr |= 0b100 << 13;
    return instr;
}

// C.XOR: 100 | 0 | 11 | rs1'/rd' | 01 | rs2' | 01
inline uint16_t gen_c_xor(RNG& rng) {
    uint32_t rd = rng.creg();
    uint32_t rs2 = rng.creg();
    uint16_t instr = 0b01;
    instr |= (rs2 & 0x7) << 2;
    instr |= 0b01 << 5;
    instr |= (rd & 0x7) << 7;
    instr |= 0b11 << 10;
    instr |= 0 << 12;
    instr |= 0b100 << 13;
    return instr;
}

// C.OR: 100 | 0 | 11 | rs1'/rd' | 10 | rs2' | 01
inline uint16_t gen_c_or(RNG& rng) {
    uint32_t rd = rng.creg();
    uint32_t rs2 = rng.creg();
    uint16_t instr = 0b01;
    instr |= (rs2 & 0x7) << 2;
    instr |= 0b10 << 5;
    instr |= (rd & 0x7) << 7;
    instr |= 0b11 << 10;
    instr |= 0 << 12;
    instr |= 0b100 << 13;
    return instr;
}

// C.AND: 100 | 0 | 11 | rs1'/rd' | 11 | rs2' | 01
inline uint16_t gen_c_and(RNG& rng) {
    uint32_t rd = rng.creg();
    uint32_t rs2 = rng.creg();
    uint16_t instr = 0b01;
    instr |= (rs2 & 0x7) << 2;
    instr |= 0b11 << 5;
    instr |= (rd & 0x7) << 7;
    instr |= 0b11 << 10;
    instr |= 0 << 12;
    instr |= 0b100 << 13;
    return instr;
}

// C.J: 101 | imm[11|4|9:8|10|6|7|3:1|5] | 01
inline uint16_t gen_c_j(RNG& rng) {
    // Full signed range: imm[11:1] in [-2048, +2046], step 2
    int32_t imm = rng.srange(-1024, 1023) * 2;
    
    uint16_t instr = 0b01;
    instr |= ((imm >> 5) & 0x1) << 2;
    instr |= ((imm >> 1) & 0x7) << 3;
    instr |= ((imm >> 7) & 0x1) << 6;
    instr |= ((imm >> 6) & 0x1) << 7;
    instr |= ((imm >> 10) & 0x1) << 8;
    instr |= ((imm >> 8) & 0x3) << 9;
    instr |= ((imm >> 4) & 0x1) << 11;
    instr |= ((imm >> 11) & 0x1) << 12;
    instr |= 0b101 << 13;
    return instr;
}

// C.BEQZ: 110 | imm[8|4:3] | rs1' | imm[7:6|2:1|5] | 01
inline uint16_t gen_c_beqz(RNG& rng) {
    uint32_t rs1 = rng.creg();
    // Full signed range: imm[8:1] in [-256, +254], step 2
    int32_t imm = rng.srange(-128, 127) * 2;
    
    uint16_t instr = 0b01;
    instr |= ((imm >> 5) & 0x1) << 2;
    instr |= ((imm >> 1) & 0x3) << 3;
    instr |= ((imm >> 6) & 0x3) << 5;
    instr |= (rs1 & 0x7) << 7;
    instr |= ((imm >> 3) & 0x3) << 10;
    instr |= ((imm >> 8) & 0x1) << 12;
    instr |= 0b110 << 13;
    return instr;
}

// C.BNEZ: 111 | imm[8|4:3] | rs1' | imm[7:6|2:1|5] | 01
inline uint16_t gen_c_bnez(RNG& rng) {
    uint32_t rs1 = rng.creg();
    // Full signed range: imm[8:1] in [-256, +254], step 2
    int32_t imm = rng.srange(-128, 127) * 2;
    
    uint16_t instr = 0b01;
    instr |= ((imm >> 5) & 0x1) << 2;
    instr |= ((imm >> 1) & 0x3) << 3;
    instr |= ((imm >> 6) & 0x3) << 5;
    instr |= (rs1 & 0x7) << 7;
    instr |= ((imm >> 3) & 0x3) << 10;
    instr |= ((imm >> 8) & 0x1) << 12;
    instr |= 0b111 << 13;
    return instr;
}

// C.SLLI: 000 | nzuimm[5] | rs1/rd!=0 | nzuimm[4:0] | 10
inline uint16_t gen_c_slli(RNG& rng) {
    uint32_t rd = rng.reg_nz();
    uint32_t shamt = rng.shamt();
    uint16_t instr = 0b10;
    instr |= (shamt & 0x1F) << 2;
    instr |= (rd & 0x1F) << 7;
    instr |= ((shamt >> 5) & 0x1) << 12;
    instr |= 0b000 << 13;
    return instr;
}

// C.LWSP: 010 | uimm[5] | rd!=0 | uimm[4:2|7:6] | 10
inline uint16_t gen_c_lwsp(RNG& rng) {
    uint32_t rd = rng.reg_nz();
    uint32_t uimm = rng.range(0, 63) * 4;  // Word-aligned
    uint16_t instr = 0b10;
    instr |= ((uimm >> 6) & 0x3) << 2;   // uimm[7:6]
    instr |= ((uimm >> 2) & 0x7) << 4;   // uimm[4:2]
    instr |= (rd & 0x1F) << 7;
    instr |= ((uimm >> 5) & 0x1) << 12;  // uimm[5]
    instr |= 0b010 << 13;
    return instr;
}

// C.JR: 100 | 0 | rs1!=0 | 00000 | 10
inline uint16_t gen_c_jr(RNG& rng) {
    uint32_t rs1 = rng.reg_nz();
    uint16_t instr = 0b10;
    instr |= 0 << 2;  // rs2 = 0
    instr |= (rs1 & 0x1F) << 7;
    instr |= 0 << 12;
    instr |= 0b100 << 13;
    return instr;
}

// C.MV: 100 | 0 | rd!=0 | rs2!=0 | 10
inline uint16_t gen_c_mv(RNG& rng) {
    uint32_t rd = rng.reg_nz();
    uint32_t rs2 = rng.reg_nz();
    uint16_t instr = 0b10;
    instr |= (rs2 & 0x1F) << 2;
    instr |= (rd & 0x1F) << 7;
    instr |= 0 << 12;
    instr |= 0b100 << 13;
    return instr;
}

// C.EBREAK: 100 | 1 | 00000 | 00000 | 10
inline uint16_t gen_c_ebreak(RNG& rng) {
    (void)rng;
    return 0x9002;
}

// C.JALR: 100 | 1 | rs1!=0 | 00000 | 10
inline uint16_t gen_c_jalr(RNG& rng) {
    uint32_t rs1 = rng.reg_nz();
    uint16_t instr = 0b10;
    instr |= 0 << 2;  // rs2 = 0
    instr |= (rs1 & 0x1F) << 7;
    instr |= 1 << 12;
    instr |= 0b100 << 13;
    return instr;
}

// C.ADD: 100 | 1 | rs1/rd!=0 | rs2!=0 | 10
inline uint16_t gen_c_add(RNG& rng) {
    uint32_t rd = rng.reg_nz();
    uint32_t rs2 = rng.reg_nz();
    uint16_t instr = 0b10;
    instr |= (rs2 & 0x1F) << 2;
    instr |= (rd & 0x1F) << 7;
    instr |= 1 << 12;
    instr |= 0b100 << 13;
    return instr;
}

// C.SWSP: 110 | uimm[5:2|7:6] | rs2 | 10
inline uint16_t gen_c_swsp(RNG& rng) {
    uint32_t rs2 = rng.reg();
    uint32_t uimm = rng.range(0, 63) * 4;
    uint16_t instr = 0b10;
    instr |= (rs2 & 0x1F) << 2;
    instr |= ((uimm >> 6) & 0x3) << 7;   // uimm[7:6]
    instr |= ((uimm >> 2) & 0xF) << 9;   // uimm[5:2]
    instr |= 0b110 << 13;
    return instr;
}

// ============================================================================
// HINT Generators
//
// HINTs are architecturally VALID encodings that execute as NOPs: the
// rd = x0 forms of C.ADDI/C.LI/C.LUI/C.MV/C.ADD/C.SLLI and the
// shift-by-zero forms (C.SLLI64/C.SRLI64/C.SRAI64). Hardware decoders
// must not trap them. They are kept OUT of the InstrType table above so
// that type-based generation stays canonical; use generate_hint() or
// generate_mixed() to include them in stimulus.
// ============================================================================

// C.ADDI x0, nzimm (nzimm != 0)
inline uint16_t gen_c_addi_x0_hint(RNG& rng) {
    int32_t nzimm = rng.imm6_nz();
    return 0b01 | ((nzimm & 0x1F) << 2) | (((nzimm >> 5) & 0x1) << 12);
}

// C.LI x0, imm (any immediate)
inline uint16_t gen_c_li_x0_hint(RNG& rng) {
    int32_t imm = rng.imm6();
    return (0b010 << 13) | 0b01 | ((imm & 0x1F) << 2) | (((imm >> 5) & 0x1) << 12);
}

// C.LUI x0, nzimm (nzimm != 0)
inline uint16_t gen_c_lui_x0_hint(RNG& rng) {
    int32_t nzimm = rng.imm6_nz();
    return (0b011 << 13) | 0b01 | ((nzimm & 0x1F) << 2) | (((nzimm >> 5) & 0x1) << 12);
}

// C.MV x0, rs2 (rs2 != x0)
inline uint16_t gen_c_mv_x0_hint(RNG& rng) {
    return (0b100 << 13) | 0b10 | (rng.reg_nz() << 2);
}

// C.ADD x0, rs2 (rs2 != x0)
inline uint16_t gen_c_add_x0_hint(RNG& rng) {
    return (0b100 << 13) | (1 << 12) | 0b10 | (rng.reg_nz() << 2);
}

// C.SLLI x0, shamt (any shamt 0-31)
inline uint16_t gen_c_slli_x0_hint(RNG& rng) {
    return 0b10 | ((rng.range(0, 31) & 0x1F) << 2);
}

// C.SLLI64: rd != x0, shamt = 0
inline uint16_t gen_c_slli64_hint(RNG& rng) {
    return 0b10 | (rng.reg_nz() << 7);
}

// C.SRLI64: shamt = 0 (rd' = x8-x15)
inline uint16_t gen_c_srli64_hint(RNG& rng) {
    return (0b100 << 13) | 0b01 | (rng.creg() << 7);
}

// C.SRAI64: shamt = 0 (rd' = x8-x15)
inline uint16_t gen_c_srai64_hint(RNG& rng) {
    return (0b100 << 13) | 0b01 | (0b01 << 10) | (rng.creg() << 7);
}

enum class HintType {
    C_ADDI_X0, C_LI_X0, C_LUI_X0, C_MV_X0, C_ADD_X0,
    C_SLLI_X0, C_SLLI64, C_SRLI64, C_SRAI64,
    COUNT
};

// ============================================================================
// Instruction Type Enum
// ============================================================================

enum class InstrType {
    // Quadrant 0
    C_ADDI4SPN,
    C_LW,
    C_SW,
    
    // Quadrant 1
    C_NOP,
    C_ADDI,
    C_JAL,      // RV32 only
    C_LI,
    C_ADDI16SP,
    C_LUI,
    C_SRLI,
    C_SRAI,
    C_ANDI,
    C_SUB,
    C_XOR,
    C_OR,
    C_AND,
    C_J,
    C_BEQZ,
    C_BNEZ,
    
    // Quadrant 2
    C_SLLI,
    C_LWSP,
    C_JR,
    C_MV,
    C_EBREAK,
    C_JALR,
    C_ADD,
    C_SWSP,
    
    COUNT
};

// Bits for the enable mask: types occupy bits 0-26, HINT families 27-35
constexpr uint64_t type_bit(InstrType t) { return 1ull << static_cast<unsigned>(t); }
constexpr uint64_t hint_bit(HintType t) { return 1ull << (27 + static_cast<unsigned>(t)); }

// Named instruction-group masks
namespace groups {
    constexpr uint64_t C_MEM   = type_bit(InstrType::C_LW) | type_bit(InstrType::C_SW) |
                                 type_bit(InstrType::C_LWSP) | type_bit(InstrType::C_SWSP);
    constexpr uint64_t C_FLOW  = type_bit(InstrType::C_JAL) | type_bit(InstrType::C_J) |
                                 type_bit(InstrType::C_BEQZ) | type_bit(InstrType::C_BNEZ) |
                                 type_bit(InstrType::C_JR) | type_bit(InstrType::C_JALR);
    constexpr uint64_t C_SYSTEM = type_bit(InstrType::C_EBREAK);
    constexpr uint64_t C_TYPES = (1ull << 27) - 1;
    constexpr uint64_t C_ALU   = C_TYPES ^ (C_MEM | C_FLOW | C_SYSTEM);
    constexpr uint64_t HINTS   = 0x1FFull << 27;
    constexpr uint64_t ALL     = ~0ull;
}

// ============================================================================
// Opcode Generator Class
// ============================================================================

class OpcodeGenerator {
public:
    using GeneratorFunc = uint16_t(*)(RNG&);
    
private:
    RNG rng;
    uint64_t enabled_ = groups::ALL;   // types: bits 0-26, hints: bits 27-35
    
    static constexpr GeneratorFunc generators[] = {
        // Quadrant 0
        gen_c_addi4spn, gen_c_lw, gen_c_sw,
        // Quadrant 1
        gen_c_nop, gen_c_addi, gen_c_jal, gen_c_li,
        gen_c_addi16sp, gen_c_lui,
        gen_c_srli, gen_c_srai, gen_c_andi,
        gen_c_sub, gen_c_xor, gen_c_or, gen_c_and,
        gen_c_j, gen_c_beqz, gen_c_bnez,
        // Quadrant 2
        gen_c_slli, gen_c_lwsp, gen_c_jr, gen_c_mv,
        gen_c_ebreak, gen_c_jalr, gen_c_add, gen_c_swsp
    };
    
    static constexpr size_t NUM_INSTR_TYPES = static_cast<size_t>(InstrType::COUNT);

    using HintGeneratorFunc = uint16_t(*)(RNG&);
    static constexpr HintGeneratorFunc hint_generators[] = {
        gen_c_addi_x0_hint, gen_c_li_x0_hint, gen_c_lui_x0_hint,
        gen_c_mv_x0_hint, gen_c_add_x0_hint, gen_c_slli_x0_hint,
        gen_c_slli64_hint, gen_c_srli64_hint, gen_c_srai64_hint
    };
    static constexpr size_t NUM_HINT_TYPES = static_cast<size_t>(HintType::COUNT);

    template<size_t N>
    InstrType pick_enabled(const InstrType (&list)[N]) {
        for (int tries = 0; tries < 8; tries++) {
            InstrType t = list[rng.range(0, N - 1)];
            if (is_enabled(t)) return t;
        }
        for (InstrType t : list) if (is_enabled(t)) return t;
        return InstrType::COUNT;
    }

public:
    explicit OpcodeGenerator(uint32_t seed = std::random_device{}()) : rng(seed) {}
    
    void seed(uint32_t s) { rng.seed(s); }

    // Enable-mask configuration (see rv32i opgen); bits 0-26 select the
    // canonical types, bits 27-35 the HINT families (hint_bit()). The
    // default (ALL) is seed-stable; an empty mask is legalized to ALL.
    void enable_group(uint64_t group_mask, bool on = true) {
        if (on) enabled_ |= group_mask; else enabled_ &= ~group_mask;
    }
    void enable(InstrType t, bool on = true) {
        if (on) enabled_ |= type_bit(t); else enabled_ &= ~type_bit(t);
    }
    void enable_hint(HintType t, bool on = true) {
        if (on) enabled_ |= hint_bit(t); else enabled_ &= ~hint_bit(t);
    }
    bool is_enabled(InstrType t) const { return (enabled_ & type_bit(t)) != 0; }
    bool is_hint_enabled(HintType t) const { return (enabled_ & hint_bit(t)) != 0; }

    // Generate a specific instruction type (ignores the enable mask)
    uint16_t generate(InstrType type) {
        return generators[static_cast<size_t>(type)](rng);
    }
    
    // Generate a random C instruction, uniformly over the ENABLED types
    uint16_t generate_random() {
        uint64_t m = (enabled_ & groups::C_TYPES);
        if (m == 0) m = groups::C_TYPES;   // no canonical type -> legalize
        if (m == groups::C_TYPES) {
            size_t idx = rng.range(0, NUM_INSTR_TYPES - 1);
            return generators[idx](rng);
        }
        for (;;) {
            size_t idx = rng.range(0, NUM_INSTR_TYPES - 1);
            if ((m >> idx) & 1ull) return generators[idx](rng);
        }
    }

    // Generate a random HINT encoding from the ENABLED hint families
    // (executes as NOP; valid, not illegal). If no hint family is
    // enabled, falls back to generate_random().
    uint16_t generate_hint() {
        uint64_t h = (enabled_ ? enabled_ : groups::ALL) & groups::HINTS;
        if (h == groups::HINTS)
            return hint_generators[rng.range(0, NUM_HINT_TYPES - 1)](rng);
        if (h == 0) return generate_random();
        for (;;) {
            size_t idx = rng.range(0, NUM_HINT_TYPES - 1);
            if ((h >> (27 + idx)) & 1ull) return hint_generators[idx](rng);
        }
    }

    uint16_t generate_hint(HintType type) {
        return hint_generators[static_cast<size_t>(type)](rng);
    }

    // Generate a mix of canonical instructions and HINTs
    // (p_hint = probability of emitting a HINT).
    uint16_t generate_mixed(double p_hint = 0.1) {
        return rng.chance(p_hint) ? generate_hint() : generate_random();
    }
    
    // Generate random ALU instruction
    uint16_t generate_alu() {
        static const InstrType alu_types[] = {
            InstrType::C_ADDI, InstrType::C_LI, InstrType::C_ADDI16SP, InstrType::C_LUI,
            InstrType::C_SRLI, InstrType::C_SRAI, InstrType::C_ANDI,
            InstrType::C_SUB, InstrType::C_XOR, InstrType::C_OR, InstrType::C_AND,
            InstrType::C_SLLI, InstrType::C_MV, InstrType::C_ADD
        };
        InstrType t = pick_enabled(alu_types);
        return (t == InstrType::COUNT) ? generate_random() : generate(t);
    }
    
    // Generate random memory instruction
    uint16_t generate_memory() {
        static const InstrType mem_types[] = {
            InstrType::C_LW, InstrType::C_SW, InstrType::C_LWSP, InstrType::C_SWSP
        };
        InstrType t = pick_enabled(mem_types);
        return (t == InstrType::COUNT) ? generate_random() : generate(t);
    }
    
    // Generate random branch/jump instruction
    uint16_t generate_control_flow() {
        static const InstrType cf_types[] = {
            InstrType::C_JAL, InstrType::C_J, InstrType::C_BEQZ, InstrType::C_BNEZ,
            InstrType::C_JR, InstrType::C_JALR
        };
        InstrType t = pick_enabled(cf_types);
        return (t == InstrType::COUNT) ? generate_random() : generate(t);
    }
    
    // Generate non-control-flow instruction
    uint16_t generate_no_control_flow() {
        static const InstrType safe_types[] = {
            InstrType::C_ADDI4SPN, InstrType::C_LW, InstrType::C_SW,
            InstrType::C_NOP, InstrType::C_ADDI, InstrType::C_LI,
            InstrType::C_ADDI16SP, InstrType::C_LUI,
            InstrType::C_SRLI, InstrType::C_SRAI, InstrType::C_ANDI,
            InstrType::C_SUB, InstrType::C_XOR, InstrType::C_OR, InstrType::C_AND,
            InstrType::C_SLLI, InstrType::C_LWSP, InstrType::C_MV, InstrType::C_ADD, InstrType::C_SWSP
        };
        InstrType t = pick_enabled(safe_types);
        return (t == InstrType::COUNT) ? generate_random() : generate(t);
    }
    
    // Generate N random instructions
    std::vector<uint16_t> generate_sequence(size_t n) {
        std::vector<uint16_t> result;
        result.reserve(n);
        for (size_t i = 0; i < n; i++) {
            result.push_back(generate_random());
        }
        return result;
    }
    
    // Generate a linear sequence (no control flow)
    std::vector<uint16_t> generate_linear_sequence(size_t n) {
        std::vector<uint16_t> result;
        result.reserve(n);
        for (size_t i = 0; i < n; i++) {
            result.push_back(generate_no_control_flow());
        }
        return result;
    }
    
    // Get instruction name
    static const char* instr_name(InstrType type) {
        static const char* names[] = {
            "C.ADDI4SPN", "C.LW", "C.SW",
            "C.NOP", "C.ADDI", "C.JAL", "C.LI",
            "C.ADDI16SP", "C.LUI",
            "C.SRLI", "C.SRAI", "C.ANDI",
            "C.SUB", "C.XOR", "C.OR", "C.AND",
            "C.J", "C.BEQZ", "C.BNEZ",
            "C.SLLI", "C.LWSP", "C.JR", "C.MV",
            "C.EBREAK", "C.JALR", "C.ADD", "C.SWSP"
        };
        return names[static_cast<size_t>(type)];
    }
};

// Static member definitions
constexpr OpcodeGenerator::GeneratorFunc OpcodeGenerator::generators[];
constexpr OpcodeGenerator::HintGeneratorFunc OpcodeGenerator::hint_generators[];

// ============================================================================
// Convenience Functions
// ============================================================================

inline uint16_t generate_random_opcode(uint32_t seed = std::random_device{}()) {
    OpcodeGenerator gen(seed);
    return gen.generate_random();
}

inline std::vector<uint16_t> generate_random_opcodes(size_t n, uint32_t seed = std::random_device{}()) {
    OpcodeGenerator gen(seed);
    return gen.generate_sequence(n);
}

} // namespace opgen
} // namespace rv32c
} // namespace riscv

#endif // RISCV_RV32C_OPGEN_HPP
