/*******************************************************************************
 * RISC-V Zifencei Random Opcode Generator
 * 
 * Generates valid FENCE.I instructions.
 * 
 * Note: FENCE.I has only one valid encoding, but the spec says implementations
 * must ignore the rd, rs1, and imm fields. This generator can produce
 * variations with those fields set to non-zero values for testing.
 ******************************************************************************/

#ifndef RISCV_ZIFENCEI_OPGEN_HPP
#define RISCV_ZIFENCEI_OPGEN_HPP

#include <cstdint>
#include <random>
#include <vector>

namespace riscv {
namespace zifencei {
namespace opgen {

// ============================================================================
// Opcodes and Function Codes
// ============================================================================

namespace opcode {
    constexpr uint32_t MISC_MEM = 0b0001111;
}

namespace funct3 {
    constexpr uint32_t FENCE_I = 0b001;
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
    
    uint32_t imm12() {
        return std::uniform_int_distribution<uint32_t>(0, 0xFFF)(gen);
    }
    
    bool chance(double p = 0.5) {
        return std::uniform_real_distribution<double>(0.0, 1.0)(gen) < p;
    }
};

// ============================================================================
// Instruction Generators
// ============================================================================

// Standard FENCE.I (all fields zero)
inline uint32_t gen_fence_i_standard(RNG&) {
    return (funct3::FENCE_I << 12) | opcode::MISC_MEM;
}

// FENCE.I with random ignored fields (for testing decoders)
inline uint32_t gen_fence_i_random(RNG& rng) {
    uint32_t rd = rng.reg();
    uint32_t rs1 = rng.reg();
    uint32_t imm = rng.imm12();
    
    return (imm << 20) |
           (rs1 << 15) |
           (funct3::FENCE_I << 12) |
           (rd << 7) |
           opcode::MISC_MEM;
}

// ============================================================================
// Instruction Type Enum
// ============================================================================

enum class InstrType {
    FENCE_I,
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
    bool generate_standard_only = true;
    bool type_enabled_ = true;   // uniform API: single-instruction mask
    
public:
    explicit OpcodeGenerator(uint32_t seed = std::random_device{}()) : rng(seed) {}
    
    void seed(uint32_t s) { rng.seed(s); }
    
    // If true, only generate canonical FENCE.I (all fields zero)
    // If false, randomize ignored fields for testing
    void set_standard_only(bool standard) {
        generate_standard_only = standard;
    }

    // Enable-mask configuration, uniform with the other generators
    // (only one instruction type exists here; an empty mask is legalized
    // back to enabled).
    void set_enabled_mask(uint64_t mask) { type_enabled_ = (mask != 0); }
    uint64_t get_enabled_mask() const { return type_enabled_ ? 1ull : 0ull; }
    void enable(InstrType, bool on = true) { type_enabled_ = on; }

    uint32_t generate(InstrType type) {
        (void)type;  // Only one type
        if (generate_standard_only) {
            return gen_fence_i_standard(rng);
        } else {
            return gen_fence_i_random(rng);
        }
    }
    
    uint32_t generate_random() {
        return generate(InstrType::FENCE_I);
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
        switch (type) {
            case InstrType::FENCE_I: return "FENCE.I";
            default: return "UNKNOWN";
        }
    }
};

// ============================================================================
// Convenience Functions
// ============================================================================

inline uint32_t generate_fence_i() {
    return (funct3::FENCE_I << 12) | opcode::MISC_MEM;
}

} // namespace opgen
} // namespace zifencei
} // namespace riscv

#endif // RISCV_ZIFENCEI_OPGEN_HPP
