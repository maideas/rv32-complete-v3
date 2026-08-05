/*******************************************************************************
 * RISC-V Zba Random Opcode Generator
 * 
 * Generates valid, random Zba address generation instructions.
 ******************************************************************************/

#ifndef RISCV_ZBA_OPGEN_HPP
#define RISCV_ZBA_OPGEN_HPP

#include <cstdint>
#include <random>
#include <vector>

namespace riscv {
namespace zba {
namespace opgen {

// ============================================================================
// Opcodes and Function Codes
// ============================================================================

namespace opcode {
    constexpr uint32_t OP = 0b0110011;
}

namespace funct3 {
    constexpr uint32_t SH1ADD = 0b010;
    constexpr uint32_t SH2ADD = 0b100;
    constexpr uint32_t SH3ADD = 0b110;
}

namespace funct7 {
    constexpr uint32_t SHADD = 0b0010000;
}

// ============================================================================
// Encoding Helper
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
    
    uint32_t range(uint32_t lo, uint32_t hi) {
        return std::uniform_int_distribution<uint32_t>(lo, hi)(gen);
    }
};

// ============================================================================
// Instruction Generators
// ============================================================================

inline uint32_t gen_sh1add(RNG& rng) {
    return encode_r_type(funct7::SHADD, rng.reg(), rng.reg(),
                         funct3::SH1ADD, rng.reg(), opcode::OP);
}

inline uint32_t gen_sh2add(RNG& rng) {
    return encode_r_type(funct7::SHADD, rng.reg(), rng.reg(),
                         funct3::SH2ADD, rng.reg(), opcode::OP);
}

inline uint32_t gen_sh3add(RNG& rng) {
    return encode_r_type(funct7::SHADD, rng.reg(), rng.reg(),
                         funct3::SH3ADD, rng.reg(), opcode::OP);
}

// ============================================================================
// Instruction Type Enum
// ============================================================================

enum class InstrType {
    SH1ADD,
    SH2ADD,
    SH3ADD,
    COUNT
};

// Bit for a type in the enable mask
constexpr uint64_t type_bit(InstrType t) { return 1ull << static_cast<unsigned>(t); }

// Named instruction-group masks
namespace groups {
    constexpr uint64_t SHADD = type_bit(InstrType::SH1ADD) |
                               type_bit(InstrType::SH2ADD) |
                               type_bit(InstrType::SH3ADD);
    constexpr uint64_t ALL   = ~0ull;
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
        gen_sh1add,
        gen_sh2add,
        gen_sh3add
    };
    
    static constexpr size_t NUM_INSTR_TYPES = static_cast<size_t>(InstrType::COUNT);
    
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
    
    // Generate a random Zba instruction, uniformly over the ENABLED types
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
            "SH1ADD",
            "SH2ADD",
            "SH3ADD"
        };
        return names[static_cast<size_t>(type)];
    }
};

constexpr OpcodeGenerator::GeneratorFunc OpcodeGenerator::generators[];

// ============================================================================
// Convenience Functions
// ============================================================================

// Generate a single random Zba opcode
inline uint32_t generate_random_opcode(uint32_t seed = std::random_device{}()) {
    OpcodeGenerator gen(seed);
    return gen.generate_random();
}

// Generate N random Zba opcodes
inline std::vector<uint32_t> generate_random_opcodes(size_t n, uint32_t seed = std::random_device{}()) {
    OpcodeGenerator gen(seed);
    return gen.generate_sequence(n);
}

} // namespace opgen
} // namespace zba
} // namespace riscv

#endif // RISCV_ZBA_OPGEN_HPP
