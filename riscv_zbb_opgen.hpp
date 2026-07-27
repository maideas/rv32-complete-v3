/*******************************************************************************
 * RISC-V Zbb Random Opcode Generator
 * 
 * Generates valid, random Zbb basic bit manipulation instructions.
 ******************************************************************************/

#ifndef RISCV_ZBB_OPGEN_HPP
#define RISCV_ZBB_OPGEN_HPP

#include <cstdint>
#include <random>
#include <vector>

namespace riscv {
namespace zbb {
namespace opgen {

// ============================================================================
// Opcodes and Function Codes
// ============================================================================

namespace opcode {
    constexpr uint32_t OP     = 0b0110011;
    constexpr uint32_t OP_IMM = 0b0010011;
}

// ============================================================================
// Encoding Helpers
// ============================================================================

inline uint32_t encode_r_type(uint32_t funct7, uint32_t rs2, uint32_t rs1,
                               uint32_t funct3, uint32_t rd, uint32_t op) {
    return ((funct7 & 0x7F) << 25) |
           ((rs2 & 0x1F) << 20) |
           ((rs1 & 0x1F) << 15) |
           ((funct3 & 0x7) << 12) |
           ((rd & 0x1F) << 7) |
           (op & 0x7F);
}

inline uint32_t encode_i_type(uint32_t imm12, uint32_t rs1, uint32_t funct3,
                               uint32_t rd, uint32_t op) {
    return ((imm12 & 0xFFF) << 20) |
           ((rs1 & 0x1F) << 15) |
           ((funct3 & 0x7) << 12) |
           ((rd & 0x1F) << 7) |
           (op & 0x7F);
}

// ============================================================================
// Random Number Generator
// ============================================================================

class RNG {
    std::mt19937 gen;
    
public:
    explicit RNG(uint32_t seed = std::random_device{}()) : gen(seed) {}
    
    void seed(uint32_t s) { gen.seed(s); }
    
    uint32_t reg() {
        return std::uniform_int_distribution<uint32_t>(0, 31)(gen);
    }
    
    uint32_t shamt() {
        return std::uniform_int_distribution<uint32_t>(0, 31)(gen);
    }
    
    uint32_t range(uint32_t lo, uint32_t hi) {
        return std::uniform_int_distribution<uint32_t>(lo, hi)(gen);
    }
};

// ============================================================================
// Instruction Generators
// ============================================================================

// Logical with negate
inline uint32_t gen_andn(RNG& rng) {
    return encode_r_type(0b0100000, rng.reg(), rng.reg(), 0b111, rng.reg(), opcode::OP);
}
inline uint32_t gen_orn(RNG& rng) {
    return encode_r_type(0b0100000, rng.reg(), rng.reg(), 0b110, rng.reg(), opcode::OP);
}
inline uint32_t gen_xnor(RNG& rng) {
    return encode_r_type(0b0100000, rng.reg(), rng.reg(), 0b100, rng.reg(), opcode::OP);
}

// Count bits
inline uint32_t gen_clz(RNG& rng) {
    return encode_i_type(0b011000000000, rng.reg(), 0b001, rng.reg(), opcode::OP_IMM);
}
inline uint32_t gen_ctz(RNG& rng) {
    return encode_i_type(0b011000000001, rng.reg(), 0b001, rng.reg(), opcode::OP_IMM);
}
inline uint32_t gen_cpop(RNG& rng) {
    return encode_i_type(0b011000000010, rng.reg(), 0b001, rng.reg(), opcode::OP_IMM);
}

// Min/Max
inline uint32_t gen_min(RNG& rng) {
    return encode_r_type(0b0000101, rng.reg(), rng.reg(), 0b100, rng.reg(), opcode::OP);
}
inline uint32_t gen_minu(RNG& rng) {
    return encode_r_type(0b0000101, rng.reg(), rng.reg(), 0b101, rng.reg(), opcode::OP);
}
inline uint32_t gen_max(RNG& rng) {
    return encode_r_type(0b0000101, rng.reg(), rng.reg(), 0b110, rng.reg(), opcode::OP);
}
inline uint32_t gen_maxu(RNG& rng) {
    return encode_r_type(0b0000101, rng.reg(), rng.reg(), 0b111, rng.reg(), opcode::OP);
}

// Sign/Zero extension
inline uint32_t gen_sext_b(RNG& rng) {
    return encode_i_type(0b011000000100, rng.reg(), 0b001, rng.reg(), opcode::OP_IMM);
}
inline uint32_t gen_sext_h(RNG& rng) {
    return encode_i_type(0b011000000101, rng.reg(), 0b001, rng.reg(), opcode::OP_IMM);
}
inline uint32_t gen_zext_h(RNG& rng) {
    return encode_r_type(0b0000100, 0, rng.reg(), 0b100, rng.reg(), opcode::OP);
}

// Rotation
inline uint32_t gen_rol(RNG& rng) {
    return encode_r_type(0b0110000, rng.reg(), rng.reg(), 0b001, rng.reg(), opcode::OP);
}
inline uint32_t gen_ror(RNG& rng) {
    return encode_r_type(0b0110000, rng.reg(), rng.reg(), 0b101, rng.reg(), opcode::OP);
}
inline uint32_t gen_rori(RNG& rng) {
    uint32_t shamt = rng.shamt();
    return encode_i_type((0b0110000 << 5) | shamt, rng.reg(), 0b101, rng.reg(), opcode::OP_IMM);
}

// Byte operations
inline uint32_t gen_orc_b(RNG& rng) {
    return encode_i_type(0b001010000111, rng.reg(), 0b101, rng.reg(), opcode::OP_IMM);
}
inline uint32_t gen_rev8(RNG& rng) {
    return encode_i_type(0b011010011000, rng.reg(), 0b101, rng.reg(), opcode::OP_IMM);
}

// ============================================================================
// Instruction Type Enum
// ============================================================================

enum class InstrType {
    ANDN, ORN, XNOR,
    CLZ, CTZ, CPOP,
    MIN, MINU, MAX, MAXU,
    SEXT_B, SEXT_H, ZEXT_H,
    ROL, ROR, RORI,
    ORC_B, REV8,
    COUNT
};

// ============================================================================
// Opcode Generator Class
// ============================================================================

class OpcodeGenerator {
public:
    using GeneratorFunc = uint32_t(*)(RNG&);
    
private:
    RNG rng;
    
    static constexpr GeneratorFunc generators[] = {
        gen_andn, gen_orn, gen_xnor,
        gen_clz, gen_ctz, gen_cpop,
        gen_min, gen_minu, gen_max, gen_maxu,
        gen_sext_b, gen_sext_h, gen_zext_h,
        gen_rol, gen_ror, gen_rori,
        gen_orc_b, gen_rev8
    };
    
    static constexpr size_t NUM_INSTR_TYPES = static_cast<size_t>(InstrType::COUNT);
    
public:
    explicit OpcodeGenerator(uint32_t seed = std::random_device{}()) : rng(seed) {}
    
    void seed(uint32_t s) { rng.seed(s); }
    
    uint32_t generate(InstrType type) {
        return generators[static_cast<size_t>(type)](rng);
    }
    
    uint32_t generate_random() {
        size_t idx = rng.range(0, NUM_INSTR_TYPES - 1);
        return generators[idx](rng);
    }
    
    // Category generators
    uint32_t generate_logical() {
        static const InstrType types[] = { InstrType::ANDN, InstrType::ORN, InstrType::XNOR };
        return generate(types[rng.range(0, 2)]);
    }
    
    uint32_t generate_count() {
        static const InstrType types[] = { InstrType::CLZ, InstrType::CTZ, InstrType::CPOP };
        return generate(types[rng.range(0, 2)]);
    }
    
    uint32_t generate_minmax() {
        static const InstrType types[] = { InstrType::MIN, InstrType::MINU, InstrType::MAX, InstrType::MAXU };
        return generate(types[rng.range(0, 3)]);
    }
    
    uint32_t generate_extension() {
        static const InstrType types[] = { InstrType::SEXT_B, InstrType::SEXT_H, InstrType::ZEXT_H };
        return generate(types[rng.range(0, 2)]);
    }
    
    uint32_t generate_rotation() {
        static const InstrType types[] = { InstrType::ROL, InstrType::ROR, InstrType::RORI };
        return generate(types[rng.range(0, 2)]);
    }
    
    std::vector<uint32_t> generate_sequence(size_t n) {
        std::vector<uint32_t> result;
        result.reserve(n);
        for (size_t i = 0; i < n; i++) {
            result.push_back(generate_random());
        }
        return result;
    }
    
    static const char* instr_name(InstrType type) {
        static const char* names[] = {
            "ANDN", "ORN", "XNOR",
            "CLZ", "CTZ", "CPOP",
            "MIN", "MINU", "MAX", "MAXU",
            "SEXT.B", "SEXT.H", "ZEXT.H",
            "ROL", "ROR", "RORI",
            "ORC.B", "REV8"
        };
        return names[static_cast<size_t>(type)];
    }
};

constexpr OpcodeGenerator::GeneratorFunc OpcodeGenerator::generators[];

} // namespace opgen
} // namespace zbb
} // namespace riscv

#endif // RISCV_ZBB_OPGEN_HPP
