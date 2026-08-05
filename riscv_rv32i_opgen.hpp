/*******************************************************************************
 * RISC-V RV32I Random Opcode Generator
 * 
 * Generates valid, random RV32I instructions with all fields randomized
 * within legal bounds for each instruction type.
 *
 * Enable-mask configuration (same pattern in all opgen modules):
 * enable_group()/enable() restrict generate_random() and the
 * auxiliary selectors to a subset of instruction types — useful during
 * bring-up (generate only what is already implemented) and debugging
 * (focus on a few groups). Named group masks live in opgen::groups.
 * generate(type) ignores the mask. The default mask (groups::ALL) is
 * seed-stable; an empty mask is legalized back to ALL.
 ******************************************************************************/

#ifndef RISCV_RV32I_OPGEN_HPP
#define RISCV_RV32I_OPGEN_HPP

#include <cstdint>
#include <random>
#include <string>
#include <vector>
#include <functional>

namespace riscv {
namespace rv32i {
namespace opgen {

// ============================================================================
// Opcodes
// ============================================================================

namespace opcode {
    constexpr uint32_t LUI    = 0b0110111;
    constexpr uint32_t AUIPC  = 0b0010111;
    constexpr uint32_t JAL    = 0b1101111;
    constexpr uint32_t JALR   = 0b1100111;
    constexpr uint32_t BRANCH = 0b1100011;
    constexpr uint32_t LOAD   = 0b0000011;
    constexpr uint32_t STORE  = 0b0100011;
    constexpr uint32_t OP_IMM = 0b0010011;
    constexpr uint32_t OP     = 0b0110011;
    constexpr uint32_t MISC   = 0b0001111;
    constexpr uint32_t SYSTEM = 0b1110011;
}

// ============================================================================
// Instruction Encoding Helpers
// ============================================================================

// R-type: funct7[31:25] | rs2[24:20] | rs1[19:15] | funct3[14:12] | rd[11:7] | opcode[6:0]
inline uint32_t encode_r_type(uint32_t funct7, uint32_t rs2, uint32_t rs1, 
                               uint32_t funct3, uint32_t rd, uint32_t op) {
    return ((funct7 & 0x7F) << 25) |
           ((rs2 & 0x1F) << 20) |
           ((rs1 & 0x1F) << 15) |
           ((funct3 & 0x7) << 12) |
           ((rd & 0x1F) << 7) |
           (op & 0x7F);
}

// I-type: imm[31:20] | rs1[19:15] | funct3[14:12] | rd[11:7] | opcode[6:0]
inline uint32_t encode_i_type(uint32_t imm, uint32_t rs1, uint32_t funct3, 
                               uint32_t rd, uint32_t op) {
    return ((imm & 0xFFF) << 20) |
           ((rs1 & 0x1F) << 15) |
           ((funct3 & 0x7) << 12) |
           ((rd & 0x1F) << 7) |
           (op & 0x7F);
}

// S-type: imm[11:5] | rs2[24:20] | rs1[19:15] | funct3[14:12] | imm[4:0] | opcode[6:0]
inline uint32_t encode_s_type(uint32_t imm, uint32_t rs2, uint32_t rs1, 
                               uint32_t funct3, uint32_t op) {
    return ((imm & 0xFE0) << 20) |  // imm[11:5] -> bits[31:25]
           ((rs2 & 0x1F) << 20) |
           ((rs1 & 0x1F) << 15) |
           ((funct3 & 0x7) << 12) |
           ((imm & 0x1F) << 7) |    // imm[4:0] -> bits[11:7]
           (op & 0x7F);
}

// B-type: imm[12|10:5] | rs2 | rs1 | funct3 | imm[4:1|11] | opcode
inline uint32_t encode_b_type(uint32_t imm, uint32_t rs2, uint32_t rs1, 
                               uint32_t funct3, uint32_t op) {
    // imm is a signed offset, bit 0 is always 0 (2-byte aligned)
    return (((imm >> 12) & 0x1) << 31) |  // imm[12]
           (((imm >> 5) & 0x3F) << 25) |  // imm[10:5]
           ((rs2 & 0x1F) << 20) |
           ((rs1 & 0x1F) << 15) |
           ((funct3 & 0x7) << 12) |
           (((imm >> 1) & 0xF) << 8) |    // imm[4:1]
           (((imm >> 11) & 0x1) << 7) |   // imm[11]
           (op & 0x7F);
}

// U-type: imm[31:12] | rd[11:7] | opcode[6:0]
inline uint32_t encode_u_type(uint32_t imm, uint32_t rd, uint32_t op) {
    return (imm & 0xFFFFF000) |
           ((rd & 0x1F) << 7) |
           (op & 0x7F);
}

// J-type: imm[20|10:1|11|19:12] | rd | opcode
inline uint32_t encode_j_type(uint32_t imm, uint32_t rd, uint32_t op) {
    // imm is a signed offset, bit 0 is always 0 (2-byte aligned)
    return (((imm >> 20) & 0x1) << 31) |   // imm[20]
           (((imm >> 1) & 0x3FF) << 21) |  // imm[10:1]
           (((imm >> 11) & 0x1) << 20) |   // imm[11]
           (((imm >> 12) & 0xFF) << 12) |  // imm[19:12]
           ((rd & 0x1F) << 7) |
           (op & 0x7F);
}

// ============================================================================
// Random Number Generator Wrapper
// ============================================================================

class RNG {
    std::mt19937 gen;
    
public:
    explicit RNG(uint32_t seed = std::random_device{}()) : gen(seed) {}
    
    void seed(uint32_t s) { gen.seed(s); }
    
    // Random register (0-31)
    uint32_t reg() {
        return std::uniform_int_distribution<uint32_t>(0, 31)(gen);
    }
    
    // Random non-zero register (1-31) - useful for meaningful operations
    uint32_t reg_nz() {
        return std::uniform_int_distribution<uint32_t>(1, 31)(gen);
    }
    
    // Random 12-bit signed immediate (-2048 to 2047)
    int32_t imm12() {
        return std::uniform_int_distribution<int32_t>(-2048, 2047)(gen);
    }
    
    // Random 12-bit unsigned immediate (0 to 4095)
    uint32_t uimm12() {
        return std::uniform_int_distribution<uint32_t>(0, 4095)(gen);
    }
    
    // Random 20-bit upper immediate (already shifted, lower 12 bits = 0)
    uint32_t imm20_upper() {
        return std::uniform_int_distribution<uint32_t>(0, 0xFFFFF)(gen) << 12;
    }
    
    // Random 13-bit signed branch offset (must be even, -4096 to 4094)
    int32_t branch_offset() {
        return std::uniform_int_distribution<int32_t>(-2048, 2047)(gen) * 2;
    }
    
    // Random 21-bit signed jump offset (must be even, -1048576 to 1048574)
    int32_t jal_offset() {
        return std::uniform_int_distribution<int32_t>(-524288, 524287)(gen) * 2;
    }
    
    // Random shift amount (0-31)
    uint32_t shamt() {
        return std::uniform_int_distribution<uint32_t>(0, 31)(gen);
    }
    
    // Random choice from array
    template<typename T, size_t N>
    T choice(const T (&arr)[N]) {
        return arr[std::uniform_int_distribution<size_t>(0, N - 1)(gen)];
    }
    
    // Random bool with given probability of true
    bool chance(double p = 0.5) {
        return std::uniform_real_distribution<double>(0.0, 1.0)(gen) < p;
    }
    
    // Random integer in range [lo, hi]
    uint32_t range(uint32_t lo, uint32_t hi) {
        return std::uniform_int_distribution<uint32_t>(lo, hi)(gen);
    }
};

// ============================================================================
// Instruction Generators
// ============================================================================

// LUI rd, imm
inline uint32_t gen_lui(RNG& rng) {
    return encode_u_type(rng.imm20_upper(), rng.reg(), opcode::LUI);
}

// AUIPC rd, imm
inline uint32_t gen_auipc(RNG& rng) {
    return encode_u_type(rng.imm20_upper(), rng.reg(), opcode::AUIPC);
}

// JAL rd, offset
inline uint32_t gen_jal(RNG& rng) {
    return encode_j_type(rng.jal_offset(), rng.reg(), opcode::JAL);
}

// JALR rd, rs1, offset
inline uint32_t gen_jalr(RNG& rng) {
    return encode_i_type(rng.imm12(), rng.reg(), 0b000, rng.reg(), opcode::JALR);
}

// Branch instructions
inline uint32_t gen_beq(RNG& rng) {
    return encode_b_type(rng.branch_offset(), rng.reg(), rng.reg(), 0b000, opcode::BRANCH);
}

inline uint32_t gen_bne(RNG& rng) {
    return encode_b_type(rng.branch_offset(), rng.reg(), rng.reg(), 0b001, opcode::BRANCH);
}

inline uint32_t gen_blt(RNG& rng) {
    return encode_b_type(rng.branch_offset(), rng.reg(), rng.reg(), 0b100, opcode::BRANCH);
}

inline uint32_t gen_bge(RNG& rng) {
    return encode_b_type(rng.branch_offset(), rng.reg(), rng.reg(), 0b101, opcode::BRANCH);
}

inline uint32_t gen_bltu(RNG& rng) {
    return encode_b_type(rng.branch_offset(), rng.reg(), rng.reg(), 0b110, opcode::BRANCH);
}

inline uint32_t gen_bgeu(RNG& rng) {
    return encode_b_type(rng.branch_offset(), rng.reg(), rng.reg(), 0b111, opcode::BRANCH);
}

// Load instructions
inline uint32_t gen_lb(RNG& rng) {
    return encode_i_type(rng.imm12(), rng.reg(), 0b000, rng.reg(), opcode::LOAD);
}

inline uint32_t gen_lh(RNG& rng) {
    return encode_i_type(rng.imm12(), rng.reg(), 0b001, rng.reg(), opcode::LOAD);
}

inline uint32_t gen_lw(RNG& rng) {
    return encode_i_type(rng.imm12(), rng.reg(), 0b010, rng.reg(), opcode::LOAD);
}

inline uint32_t gen_lbu(RNG& rng) {
    return encode_i_type(rng.imm12(), rng.reg(), 0b100, rng.reg(), opcode::LOAD);
}

inline uint32_t gen_lhu(RNG& rng) {
    return encode_i_type(rng.imm12(), rng.reg(), 0b101, rng.reg(), opcode::LOAD);
}

// Store instructions
inline uint32_t gen_sb(RNG& rng) {
    return encode_s_type(rng.imm12(), rng.reg(), rng.reg(), 0b000, opcode::STORE);
}

inline uint32_t gen_sh(RNG& rng) {
    return encode_s_type(rng.imm12(), rng.reg(), rng.reg(), 0b001, opcode::STORE);
}

inline uint32_t gen_sw(RNG& rng) {
    return encode_s_type(rng.imm12(), rng.reg(), rng.reg(), 0b010, opcode::STORE);
}

// Immediate ALU instructions
inline uint32_t gen_addi(RNG& rng) {
    return encode_i_type(rng.imm12(), rng.reg(), 0b000, rng.reg(), opcode::OP_IMM);
}

inline uint32_t gen_slti(RNG& rng) {
    return encode_i_type(rng.imm12(), rng.reg(), 0b010, rng.reg(), opcode::OP_IMM);
}

inline uint32_t gen_sltiu(RNG& rng) {
    return encode_i_type(rng.imm12(), rng.reg(), 0b011, rng.reg(), opcode::OP_IMM);
}

inline uint32_t gen_xori(RNG& rng) {
    return encode_i_type(rng.imm12(), rng.reg(), 0b100, rng.reg(), opcode::OP_IMM);
}

inline uint32_t gen_ori(RNG& rng) {
    return encode_i_type(rng.imm12(), rng.reg(), 0b110, rng.reg(), opcode::OP_IMM);
}

inline uint32_t gen_andi(RNG& rng) {
    return encode_i_type(rng.imm12(), rng.reg(), 0b111, rng.reg(), opcode::OP_IMM);
}

// Shift immediate instructions (special I-type with shamt)
inline uint32_t gen_slli(RNG& rng) {
    uint32_t shamt = rng.shamt();
    return encode_i_type(shamt, rng.reg(), 0b001, rng.reg(), opcode::OP_IMM);
}

inline uint32_t gen_srli(RNG& rng) {
    uint32_t shamt = rng.shamt();
    return encode_i_type(shamt, rng.reg(), 0b101, rng.reg(), opcode::OP_IMM);
}

inline uint32_t gen_srai(RNG& rng) {
    uint32_t shamt = rng.shamt() | 0x400;  // funct7 bit 5 = 1
    return encode_i_type(shamt, rng.reg(), 0b101, rng.reg(), opcode::OP_IMM);
}

// Register-Register ALU instructions
inline uint32_t gen_add(RNG& rng) {
    return encode_r_type(0b0000000, rng.reg(), rng.reg(), 0b000, rng.reg(), opcode::OP);
}

inline uint32_t gen_sub(RNG& rng) {
    return encode_r_type(0b0100000, rng.reg(), rng.reg(), 0b000, rng.reg(), opcode::OP);
}

inline uint32_t gen_sll(RNG& rng) {
    return encode_r_type(0b0000000, rng.reg(), rng.reg(), 0b001, rng.reg(), opcode::OP);
}

inline uint32_t gen_slt(RNG& rng) {
    return encode_r_type(0b0000000, rng.reg(), rng.reg(), 0b010, rng.reg(), opcode::OP);
}

inline uint32_t gen_sltu(RNG& rng) {
    return encode_r_type(0b0000000, rng.reg(), rng.reg(), 0b011, rng.reg(), opcode::OP);
}

inline uint32_t gen_xor(RNG& rng) {
    return encode_r_type(0b0000000, rng.reg(), rng.reg(), 0b100, rng.reg(), opcode::OP);
}

inline uint32_t gen_srl(RNG& rng) {
    return encode_r_type(0b0000000, rng.reg(), rng.reg(), 0b101, rng.reg(), opcode::OP);
}

inline uint32_t gen_sra(RNG& rng) {
    return encode_r_type(0b0100000, rng.reg(), rng.reg(), 0b101, rng.reg(), opcode::OP);
}

inline uint32_t gen_or(RNG& rng) {
    return encode_r_type(0b0000000, rng.reg(), rng.reg(), 0b110, rng.reg(), opcode::OP);
}

inline uint32_t gen_and(RNG& rng) {
    return encode_r_type(0b0000000, rng.reg(), rng.reg(), 0b111, rng.reg(), opcode::OP);
}

// Fence instructions
inline uint32_t gen_fence(RNG& rng) {
    // FENCE has predecessor/successor ordering bits in imm field
    // fm[3:0] | pred[3:0] | succ[3:0] in bits [31:20]
    // pred/succ: I=bit3, O=bit2, R=bit1, W=bit0
    // fm: 0000 = normal FENCE, 1000 = FENCE.TSO — the two defined values
    // (rd/rs1 stay 0: reserved fields that must be zero).
    uint32_t fm = rng.chance(0.5) ? 0b0000 : 0b1000;
    uint32_t pred = rng.range(0, 15);
    uint32_t succ = rng.range(0, 15);
    uint32_t imm = (fm << 8) | (pred << 4) | succ;
    return encode_i_type(imm, 0, 0b000, 0, opcode::MISC);
}

// System instructions
inline uint32_t gen_ecall(RNG& rng) {
    (void)rng;
    return 0x00000073;  // Fixed encoding
}

inline uint32_t gen_ebreak(RNG& rng) {
    (void)rng;
    return 0x00100073;  // Fixed encoding
}

// ============================================================================
// Instruction Type Enum
// ============================================================================

enum class InstrType {
    // U-type
    LUI, AUIPC,
    // J-type
    JAL,
    // I-type (JALR)
    JALR,
    // B-type
    BEQ, BNE, BLT, BGE, BLTU, BGEU,
    // I-type (Load)
    LB, LH, LW, LBU, LHU,
    // S-type
    SB, SH, SW,
    // I-type (ALU immediate)
    ADDI, SLTI, SLTIU, XORI, ORI, ANDI,
    // I-type (Shift immediate)
    SLLI, SRLI, SRAI,
    // R-type
    ADD, SUB, SLL, SLT, SLTU, XOR, SRL, SRA, OR, AND,
    // Fence
    FENCE,
    // System
    ECALL, EBREAK,
    
    COUNT  // Number of instruction types
};

// Bit for a type in the enable mask
constexpr uint64_t type_bit(InstrType t) { return 1ull << static_cast<unsigned>(t); }

// Named instruction-group masks for the enable configuration
namespace groups {
    constexpr uint64_t UPPER    = type_bit(InstrType::LUI) | type_bit(InstrType::AUIPC);
    constexpr uint64_t JUMPS    = type_bit(InstrType::JAL) | type_bit(InstrType::JALR);
    constexpr uint64_t BRANCHES = type_bit(InstrType::BEQ) | type_bit(InstrType::BNE) |
                                  type_bit(InstrType::BLT) | type_bit(InstrType::BGE) |
                                  type_bit(InstrType::BLTU) | type_bit(InstrType::BGEU);
    constexpr uint64_t LOADS    = type_bit(InstrType::LB) | type_bit(InstrType::LH) |
                                  type_bit(InstrType::LW) | type_bit(InstrType::LBU) |
                                  type_bit(InstrType::LHU);
    constexpr uint64_t STORES   = type_bit(InstrType::SB) | type_bit(InstrType::SH) |
                                  type_bit(InstrType::SW);
    constexpr uint64_t ALU_IMM  = type_bit(InstrType::ADDI) | type_bit(InstrType::SLTI) |
                                  type_bit(InstrType::SLTIU) | type_bit(InstrType::XORI) |
                                  type_bit(InstrType::ORI) | type_bit(InstrType::ANDI);
    constexpr uint64_t SHIFTS   = type_bit(InstrType::SLLI) | type_bit(InstrType::SRLI) |
                                  type_bit(InstrType::SRAI) | type_bit(InstrType::SLL) |
                                  type_bit(InstrType::SRL) | type_bit(InstrType::SRA);
    constexpr uint64_t ALU_REG  = type_bit(InstrType::ADD) | type_bit(InstrType::SUB) |
                                  type_bit(InstrType::SLT) | type_bit(InstrType::SLTU) |
                                  type_bit(InstrType::XOR) | type_bit(InstrType::OR) |
                                  type_bit(InstrType::AND);
    constexpr uint64_t FENCE_OP = type_bit(InstrType::FENCE);
    constexpr uint64_t SYSTEM   = type_bit(InstrType::ECALL) | type_bit(InstrType::EBREAK);
    constexpr uint64_t ALL      = ~0ull;
}

// ============================================================================
// Opcode Generator Class
// ============================================================================

class OpcodeGenerator {
public:
    using GeneratorFunc = uint32_t(*)(RNG&);
    
private:
    RNG rng;
    uint64_t enabled_ = groups::ALL;   // per-instruction-type enable mask
    
    // Generator function table
    static constexpr GeneratorFunc generators[] = {
        // U-type
        gen_lui, gen_auipc,
        // J-type
        gen_jal,
        // I-type (JALR)
        gen_jalr,
        // B-type
        gen_beq, gen_bne, gen_blt, gen_bge, gen_bltu, gen_bgeu,
        // I-type (Load)
        gen_lb, gen_lh, gen_lw, gen_lbu, gen_lhu,
        // S-type
        gen_sb, gen_sh, gen_sw,
        // I-type (ALU immediate)
        gen_addi, gen_slti, gen_sltiu, gen_xori, gen_ori, gen_andi,
        // I-type (Shift immediate)
        gen_slli, gen_srli, gen_srai,
        // R-type
        gen_add, gen_sub, gen_sll, gen_slt, gen_sltu,
        gen_xor, gen_srl, gen_sra, gen_or, gen_and,
        // Fence
        gen_fence,
        // System
        gen_ecall, gen_ebreak
    };
    
    static constexpr size_t NUM_INSTR_TYPES = static_cast<size_t>(InstrType::COUNT);

    // Pick an enabled type from a list; returns COUNT if none is enabled.
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

    // Enable-mask configuration: restrict generate_random() (and the
    // auxiliary selectors) to a subset of instruction types/groups, e.g.
    // during bring-up or debugging. The default (ALL) is seed-stable.
    // An empty mask is legalized back to ALL.
    void enable_group(uint64_t group_mask, bool on = true) {
        if (on) enabled_ |= group_mask; else enabled_ &= ~group_mask;
    }
    void enable(InstrType t, bool on = true) {
        if (on) enabled_ |= type_bit(t); else enabled_ &= ~type_bit(t);
    }
    bool is_enabled(InstrType t) const { return (enabled_ & type_bit(t)) != 0; }

    // Generate a specific instruction type (ignores the enable mask)
    uint32_t generate(InstrType type) {
        return generators[static_cast<size_t>(type)](rng);
    }
    
    // Generate a random instruction, uniformly over the ENABLED types
    uint32_t generate_random() {
        uint64_t m = enabled_ ? enabled_ : groups::ALL;
        if (m == groups::ALL) {
            size_t idx = rng.range(0, NUM_INSTR_TYPES - 1);
            return generators[idx](rng);
        }
        for (;;) {
            size_t idx = rng.range(0, NUM_INSTR_TYPES - 1);
            if ((m >> idx) & 1ull) return generators[idx](rng);
        }
    }
    
    // Generate random instruction excluding control flow (branches/jumps)
    uint32_t generate_no_control_flow() {
        // Exclude: JAL, JALR, BEQ, BNE, BLT, BGE, BLTU, BGEU, ECALL, EBREAK
        static const InstrType safe_types[] = {
            InstrType::LUI, InstrType::AUIPC,
            InstrType::LB, InstrType::LH, InstrType::LW, InstrType::LBU, InstrType::LHU,
            InstrType::SB, InstrType::SH, InstrType::SW,
            InstrType::ADDI, InstrType::SLTI, InstrType::SLTIU, 
            InstrType::XORI, InstrType::ORI, InstrType::ANDI,
            InstrType::SLLI, InstrType::SRLI, InstrType::SRAI,
            InstrType::ADD, InstrType::SUB, InstrType::SLL, InstrType::SLT, InstrType::SLTU,
            InstrType::XOR, InstrType::SRL, InstrType::SRA, InstrType::OR, InstrType::AND,
            InstrType::FENCE
        };
        InstrType t = pick_enabled(safe_types);
        return (t == InstrType::COUNT) ? generate_random() : generate(t);
    }
    
    // Generate random ALU instruction (R-type or I-type ALU)
    uint32_t generate_alu() {
        static const InstrType alu_types[] = {
            InstrType::ADDI, InstrType::SLTI, InstrType::SLTIU,
            InstrType::XORI, InstrType::ORI, InstrType::ANDI,
            InstrType::SLLI, InstrType::SRLI, InstrType::SRAI,
            InstrType::ADD, InstrType::SUB, InstrType::SLL, InstrType::SLT, InstrType::SLTU,
            InstrType::XOR, InstrType::SRL, InstrType::SRA, InstrType::OR, InstrType::AND
        };
        InstrType t = pick_enabled(alu_types);
        return (t == InstrType::COUNT) ? generate_random() : generate(t);
    }
    
    // Generate random memory instruction
    uint32_t generate_memory() {
        static const InstrType mem_types[] = {
            InstrType::LB, InstrType::LH, InstrType::LW, InstrType::LBU, InstrType::LHU,
            InstrType::SB, InstrType::SH, InstrType::SW
        };
        InstrType t = pick_enabled(mem_types);
        return (t == InstrType::COUNT) ? generate_random() : generate(t);
    }
    
    // Generate random branch instruction
    uint32_t generate_branch() {
        static const InstrType branch_types[] = {
            InstrType::BEQ, InstrType::BNE, InstrType::BLT,
            InstrType::BGE, InstrType::BLTU, InstrType::BGEU
        };
        InstrType t = pick_enabled(branch_types);
        return (t == InstrType::COUNT) ? generate_random() : generate(t);
    }
    
    // Generate random jump instruction
    uint32_t generate_jump() {
        bool jal_enabled = is_enabled(InstrType::JAL);
        bool jalr_enabled = is_enabled(InstrType::JALR);
        if (jal_enabled && jalr_enabled)
            return rng.chance(0.5) ? generate(InstrType::JAL) : generate(InstrType::JALR);
        if (jal_enabled)  return generate(InstrType::JAL);
        if (jalr_enabled) return generate(InstrType::JALR);
        return generate_random();
    }
    
    // Generate N random instructions
    std::vector<uint32_t> generate_sequence(size_t n) {
        std::vector<uint32_t> result;
        result.reserve(n);
        for (size_t i = 0; i < n; i++) {
            result.push_back(generate_random());
        }
        return result;
    }
    
    // Generate a linear sequence (no control flow except at end)
    std::vector<uint32_t> generate_linear_sequence(size_t n) {
        std::vector<uint32_t> result;
        result.reserve(n);
        for (size_t i = 0; i < n; i++) {
            result.push_back(generate_no_control_flow());
        }
        return result;
    }
    
    // Get instruction name
    static const char* instr_name(InstrType type) {
        static const char* names[] = {
            "LUI", "AUIPC", "JAL", "JALR",
            "BEQ", "BNE", "BLT", "BGE", "BLTU", "BGEU",
            "LB", "LH", "LW", "LBU", "LHU",
            "SB", "SH", "SW",
            "ADDI", "SLTI", "SLTIU", "XORI", "ORI", "ANDI",
            "SLLI", "SRLI", "SRAI",
            "ADD", "SUB", "SLL", "SLT", "SLTU",
            "XOR", "SRL", "SRA", "OR", "AND",
            "FENCE",
            "ECALL", "EBREAK"
        };
        return names[static_cast<size_t>(type)];
    }
};

// Static member definition
constexpr OpcodeGenerator::GeneratorFunc OpcodeGenerator::generators[];

// ============================================================================
// Convenience Functions
// ============================================================================

// Generate a single random RV32I opcode
inline uint32_t generate_random_opcode(uint32_t seed = std::random_device{}()) {
    OpcodeGenerator gen(seed);
    return gen.generate_random();
}

// Generate N random RV32I opcodes
inline std::vector<uint32_t> generate_random_opcodes(size_t n, uint32_t seed = std::random_device{}()) {
    OpcodeGenerator gen(seed);
    return gen.generate_sequence(n);
}

} // namespace opgen
} // namespace rv32i
} // namespace riscv

#endif // RISCV_RV32I_OPGEN_HPP
