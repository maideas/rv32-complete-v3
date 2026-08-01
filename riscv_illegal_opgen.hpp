/*******************************************************************************
 * RISC-V Illegal Opcode Generator
 *
 * Generates encodings that are INVALID on a fully-enabled
 * RV32IMAFC_Zicsr_Zifencei_Zba_Zbb_Zbs_Zicond hart (M/S/U modes), for
 * verifying that an implementation rejects every one of them with a
 * precise illegal-instruction exception (cause 2, xtval = instruction
 * bits, xepc = faulting pc, no architectural side effects).
 *
 * The classes below are derived from the specification's encoding
 * tables, NOT from this project's decoders, so that a spec misreading
 * shared by decoder and generator cannot cancel out. Categories:
 *
 *   1. Canonical illegal patterns (all-zero, all-ones)
 *   2. Reserved major opcodes (from the spec's opcode map)
 *   3. Constraint violations inside valid instruction families
 *      (one violated constraint per sample, all other fields random-valid)
 *   4. Compressed (16-bit) reserved encodings
 *
 * Deliberately NOT included (they are VALID and must not trap):
 *   - HINT encodings (c.addi/c.li/c.lui/c.mv/c.slli with rd = x0,
 *     compressed shifts with shamt = 0, c.nop variants)
 *   - ECALL/EBREAK (valid; trap with causes 8/9/11 and 3, not 2)
 *   - CSR instructions on implemented CSRs; SRET/SFENCE.VMA legality is
 *     config-dependent and covered by dedicated config-matrix tests
 *   - rm = DYN with an invalid frm CSR value (state-dependent legality)
 ******************************************************************************/

#ifndef RISCV_ILLEGAL_OPGEN_HPP
#define RISCV_ILLEGAL_OPGEN_HPP

#include <cstdint>
#include <random>

namespace riscv {
namespace illegal_opgen {

// ============================================================================
// Random Number Generator
// ============================================================================

class RNG {
    std::mt19937 gen;

public:
    explicit RNG(uint32_t seed = std::random_device{}()) : gen(seed) {}

    void seed(uint32_t s) { gen.seed(s); }

    uint32_t reg() { return std::uniform_int_distribution<uint32_t>(0, 31)(gen); }
    uint32_t range(uint32_t lo, uint32_t hi) {
        return std::uniform_int_distribution<uint32_t>(lo, hi)(gen);
    }
    uint32_t bits32() { return gen(); }
    bool chance(double p = 0.5) {
        return std::uniform_real_distribution<double>(0.0, 1.0)(gen) < p;
    }
    template<typename T, size_t N>
    T choice(const T (&arr)[N]) {
        return arr[std::uniform_int_distribution<size_t>(0, N - 1)(gen)];
    }
};

// ============================================================================
// Encoding helpers
// ============================================================================

inline uint32_t enc_r(uint32_t f7, uint32_t rs2, uint32_t rs1,
                      uint32_t f3, uint32_t rd, uint32_t op) {
    return ((f7 & 0x7F) << 25) | ((rs2 & 0x1F) << 20) | ((rs1 & 0x1F) << 15) |
           ((f3 & 0x7) << 12) | ((rd & 0x1F) << 7) | (op & 0x7F);
}
inline uint32_t enc_i(uint32_t imm, uint32_t rs1, uint32_t f3,
                      uint32_t rd, uint32_t op) {
    return ((imm & 0xFFF) << 20) | ((rs1 & 0x1F) << 15) |
           ((f3 & 0x7) << 12) | ((rd & 0x1F) << 7) | (op & 0x7F);
}

// ============================================================================
// Category 1: canonical illegal patterns
// ============================================================================

// 0x00000000: the canonical illegal 16-bit instruction (mtval = 0).
inline uint32_t gen_all_zeros(RNG&) { return 0x00000000u; }

// 0xFFFFFFFF: [4:2]=111 marks a >=48-bit instruction prefix; no such
// instruction exists on this hart, so it must trap as illegal.
inline uint32_t gen_all_ones(RNG&) { return 0xFFFFFFFFu; }

// ============================================================================
// Category 2: reserved major opcodes (spec opcode map, inst[6:2])
// ============================================================================

inline uint32_t gen_reserved_major_opcode(RNG& rng) {
    // custom-0, OP-IMM-32, 48-bit, custom-1, OP-32, 64-bit, reserved,
    // custom-2, 48-bit, reserved, reserved, custom-3, >=80-bit
    static const uint32_t bad_ops[] = {
        0b00010, 0b00110, 0b00111, 0b01010, 0b01110, 0b01111,
        0b10101, 0b10110, 0b10111, 0b11010, 0b11101, 0b11110, 0b11111,
    };
    return (rng.bits32() & ~0x7Fu) | (rng.choice(bad_ops) << 2) | 0b11;
}

// ============================================================================
// Category 3: constraint violations in valid 32-bit families
// ============================================================================

// LOAD funct3: 011 = LD (RV64), 110/111 reserved on RV32
inline uint32_t gen_bad_load(RNG& rng) {
    static const uint32_t f3[] = {0b011, 0b110, 0b111};
    return enc_i(rng.range(0, 0xFFF), rng.reg(), rng.choice(f3), rng.reg(), 0b0000011);
}

// STORE funct3: 011 = SD (RV64), 100-111 reserved
inline uint32_t gen_bad_store(RNG& rng) {
    static const uint32_t f3[] = {0b011, 0b100, 0b101, 0b110, 0b111};
    uint32_t imm = rng.range(0, 0xFFF);
    return ((imm & 0xFE0) << 20) | (rng.reg() << 20) | (rng.reg() << 15) |
           (rng.choice(f3) << 12) | ((imm & 0x1F) << 7) | 0b0100011;
}

// BRANCH funct3: 010/011 reserved
inline uint32_t gen_bad_branch(RNG& rng) {
    static const uint32_t f3[] = {0b010, 0b011};
    return enc_r(rng.range(0, 0x7F), rng.reg(), rng.reg(),
                 rng.choice(f3), rng.range(0, 0x1F), 0b1100011);
}

// JALR funct3 must be 000; all other values are reserved
inline uint32_t gen_bad_jalr(RNG& rng) {
    return enc_i(rng.range(0, 0xFFF), rng.reg(), rng.range(1, 7), rng.reg(), 0b1100111);
}

// OP-IMM funct3 = 001 slot. Legal: SLLI (f7=0000000), BSETI (0010100),
// BCLRI (0100100), BINVI (0110100), and the Zbb unary group (0110000)
// with rs2 in {0,1,2,4,5}. Everything else is reserved.
inline uint32_t gen_bad_slli_slot(RNG& rng) {
    switch (rng.range(0, 2)) {
        case 0:  // SLLI with shamt[5] = 1 (and other f7 = 0000001 encodings)
            return enc_i((0b0000001 << 5) | rng.range(0, 31), rng.reg(),
                         0b001, rng.reg(), 0b0010011);
        case 1: {  // unassigned funct7 in this slot
            static const uint32_t f7[] = {0b0000010, 0b0000011, 0b0000100,
                                          0b0000101, 0b0000110, 0b0000111,
                                          0b0001000, 0b0100000, 0b1111111};
            return enc_i(rng.choice(f7) << 5 | rng.range(0, 31), rng.reg(),
                         0b001, rng.reg(), 0b0010011);
        }
        default: { // Zbb unary funct7 with a reserved rs2 value
            uint32_t rs2;
            do { rs2 = rng.reg(); } while (rs2 == 0 || rs2 == 1 || rs2 == 2 ||
                                           rs2 == 4 || rs2 == 5);
            return enc_i(0b0110000 << 5 | rs2, rng.reg(), 0b001, rng.reg(), 0b0010011);
        }
    }
}

// OP-IMM funct3 = 101 slot. Legal: SRLI (0000000), SRAI (0100000),
// RORI (0110000), BEXTI (0100100), ORC.B (0010100 with rs2 = 7),
// REV8 (0110100 with rs2 = 24). Everything else is reserved.
inline uint32_t gen_bad_srli_slot(RNG& rng) {
    switch (rng.range(0, 3)) {
        case 0:  // SRLI slot with shamt[5] = 1
            return enc_i((0b0000001 << 5) | rng.range(0, 31), rng.reg(),
                         0b101, rng.reg(), 0b0010011);
        case 1: {  // unassigned funct7
            static const uint32_t f7[] = {0b0000010, 0b0000011, 0b0001000,
                                          0b0100001, 0b0110001, 0b1111111};
            return enc_i(rng.choice(f7) << 5 | rng.range(0, 31), rng.reg(),
                         0b101, rng.reg(), 0b0010011);
        }
        case 2: {  // ORC.B funct7 with rs2 != 7
            uint32_t rs2;
            do { rs2 = rng.reg(); } while (rs2 == 7);
            return enc_i(0b0010100 << 5 | rs2, rng.reg(), 0b101, rng.reg(), 0b0010011);
        }
        default: { // REV8 funct7 with rs2 != 24
            uint32_t rs2;
            do { rs2 = rng.reg(); } while (rs2 == 24);
            return enc_i(0b0110100 << 5 | rs2, rng.reg(), 0b101, rng.reg(), 0b0010011);
        }
    }
}

// OP (0110011) slot. Legal funct3 mask per funct7 for
// RV32IM + Zba + Zbb + Zbs + Zicond (spec encoding table):
inline uint32_t op_legal_funct3_mask(uint32_t f7) {
    switch (f7) {
        case 0b0000000: return 0xFF;        // ADD/SLL/SLT/SLTU/XOR/SRL/OR/AND
        case 0b0100000: return 0xF1;        // SUB, XNOR, SRA, ORN, ANDN
        case 0b0000001: return 0xFF;        // M extension
        case 0b0010000: return 0x54;        // SH1ADD/SH2ADD/SH3ADD (Zba)
        case 0b0000101: return 0xF0;        // MIN/MINU/MAX/MAXU (Zbb)
        case 0b0110000: return 0x22;        // ROL/ROR (Zbb)
        case 0b0010100: return 0x02;        // BSET (Zbs)
        case 0b0100100: return 0x22;        // BCLR/BEXT (Zbs)
        case 0b0110100: return 0x02;        // BINV (Zbs)
        case 0b0000111: return 0xA0;        // CZERO.EQZ/CZERO.NEZ (Zicond)
        case 0b0000100: return 0x10;        // ZEXT.H (Zbb; only with rs2 = 0)
        default:         return 0x00;
    }
}

inline uint32_t gen_bad_op(RNG& rng) {
    if (rng.chance(0.15)) {
        // funct7 = 0000100, funct3 = 100 with rs2 != 0: this is PACK,
        // which belongs to Zbkb — not implemented on this hart.
        uint32_t rs2;
        do { rs2 = rng.reg(); } while (rs2 == 0);
        return enc_r(0b0000100, rs2, rng.reg(), 0b100, rng.reg(), 0b0110011);
    }
    // Random (funct7, funct3) outside the legal mask (~96% of the space).
    for (;;) {
        uint32_t f7 = rng.range(0, 0x7F), f3 = rng.range(0, 7);
        if ((op_legal_funct3_mask(f7) >> f3) & 1u) continue;
        return enc_r(f7, rng.reg(), rng.reg(), f3, rng.reg(), 0b0110011);
    }
}

// MISC-MEM funct3: only 000 (FENCE) and 001 (FENCE.I) are defined
inline uint32_t gen_bad_misc_mem(RNG& rng) {
    return enc_i(rng.range(0, 0xFFF), rng.reg(), rng.range(2, 7), rng.reg(), 0b0001111);
}

// SYSTEM funct3 = 000: only ECALL, EBREAK, MRET, WFI, SRET and
// SFENCE.VMA are defined. SRET/SFENCE.VMA are excluded here (their
// legality is config-dependent; covered by config-matrix tests).
inline uint32_t gen_bad_system_f3_0(RNG& rng) {
    for (;;) {
        uint32_t raw = enc_r(rng.range(0, 0x7F), rng.reg(), rng.reg(),
                             0b000, rng.reg(), 0b1110011);
        if (raw == 0x00000073u || raw == 0x00100073u ||       // ecall/ebreak
            raw == 0x30200073u || raw == 0x10500073u ||       // mret/wfi
            raw == 0x10200073u ||                             // sret
            (raw & 0xFE007FFFu) == 0x12000073u)               // sfence.vma
            continue;
        return raw;
    }
}

// SYSTEM funct3 = 100: hypervisor slot, reserved without the H extension
inline uint32_t gen_bad_system_f3_4(RNG& rng) {
    return enc_i(rng.range(0, 0xFFF), rng.reg(), 0b100, rng.reg(), 0b1110011);
}

// CSR instruction on a CSR address that does not exist on this hart in
// any configuration (debug-mode, trigger, hypervisor, unimplemented
// counters). Accessing a nonexistent CSR must trap as illegal.
inline uint32_t gen_bad_csr_addr(RNG& rng) {
    static const uint32_t addrs[] = {
        0xC03,  // hpmcounter3 (no HPM counters)
        0xB03,  // mhpmcounter3
        0x320,  // mcountinhibit (not implemented)
        0x7A0,  // tselect (debug triggers)
        0x7B0,  // dcsr (debug mode)
        0x600,  // hstatus (hypervisor)
    };
    static const uint32_t f3[] = {0b001, 0b010, 0b011, 0b101, 0b110, 0b111};
    return enc_i(rng.choice(addrs), rng.reg(), rng.choice(f3), rng.reg(), 0b1110011);
}

// OP-FP with an unassigned funct7 (D-extension slots or garbage)
inline uint32_t gen_bad_fp_funct7(RNG& rng) {
    static const uint32_t f7[] = {0b0000001, 0b0001001, 0b0001101,
                                  0b0101101, 0b0010001, 0b1111111};
    static const uint32_t rm[] = {0, 1, 2, 3, 4, 7};
    return enc_r(rng.choice(f7), rng.reg(), rng.reg(),
                 rng.choice(rm), rng.reg(), 0b1010011);
}

// rm = 101/110 are statically reserved on every rm-using instruction
inline uint32_t gen_bad_fp_rm(RNG& rng) {
    static const uint32_t f7[] = {0b0000000, 0b0001000, 0b0101100,
                                  0b1100000, 0b1101000};  // fadd/fmul/fsqrt/fcvt slots
    return enc_r(rng.choice(f7), rng.reg(), rng.reg(),
                 rng.range(5, 6), rng.reg(), 0b1010011);
}

// FSQRT.S requires rs2 = 0
inline uint32_t gen_bad_fsqrt(RNG& rng) {
    static const uint32_t rm[] = {0, 1, 2, 3, 4, 7};
    uint32_t rs2;
    do { rs2 = rng.reg(); } while (rs2 == 0);
    return enc_r(0b0101100, rs2, rng.reg(), rng.choice(rm), rng.reg(), 0b1010011);
}

// FCVT.W.S/WU.S and FCVT.S.W/WU.S require rs2 in {0, 1}
inline uint32_t gen_bad_fcvt(RNG& rng) {
    static const uint32_t f7[] = {0b1100000, 0b1101000};
    static const uint32_t rm[] = {0, 1, 2, 3, 4, 7};
    return enc_r(rng.choice(f7), rng.range(2, 31), rng.reg(),
                 rng.choice(rm), rng.reg(), 0b1010011);
}

// funct7 = 1110000 slot: FMV.X.W (f3=0) and FCLASS.S (f3=1), both rs2=0
inline uint32_t gen_bad_fmv_fclass(RNG& rng) {
    if (rng.chance(0.5)) {
        return enc_r(0b1110000, rng.reg(), rng.reg(),
                     rng.range(2, 7), rng.reg(), 0b1010011);   // bad funct3
    }
    uint32_t rs2;
    do { rs2 = rng.reg(); } while (rs2 == 0);
    return enc_r(0b1110000, rs2, rng.reg(),
                 rng.range(0, 1), rng.reg(), 0b1010011);        // bad rs2
}

// FCMP funct3 must be in {000, 001, 010}
inline uint32_t gen_bad_fcmp(RNG& rng) {
    return enc_r(0b1010000, rng.reg(), rng.reg(),
                 rng.range(3, 7), rng.reg(), 0b1010011);
}

// FMIN/FMAX funct3 must be in {000, 001}
inline uint32_t gen_bad_fminmax(RNG& rng) {
    return enc_r(0b0010100, rng.reg(), rng.reg(),
                 rng.range(2, 7), rng.reg(), 0b1010011);
}

// FSGNJ funct3 must be in {000, 001, 010}
inline uint32_t gen_bad_fsgnj(RNG& rng) {
    return enc_r(0b0010000, rng.reg(), rng.reg(),
                 rng.range(3, 7), rng.reg(), 0b1010011);
}

// FMV.W.X requires funct3 = 000 and rs2 = 0
inline uint32_t gen_bad_fmv_wx(RNG& rng) {
    if (rng.chance(0.5)) {
        return enc_r(0b1111000, rng.reg(), rng.reg(),
                     rng.range(1, 7), rng.reg(), 0b1010011);   // bad funct3
    }
    uint32_t rs2;
    do { rs2 = rng.reg(); } while (rs2 == 0);
    return enc_r(0b1111000, rs2, rng.reg(), 0b000, rng.reg(), 0b1010011);
}

// LOAD_FP / STORE_FP funct3: only 010 (word) is defined on this hart
inline uint32_t gen_bad_load_fp(RNG& rng) {
    static const uint32_t f3[] = {0b000, 0b001, 0b011, 0b100,
                                  0b101, 0b110, 0b111};
    return enc_i(rng.range(0, 0xFFF), rng.reg(), rng.choice(f3), rng.reg(), 0b0000111);
}
inline uint32_t gen_bad_store_fp(RNG& rng) {
    static const uint32_t f3[] = {0b000, 0b001, 0b011, 0b100,
                                  0b101, 0b110, 0b111};
    uint32_t imm = rng.range(0, 0xFFF);
    return ((imm & 0xFE0) << 20) | (rng.reg() << 20) | (rng.reg() << 15) |
           (rng.choice(f3) << 12) | ((imm & 0x1F) << 7) | 0b0100111;
}

// Fused multiply-add: fmt field (inst[26:25]) must be 00 (single)
inline uint32_t gen_bad_madd_fmt(RNG& rng) {
    static const uint32_t op[] = {0b1000011, 0b1000111, 0b1001011, 0b1001111};
    static const uint32_t rm[] = {0, 1, 2, 3, 4, 7};
    return ((rng.reg() & 0x1F) << 27) | (rng.range(1, 3) << 25) |
           ((rng.reg() & 0x1F) << 20) | ((rng.reg() & 0x1F) << 15) |
           (rng.choice(rm) << 12) | ((rng.reg() & 0x1F) << 7) | rng.choice(op);
}

// AMO funct3: only 010 (word) is defined on RV32
inline uint32_t gen_bad_amo_funct3(RNG& rng) {
    static const uint32_t f3[] = {0b000, 0b001, 0b011, 0b100,
                                  0b101, 0b110, 0b111};
    return enc_r(rng.range(0, 0x7F), rng.reg(), rng.reg(),
                 rng.choice(f3), rng.reg(), 0b0101111);
}

// AMO funct5: only the 11 defined operations
inline uint32_t gen_bad_amo_funct5(RNG& rng) {
    static const uint32_t legal[] = {0b00010, 0b00011, 0b00001, 0b00000,
                                     0b00100, 0b01100, 0b01000, 0b10000,
                                     0b10100, 0b11000, 0b11100};
    uint32_t f5;
    for (;;) {
        f5 = rng.range(0, 31);
        bool ok = false;
        for (uint32_t l : legal) ok |= (f5 == l);
        if (!ok) break;
    }
    return enc_r(f5 << 2 | rng.range(0, 3), rng.reg(), rng.reg(),
                 0b010, rng.reg(), 0b0101111);
}

// LR.W requires rs2 = 0
inline uint32_t gen_bad_lr(RNG& rng) {
    uint32_t rs2;
    do { rs2 = rng.reg(); } while (rs2 == 0);
    return enc_r(0b00010 << 2 | rng.range(0, 3), rs2, rng.reg(),
                 0b010, rng.reg(), 0b0101111);
}

// ============================================================================
// Category 4: compressed (16-bit) reserved encodings (RV32, C + Zcf)
// ============================================================================

// Quadrant 0: funct3 = 001 (C.FLD, RV64), 100 (reserved), 101 (C.FSD, RV64)
inline uint32_t gen_c_quad0_bad_funct3(RNG& rng) {
    static const uint32_t f3[] = {0b001, 0b100, 0b101};
    return (rng.choice(f3) << 13) | (rng.bits32() & 0x1FFC) | 0b00;
}

// C.ADDI4SPN with nzimm = 0 is reserved (includes the all-zero encoding)
inline uint32_t gen_c_addi4spn_zero(RNG& rng) {
    return (rng.reg() & 0x7) << 2;  // funct3 = 000, imm fields = 0, op = 00
}

// C.LWSP with rd = x0 is reserved
inline uint32_t gen_c_lwsp_rd0(RNG& rng) {
    return (0b010u << 13) | ((rng.bits32() & 1) << 12) |
           ((rng.reg() & 0x1F) << 2) | 0b10;
}

// C.JR with rs1 = x0 is reserved
inline uint32_t gen_c_jr_rs1_0(RNG&) {
    return (0b100u << 13) | 0b10;  // bit12 = 0, rs1 = 0, rs2 = 0
}

// Compressed shifts with shamt[5] = 1: reserved/custom space on RV32
inline uint32_t gen_c_shift_shamt5(RNG& rng) {
    if (rng.chance(0.5)) {
        // C.SRLI / C.SRAI (quadrant 1, funct2 = 00/01) with bit 12 set
        return (0b100u << 13) | (1u << 12) | (rng.range(0, 1) << 10) |
               ((rng.reg() & 0x7) << 7) | ((rng.reg() & 0x1F) << 2) | 0b01;
    }
    // C.SLLI (quadrant 2) with bit 12 set
    return (1u << 12) | ((rng.reg() & 0x1F) << 7) |
           ((rng.reg() & 0x1F) << 2) | 0b10;
}

// Quadrant 1 ALU group with funct1 = 1: C.SUBW/C.ADDW are RV64-only,
// the rest is reserved
inline uint32_t gen_c_alu_funct1(RNG& rng) {
    return (0b100u << 13) | (1u << 12) | (0b11u << 10) |
           ((rng.reg() & 0x7) << 7) | (rng.range(0, 3) << 5) |
           ((rng.reg() & 0x7) << 2) | 0b01;
}

// C.LUI (rd != x0, rd != x2) with nzimm = 0 is reserved
inline uint32_t gen_c_lui_zero(RNG& rng) {
    uint32_t rd;
    do { rd = rng.reg(); } while (rd == 0 || rd == 2);
    return (0b011u << 13) | (rd << 7) | 0b01;
}

// C.ADDI16SP with nzimm = 0 is reserved (single encoding 0x6101)
inline uint32_t gen_c_addi16sp_zero(RNG&) {
    return 0x6101;
}

// Quadrant 2: funct3 = 001 (C.FLDSP, RV64) and 101 (C.LDSP, RV64)
inline uint32_t gen_c_quad2_bad_funct3(RNG& rng) {
    static const uint32_t f3[] = {0b001, 0b101};
    return (rng.choice(f3) << 13) | (rng.bits32() & 0x1FFC) | 0b10;
}

// ============================================================================
// Class table (name + generator) for test iteration
// ============================================================================

using GenFunc = uint32_t(*)(RNG&);

struct ClassInfo {
    const char* name;
    GenFunc gen;
    bool compressed;   // true: 16-bit encoding (mtval = zero-extended parcel)
};

// Class indices (stable: they match the table order below), used for
// the enable-mask configuration and group constants.
namespace class_idx {
    constexpr size_t ALL_ZEROS = 0, ALL_ONES = 1, RESERVED_MAJOR = 2;
    constexpr size_t LOAD = 3, STORE = 4, BRANCH = 5, JALR = 6, SLLI = 7,
                     SRLI = 8, OP = 9, MISC_MEM = 10, SYSTEM_F3_0 = 11,
                     SYSTEM_F3_4 = 12, CSR_ADDR = 13;
    constexpr size_t FP_FIRST = 14, FP_LAST = 25;
    constexpr size_t AMO_F3 = 26, AMO_F5 = 27, LR = 28;
    constexpr size_t C_FIRST = 29, C_LAST = 37;
}

// Named class-group masks
namespace groups {
    constexpr uint64_t bit(size_t i) { return 1ull << i; }
    constexpr uint64_t CANONICAL  = bit(0) | bit(1) | bit(2);
    constexpr uint64_t BASE       = 0x3FFull << 3;    // classes 3-12
    constexpr uint64_t CSR        = bit(13);
    constexpr uint64_t FP         = 0xFFFull << 14;   // classes 14-25
    constexpr uint64_t AMO        = 0x7ull << 26;     // classes 26-28
    constexpr uint64_t COMPRESSED = 0x1FFull << 29;   // classes 29-37
    constexpr uint64_t ALL        = ~0ull;
}

inline const ClassInfo* classes(size_t& count) {
    static const ClassInfo table[] = {
        {"all-zeros",                 gen_all_zeros,              true},
        {"all-ones",                  gen_all_ones,               false},
        {"reserved major opcode",     gen_reserved_major_opcode,  false},
        {"LOAD bad funct3",           gen_bad_load,               false},
        {"STORE bad funct3",          gen_bad_store,              false},
        {"BRANCH bad funct3",         gen_bad_branch,             false},
        {"JALR bad funct3",           gen_bad_jalr,               false},
        {"SLLI slot violation",       gen_bad_slli_slot,          false},
        {"SRLI slot violation",       gen_bad_srli_slot,          false},
        {"OP bad funct7/funct3",      gen_bad_op,                 false},
        {"MISC-MEM bad funct3",       gen_bad_misc_mem,           false},
        {"SYSTEM f3=0 unassigned",    gen_bad_system_f3_0,        false},
        {"SYSTEM f3=100 (hypervisor)",gen_bad_system_f3_4,        false},
        {"CSR nonexistent address",   gen_bad_csr_addr,           false},
        {"OP-FP bad funct7",          gen_bad_fp_funct7,          false},
        {"FP reserved rm (101/110)",  gen_bad_fp_rm,              false},
        {"FSQRT.S rs2 != 0",          gen_bad_fsqrt,              false},
        {"FCVT rs2 > 1",              gen_bad_fcvt,               false},
        {"FMV.X.W/FCLASS bad fields", gen_bad_fmv_fclass,         false},
        {"FCMP bad funct3",           gen_bad_fcmp,               false},
        {"FMIN/FMAX bad funct3",      gen_bad_fminmax,            false},
        {"FSGNJ bad funct3",          gen_bad_fsgnj,              false},
        {"FMV.W.X bad fields",        gen_bad_fmv_wx,             false},
        {"LOAD_FP bad funct3",        gen_bad_load_fp,            false},
        {"STORE_FP bad funct3",       gen_bad_store_fp,           false},
        {"MADD-family bad fmt",       gen_bad_madd_fmt,           false},
        {"AMO bad funct3",            gen_bad_amo_funct3,         false},
        {"AMO bad funct5",            gen_bad_amo_funct5,         false},
        {"LR.W rs2 != 0",             gen_bad_lr,                 false},
        {"C quad0 reserved funct3",   gen_c_quad0_bad_funct3,     true},
        {"C.ADDI4SPN nzimm=0",        gen_c_addi4spn_zero,        true},
        {"C.LWSP rd=x0",              gen_c_lwsp_rd0,             true},
        {"C.JR rs1=x0",               gen_c_jr_rs1_0,             true},
        {"C shift shamt[5]=1",        gen_c_shift_shamt5,         true},
        {"C ALU funct1=1 (RV64)",     gen_c_alu_funct1,           true},
        {"C.LUI nzimm=0",             gen_c_lui_zero,             true},
        {"C.ADDI16SP nzimm=0",        gen_c_addi16sp_zero,        true},
        {"C quad2 RV64 funct3",       gen_c_quad2_bad_funct3,     true},
    };
    count = sizeof(table) / sizeof(table[0]);
    return table;
}

// ============================================================================
// Masked random class generator
//
// Same enable-mask configuration as the valid opcode generators (see
// rv32i opgen): restrict the generated classes during bring-up/debugging.
// The default (ALL) is seed-stable per class draw; an empty mask is
// legalized back to ALL.
// ============================================================================

class ClassGenerator {
    RNG rng_;
    uint64_t enabled_ = groups::ALL;

public:
    explicit ClassGenerator(uint32_t seed = std::random_device{}()) : rng_(seed) {}

    void seed(uint32_t s) { rng_.seed(s); }

    void set_enabled_mask(uint64_t mask) { enabled_ = mask; }
    uint64_t get_enabled_mask() const { return enabled_; }
    void enable(size_t class_index, bool on = true) {
        if (on) enabled_ |= groups::bit(class_index);
        else    enabled_ &= ~groups::bit(class_index);
    }

    // Draw a random enabled class, generate one encoding from it, and
    // return the class info through `out` (for diagnostics / mtval
    // handling).
    uint32_t generate_random(const ClassInfo*& out) {
        size_t n;
        const ClassInfo* table = classes(n);
        uint64_t m = enabled_ ? enabled_ : groups::ALL;
        for (;;) {
            size_t idx = rng_.range(0, n - 1);
            if ((m >> idx) & 1ull) {
                out = &table[idx];
                return table[idx].gen(rng_);
            }
        }
    }
};

} // namespace illegal_opgen
} // namespace riscv

#endif // RISCV_ILLEGAL_OPGEN_HPP
