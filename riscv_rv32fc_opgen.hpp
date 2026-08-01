/*******************************************************************************
 * RISC-V RV32FC Random Opcode Generator (Zfc)
 * 
 * Generates valid, random 16-bit compressed floating-point instructions
 * with all fields randomized within legal bounds.
 * 
 * Instructions:
 *   - C.FLW (Compressed Floating Load Word)
 *   - C.FSW (Compressed Floating Store Word)
 *   - C.FLWSP (Compressed Floating Load Word, SP-relative)
 *   - C.FSWSP (Compressed Floating Store Word, SP-relative)
 ******************************************************************************/

#ifndef RISCV_RV32FC_OPGEN_HPP
#define RISCV_RV32FC_OPGEN_HPP

#include <cstdint>
#include <random>
#include <vector>

namespace riscv {
namespace rv32fc {
namespace opgen {

// ============================================================================
// Random Number Generator
// ============================================================================

class RNG {
    std::mt19937 gen;
    
public:
    explicit RNG(uint32_t seed = std::random_device{}()) : gen(seed) {}
    
    void seed(uint32_t s) { gen.seed(s); }
    
    // Full 5-bit register (0-31)
    uint32_t reg() {
        return std::uniform_int_distribution<uint32_t>(0, 31)(gen);
    }
    
    // Non-zero 5-bit register (1-31)
    uint32_t reg_nz() {
        return std::uniform_int_distribution<uint32_t>(1, 31)(gen);
    }
    
    // Compressed register (x8-x15 / f8-f15, encoded as 0-7)
    uint32_t creg() {
        return std::uniform_int_distribution<uint32_t>(0, 7)(gen);
    }
    
    // Word-aligned offset for C.FLW/C.FSW (0-124, step 4)
    uint32_t offset_7bit_w() {
        return std::uniform_int_distribution<uint32_t>(0, 31)(gen) * 4;
    }
    
    // Word-aligned offset for C.FLWSP/C.FSWSP (0-252, step 4)
    uint32_t offset_8bit_w() {
        return std::uniform_int_distribution<uint32_t>(0, 63)(gen) * 4;
    }
    
    uint32_t range(uint32_t lo, uint32_t hi) {
        return std::uniform_int_distribution<uint32_t>(lo, hi)(gen);
    }
    
    bool chance(double p = 0.5) {
        return std::uniform_real_distribution<double>(0.0, 1.0)(gen) < p;
    }
};

// ============================================================================
// Instruction Encoders
// ============================================================================

// C.FLW: 011 | uimm[5:3] | rs1' | uimm[2|6] | rd' | 00
inline uint16_t encode_c_flw(uint32_t rd_c, uint32_t rs1_c, uint32_t uimm) {
    return 0b00 |                              // op
           ((rd_c & 0x7) << 2) |               // rd'
           (((uimm >> 6) & 0x1) << 5) |       // uimm[6]
           (((uimm >> 2) & 0x1) << 6) |       // uimm[2]
           ((rs1_c & 0x7) << 7) |              // rs1'
           (((uimm >> 3) & 0x7) << 10) |      // uimm[5:3]
           (0b011 << 13);                      // funct3
}

// C.FSW: 111 | uimm[5:3] | rs1' | uimm[2|6] | rs2' | 00
inline uint16_t encode_c_fsw(uint32_t rs2_c, uint32_t rs1_c, uint32_t uimm) {
    return 0b00 |
           ((rs2_c & 0x7) << 2) |
           (((uimm >> 6) & 0x1) << 5) |
           (((uimm >> 2) & 0x1) << 6) |
           ((rs1_c & 0x7) << 7) |
           (((uimm >> 3) & 0x7) << 10) |
           (0b111 << 13);
}

// C.FLWSP: 011 | uimm[5] | rd | uimm[4:2|7:6] | 10
inline uint16_t encode_c_flwsp(uint32_t rd, uint32_t uimm) {
    return 0b10 |                              // op
           (((uimm >> 6) & 0x3) << 2) |       // uimm[7:6]
           (((uimm >> 2) & 0x7) << 4) |       // uimm[4:2]
           ((rd & 0x1F) << 7) |               // rd
           (((uimm >> 5) & 0x1) << 12) |      // uimm[5]
           (0b011 << 13);                      // funct3
}

// C.FSWSP: 111 | uimm[5:2|7:6] | rs2 | 10
inline uint16_t encode_c_fswsp(uint32_t rs2, uint32_t uimm) {
    return 0b10 |
           ((rs2 & 0x1F) << 2) |
           (((uimm >> 6) & 0x3) << 7) |
           (((uimm >> 2) & 0xF) << 9) |
           (0b111 << 13);
}

// ============================================================================
// Instruction Generators
// ============================================================================

inline uint16_t gen_c_flw(RNG& rng) {
    return encode_c_flw(rng.creg(), rng.creg(), rng.offset_7bit_w());
}

inline uint16_t gen_c_fsw(RNG& rng) {
    return encode_c_fsw(rng.creg(), rng.creg(), rng.offset_7bit_w());
}

inline uint16_t gen_c_flwsp(RNG& rng) {
    // f0 is a valid destination (unlike x0 for C.LWSP)
    return encode_c_flwsp(rng.reg(), rng.offset_8bit_w());
}

inline uint16_t gen_c_fswsp(RNG& rng) {
    return encode_c_fswsp(rng.reg(), rng.offset_8bit_w());
}

// ============================================================================
// Instruction Type Enum
// ============================================================================

enum class InstrType {
    C_FLW,
    C_FSW,
    C_FLWSP,
    C_FSWSP,
    
    COUNT
};

// Bit for a type in the enable mask
constexpr uint64_t type_bit(InstrType t) { return 1ull << static_cast<unsigned>(t); }

// Named instruction-group masks
namespace groups {
    constexpr uint64_t LOADS  = type_bit(InstrType::C_FLW) | type_bit(InstrType::C_FLWSP);
    constexpr uint64_t STORES = type_bit(InstrType::C_FSW) | type_bit(InstrType::C_FSWSP);
    constexpr uint64_t ALL    = ~0ull;
}

// ============================================================================
// Opcode Generator Class
// ============================================================================

class OpcodeGenerator {
public:
    using GeneratorFunc = uint16_t(*)(RNG&);
    
private:
    RNG rng;
    uint64_t enabled_ = groups::ALL;   // per-instruction-type enable mask
    
    static constexpr GeneratorFunc generators[] = {
        gen_c_flw, gen_c_fsw, gen_c_flwsp, gen_c_fswsp
    };
    
    static constexpr size_t NUM_INSTR_TYPES = static_cast<size_t>(InstrType::COUNT);

    // chance()-based pick between two types honoring the mask
    uint16_t pick2(GeneratorFunc a, InstrType ta, GeneratorFunc b, InstrType tb) {
        bool ea = is_enabled(ta), eb = is_enabled(tb);
        if (ea && eb) return rng.chance(0.5) ? a(rng) : b(rng);
        if (ea) return a(rng);
        if (eb) return b(rng);
        return generate_random();
    }
    
public:
    explicit OpcodeGenerator(uint32_t seed = std::random_device{}()) : rng(seed) {}
    
    void seed(uint32_t s) { rng.seed(s); }

    // Enable-mask configuration (see rv32i opgen); default ALL is
    // seed-stable, an empty mask is legalized back to ALL.
    void set_enabled_mask(uint64_t mask) { enabled_ = mask; }
    uint64_t get_enabled_mask() const { return enabled_; }
    void enable(InstrType t, bool on = true) {
        if (on) enabled_ |= type_bit(t); else enabled_ &= ~type_bit(t);
    }
    bool is_enabled(InstrType t) const { return (enabled_ & type_bit(t)) != 0; }

    // Generate a specific instruction type (ignores the enable mask)
    uint16_t generate(InstrType type) {
        return generators[static_cast<size_t>(type)](rng);
    }
    
    // Generate a random Zcf instruction, uniformly over the ENABLED types
    uint16_t generate_random() {
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
    
    uint16_t generate_load() {
        return pick2(gen_c_flw, InstrType::C_FLW, gen_c_flwsp, InstrType::C_FLWSP);
    }
    
    uint16_t generate_store() {
        return pick2(gen_c_fsw, InstrType::C_FSW, gen_c_fswsp, InstrType::C_FSWSP);
    }
    
    uint16_t generate_compressed_reg() {
        return pick2(gen_c_flw, InstrType::C_FLW, gen_c_fsw, InstrType::C_FSW);
    }
    
    uint16_t generate_sp_relative() {
        return pick2(gen_c_flwsp, InstrType::C_FLWSP, gen_c_fswsp, InstrType::C_FSWSP);
    }
    
    std::vector<uint16_t> generate_sequence(size_t n) {
        std::vector<uint16_t> result;
        result.reserve(n);
        for (size_t i = 0; i < n; i++) {
            result.push_back(generate_random());
        }
        return result;
    }
    
    static const char* instr_name(InstrType type) {
        static const char* names[] = {
            "C.FLW", "C.FSW", "C.FLWSP", "C.FSWSP"
        };
        return names[static_cast<size_t>(type)];
    }
};

constexpr OpcodeGenerator::GeneratorFunc OpcodeGenerator::generators[];

inline uint16_t generate_random_opcode(uint32_t seed = std::random_device{}()) {
    OpcodeGenerator gen(seed);
    return gen.generate_random();
}

inline std::vector<uint16_t> generate_random_opcodes(size_t n, uint32_t seed = std::random_device{}()) {
    OpcodeGenerator gen(seed);
    return gen.generate_sequence(n);
}

} // namespace opgen
} // namespace rv32fc
} // namespace riscv

#endif // RISCV_RV32FC_OPGEN_HPP
