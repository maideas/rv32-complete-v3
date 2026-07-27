/*******************************************************************************
 * RISC-V Zbs Random Opcode Generator
 * 
 * Generates valid, random Zbs single-bit operation instructions.
 ******************************************************************************/

#ifndef RISCV_ZBS_OPGEN_HPP
#define RISCV_ZBS_OPGEN_HPP

#include <cstdint>
#include <random>
#include <vector>

namespace riscv {
namespace zbs {
namespace opgen {

// ============================================================================
// Opcodes and Function Codes
// ============================================================================

namespace opcode {
    constexpr uint32_t OP     = 0b0110011;
    constexpr uint32_t OP_IMM = 0b0010011;
}

namespace funct3 {
    constexpr uint32_t BSET_BCLR_BINV = 0b001;
    constexpr uint32_t BEXT = 0b101;
}

namespace funct7 {
    constexpr uint32_t BSET = 0b0010100;
    constexpr uint32_t BCLR = 0b0100100;
    constexpr uint32_t BINV = 0b0110100;
    constexpr uint32_t BEXT = 0b0100100;
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

inline uint32_t encode_i_type_shamt(uint32_t funct7, uint32_t shamt, uint32_t rs1,
                                     uint32_t funct3, uint32_t rd, uint32_t op) {
    return ((funct7 & 0x7F) << 25) |
           ((shamt & 0x1F) << 20) |
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

// Register variants
inline uint32_t gen_bset(RNG& rng) {
    return encode_r_type(funct7::BSET, rng.reg(), rng.reg(), 
                         funct3::BSET_BCLR_BINV, rng.reg(), opcode::OP);
}
inline uint32_t gen_bclr(RNG& rng) {
    return encode_r_type(funct7::BCLR, rng.reg(), rng.reg(),
                         funct3::BSET_BCLR_BINV, rng.reg(), opcode::OP);
}
inline uint32_t gen_binv(RNG& rng) {
    return encode_r_type(funct7::BINV, rng.reg(), rng.reg(),
                         funct3::BSET_BCLR_BINV, rng.reg(), opcode::OP);
}
inline uint32_t gen_bext(RNG& rng) {
    return encode_r_type(funct7::BEXT, rng.reg(), rng.reg(),
                         funct3::BEXT, rng.reg(), opcode::OP);
}

// Immediate variants
inline uint32_t gen_bseti(RNG& rng) {
    return encode_i_type_shamt(funct7::BSET, rng.shamt(), rng.reg(),
                                funct3::BSET_BCLR_BINV, rng.reg(), opcode::OP_IMM);
}
inline uint32_t gen_bclri(RNG& rng) {
    return encode_i_type_shamt(funct7::BCLR, rng.shamt(), rng.reg(),
                                funct3::BSET_BCLR_BINV, rng.reg(), opcode::OP_IMM);
}
inline uint32_t gen_binvi(RNG& rng) {
    return encode_i_type_shamt(funct7::BINV, rng.shamt(), rng.reg(),
                                funct3::BSET_BCLR_BINV, rng.reg(), opcode::OP_IMM);
}
inline uint32_t gen_bexti(RNG& rng) {
    return encode_i_type_shamt(funct7::BEXT, rng.shamt(), rng.reg(),
                                funct3::BEXT, rng.reg(), opcode::OP_IMM);
}

// ============================================================================
// Instruction Type Enum
// ============================================================================

enum class InstrType {
    BSET, BSETI,
    BCLR, BCLRI,
    BINV, BINVI,
    BEXT, BEXTI,
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
        gen_bset, gen_bseti,
        gen_bclr, gen_bclri,
        gen_binv, gen_binvi,
        gen_bext, gen_bexti
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
    uint32_t generate_register_variant() {
        static const InstrType types[] = { 
            InstrType::BSET, InstrType::BCLR, InstrType::BINV, InstrType::BEXT 
        };
        return generate(types[rng.range(0, 3)]);
    }
    
    uint32_t generate_immediate_variant() {
        static const InstrType types[] = { 
            InstrType::BSETI, InstrType::BCLRI, InstrType::BINVI, InstrType::BEXTI 
        };
        return generate(types[rng.range(0, 3)]);
    }
    
    uint32_t generate_set_clear() {
        static const InstrType types[] = { 
            InstrType::BSET, InstrType::BSETI, InstrType::BCLR, InstrType::BCLRI 
        };
        return generate(types[rng.range(0, 3)]);
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
            "BSET", "BSETI",
            "BCLR", "BCLRI",
            "BINV", "BINVI",
            "BEXT", "BEXTI"
        };
        return names[static_cast<size_t>(type)];
    }
};

constexpr OpcodeGenerator::GeneratorFunc OpcodeGenerator::generators[];

} // namespace opgen
} // namespace zbs
} // namespace riscv

#endif // RISCV_ZBS_OPGEN_HPP
