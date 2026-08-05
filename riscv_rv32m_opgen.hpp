/*******************************************************************************
 * RISC-V RV32M Random Opcode Generator
 * 
 * Generates valid, random RV32M (Multiply-Divide) instructions with all 
 * fields randomized within legal bounds.
 ******************************************************************************/

#ifndef RISCV_RV32M_OPGEN_HPP
#define RISCV_RV32M_OPGEN_HPP

#include <cstdint>
#include <random>
#include <vector>

namespace riscv {
namespace rv32m {
namespace opgen {

// ============================================================================
// Opcodes and Function Codes
// ============================================================================

namespace opcode {
    constexpr uint32_t OP = 0b0110011;  // R-type ALU
}

namespace funct7 {
    constexpr uint32_t MULDIV = 0b0000001;
}

namespace funct3 {
    constexpr uint32_t MUL    = 0b000;
    constexpr uint32_t MULH   = 0b001;
    constexpr uint32_t MULHSU = 0b010;
    constexpr uint32_t MULHU  = 0b011;
    constexpr uint32_t DIV    = 0b100;
    constexpr uint32_t DIVU   = 0b101;
    constexpr uint32_t REM    = 0b110;
    constexpr uint32_t REMU   = 0b111;
}

// ============================================================================
// Instruction Encoding
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
    
    uint32_t reg_nz() {
        return std::uniform_int_distribution<uint32_t>(1, 31)(gen);
    }
    
    uint32_t range(uint32_t lo, uint32_t hi) {
        return std::uniform_int_distribution<uint32_t>(lo, hi)(gen);
    }
    
    bool chance(double p = 0.5) {
        return std::uniform_real_distribution<double>(0.0, 1.0)(gen) < p;
    }
};

// ============================================================================
// Instruction Generators
// ============================================================================

inline uint32_t gen_mul(RNG& rng) {
    return encode_r_type(funct7::MULDIV, rng.reg(), rng.reg(), 
                         funct3::MUL, rng.reg(), opcode::OP);
}

inline uint32_t gen_mulh(RNG& rng) {
    return encode_r_type(funct7::MULDIV, rng.reg(), rng.reg(), 
                         funct3::MULH, rng.reg(), opcode::OP);
}

inline uint32_t gen_mulhsu(RNG& rng) {
    return encode_r_type(funct7::MULDIV, rng.reg(), rng.reg(), 
                         funct3::MULHSU, rng.reg(), opcode::OP);
}

inline uint32_t gen_mulhu(RNG& rng) {
    return encode_r_type(funct7::MULDIV, rng.reg(), rng.reg(), 
                         funct3::MULHU, rng.reg(), opcode::OP);
}

inline uint32_t gen_div(RNG& rng) {
    return encode_r_type(funct7::MULDIV, rng.reg(), rng.reg(), 
                         funct3::DIV, rng.reg(), opcode::OP);
}

inline uint32_t gen_divu(RNG& rng) {
    return encode_r_type(funct7::MULDIV, rng.reg(), rng.reg(), 
                         funct3::DIVU, rng.reg(), opcode::OP);
}

inline uint32_t gen_rem(RNG& rng) {
    return encode_r_type(funct7::MULDIV, rng.reg(), rng.reg(), 
                         funct3::REM, rng.reg(), opcode::OP);
}

inline uint32_t gen_remu(RNG& rng) {
    return encode_r_type(funct7::MULDIV, rng.reg(), rng.reg(), 
                         funct3::REMU, rng.reg(), opcode::OP);
}

// ============================================================================
// Instruction Type Enum
// ============================================================================

enum class InstrType {
    MUL,
    MULH,
    MULHSU,
    MULHU,
    DIV,
    DIVU,
    REM,
    REMU,
    
    COUNT
};

// Bit for a type in the enable mask
constexpr uint64_t type_bit(InstrType t) { return 1ull << static_cast<unsigned>(t); }

// Named instruction-group masks
namespace groups {
    constexpr uint64_t MULTIPLY = type_bit(InstrType::MUL) | type_bit(InstrType::MULH) |
                                  type_bit(InstrType::MULHSU) | type_bit(InstrType::MULHU);
    constexpr uint64_t DIVIDE   = type_bit(InstrType::DIV) | type_bit(InstrType::DIVU) |
                                  type_bit(InstrType::REM) | type_bit(InstrType::REMU);
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
    
    static constexpr GeneratorFunc generators[] = {
        gen_mul, gen_mulh, gen_mulhsu, gen_mulhu,
        gen_div, gen_divu, gen_rem, gen_remu
    };
    
    static constexpr size_t NUM_INSTR_TYPES = static_cast<size_t>(InstrType::COUNT);

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

    // Enable-mask configuration (see rv32i opgen); default ALL is
    // seed-stable, an empty mask is legalized back to ALL.
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
    
    // Generate a random M instruction, uniformly over the ENABLED types
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
    
    // Generate random multiply instruction
    uint32_t generate_multiply() {
        static const InstrType mul_types[] = {
            InstrType::MUL, InstrType::MULH, InstrType::MULHSU, InstrType::MULHU
        };
        InstrType t = pick_enabled(mul_types);
        return (t == InstrType::COUNT) ? generate_random() : generate(t);
    }
    
    // Generate random divide instruction
    uint32_t generate_divide() {
        static const InstrType div_types[] = {
            InstrType::DIV, InstrType::DIVU, InstrType::REM, InstrType::REMU
        };
        InstrType t = pick_enabled(div_types);
        return (t == InstrType::COUNT) ? generate_random() : generate(t);
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
    
    // Get instruction name
    static const char* instr_name(InstrType type) {
        static const char* names[] = {
            "MUL", "MULH", "MULHSU", "MULHU",
            "DIV", "DIVU", "REM", "REMU"
        };
        return names[static_cast<size_t>(type)];
    }
};

// Static member definition
constexpr OpcodeGenerator::GeneratorFunc OpcodeGenerator::generators[];

// ============================================================================
// Convenience Functions
// ============================================================================

inline uint32_t generate_random_opcode(uint32_t seed = std::random_device{}()) {
    OpcodeGenerator gen(seed);
    return gen.generate_random();
}

inline std::vector<uint32_t> generate_random_opcodes(size_t n, uint32_t seed = std::random_device{}()) {
    OpcodeGenerator gen(seed);
    return gen.generate_sequence(n);
}

} // namespace opgen
} // namespace rv32m
} // namespace riscv

#endif // RISCV_RV32M_OPGEN_HPP
