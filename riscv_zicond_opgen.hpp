/*******************************************************************************
 * RISC-V Zicond Random Opcode Generator
 * 
 * Generates valid, random Zicond conditional operation instructions.
 ******************************************************************************/

#ifndef RISCV_ZICOND_OPGEN_HPP
#define RISCV_ZICOND_OPGEN_HPP

#include <cstdint>
#include <random>
#include <vector>

namespace riscv {
namespace zicond {
namespace opgen {

// ============================================================================
// Opcodes and Function Codes
// ============================================================================

namespace opcode {
    constexpr uint32_t OP = 0b0110011;
}

// Ratified Zicond encoding: both instructions share funct7 = 0000111 and
// are distinguished by funct3 (czero.eqz = 101, czero.nez = 111).
namespace funct3 {
    constexpr uint32_t CZERO_EQZ = 0b101;
    constexpr uint32_t CZERO_NEZ = 0b111;
}

namespace funct7 {
    constexpr uint32_t CZERO = 0b0000111;
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
    
    bool chance(double p = 0.5) {
        return std::uniform_real_distribution<double>(0.0, 1.0)(gen) < p;
    }
};

// ============================================================================
// Instruction Generators
// ============================================================================

inline uint32_t gen_czero_eqz(RNG& rng) {
    return encode_r_type(funct7::CZERO, rng.reg(), rng.reg(), 
                         funct3::CZERO_EQZ, rng.reg(), opcode::OP);
}

inline uint32_t gen_czero_nez(RNG& rng) {
    return encode_r_type(funct7::CZERO, rng.reg(), rng.reg(),
                         funct3::CZERO_NEZ, rng.reg(), opcode::OP);
}

// ============================================================================
// Instruction Type Enum
// ============================================================================

enum class InstrType {
    CZERO_EQZ,
    CZERO_NEZ,
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
        gen_czero_eqz,
        gen_czero_nez
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
            "CZERO.EQZ",
            "CZERO.NEZ"
        };
        return names[static_cast<size_t>(type)];
    }
};

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
} // namespace zicond
} // namespace riscv

#endif // RISCV_ZICOND_OPGEN_HPP
