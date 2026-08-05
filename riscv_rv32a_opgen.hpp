/*******************************************************************************
 * RISC-V RV32A Random Opcode Generator
 * 
 * Generates valid, random RV32A (Atomic) instructions with all fields 
 * randomized within legal bounds.
 * 
 * All 11 RV32A instructions:
 *   - LR.W, SC.W (Load-Reserved / Store-Conditional)
 *   - AMOSWAP.W, AMOADD.W, AMOXOR.W, AMOAND.W, AMOOR.W
 *   - AMOMIN.W, AMOMAX.W, AMOMINU.W, AMOMAXU.W
 * 
 * Memory ordering bits (aq/rl) are randomly generated.
 ******************************************************************************/

#ifndef RISCV_RV32A_OPGEN_HPP
#define RISCV_RV32A_OPGEN_HPP

#include <cstdint>
#include <random>
#include <vector>

namespace riscv {
namespace rv32a {
namespace opgen {

// ============================================================================
// Opcodes and Function Codes
// ============================================================================

namespace opcode {
    constexpr uint32_t AMO = 0b0101111;
}

namespace funct3 {
    constexpr uint32_t W = 0b010;
}

namespace funct5 {
    constexpr uint32_t LR      = 0b00010;
    constexpr uint32_t SC      = 0b00011;
    constexpr uint32_t AMOSWAP = 0b00001;
    constexpr uint32_t AMOADD  = 0b00000;
    constexpr uint32_t AMOXOR  = 0b00100;
    constexpr uint32_t AMOAND  = 0b01100;
    constexpr uint32_t AMOOR   = 0b01000;
    constexpr uint32_t AMOMIN  = 0b10000;
    constexpr uint32_t AMOMAX  = 0b10100;
    constexpr uint32_t AMOMINU = 0b11000;
    constexpr uint32_t AMOMAXU = 0b11100;
}

// ============================================================================
// Instruction Encoding
// ============================================================================

inline uint32_t encode_amo(uint32_t funct5, bool aq, bool rl,
                            uint32_t rs2, uint32_t rs1, uint32_t rd) {
    return ((funct5 & 0x1F) << 27) |
           ((aq ? 1u : 0u) << 26) |
           ((rl ? 1u : 0u) << 25) |
           ((rs2 & 0x1F) << 20) |
           ((rs1 & 0x1F) << 15) |
           (funct3::W << 12) |
           ((rd & 0x1F) << 7) |
           opcode::AMO;
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
    
    bool bit() {
        return std::uniform_int_distribution<uint32_t>(0, 1)(gen) == 1;
    }
    
    uint32_t range(uint32_t lo, uint32_t hi) {
        return std::uniform_int_distribution<uint32_t>(lo, hi)(gen);
    }
    
    // Generate ordering bits (aq, rl)
    // Returns: 0=none, 1=rl, 2=aq, 3=aqrl
    uint32_t ordering() {
        return std::uniform_int_distribution<uint32_t>(0, 3)(gen);
    }
};

// ============================================================================
// Instruction Generators
// ============================================================================

// LR.W rd, (rs1) - rs2 must be 0
inline uint32_t gen_lr_w(RNG& rng) {
    uint32_t ord = rng.ordering();
    bool aq = (ord >> 1) & 1;
    bool rl = ord & 1;
    return encode_amo(funct5::LR, aq, rl, 0, rng.reg(), rng.reg());
}

// SC.W rd, rs2, (rs1)
inline uint32_t gen_sc_w(RNG& rng) {
    uint32_t ord = rng.ordering();
    bool aq = (ord >> 1) & 1;
    bool rl = ord & 1;
    return encode_amo(funct5::SC, aq, rl, rng.reg(), rng.reg(), rng.reg());
}

// AMOSWAP.W rd, rs2, (rs1)
inline uint32_t gen_amoswap_w(RNG& rng) {
    uint32_t ord = rng.ordering();
    bool aq = (ord >> 1) & 1;
    bool rl = ord & 1;
    return encode_amo(funct5::AMOSWAP, aq, rl, rng.reg(), rng.reg(), rng.reg());
}

// AMOADD.W rd, rs2, (rs1)
inline uint32_t gen_amoadd_w(RNG& rng) {
    uint32_t ord = rng.ordering();
    bool aq = (ord >> 1) & 1;
    bool rl = ord & 1;
    return encode_amo(funct5::AMOADD, aq, rl, rng.reg(), rng.reg(), rng.reg());
}

// AMOXOR.W rd, rs2, (rs1)
inline uint32_t gen_amoxor_w(RNG& rng) {
    uint32_t ord = rng.ordering();
    bool aq = (ord >> 1) & 1;
    bool rl = ord & 1;
    return encode_amo(funct5::AMOXOR, aq, rl, rng.reg(), rng.reg(), rng.reg());
}

// AMOAND.W rd, rs2, (rs1)
inline uint32_t gen_amoand_w(RNG& rng) {
    uint32_t ord = rng.ordering();
    bool aq = (ord >> 1) & 1;
    bool rl = ord & 1;
    return encode_amo(funct5::AMOAND, aq, rl, rng.reg(), rng.reg(), rng.reg());
}

// AMOOR.W rd, rs2, (rs1)
inline uint32_t gen_amoor_w(RNG& rng) {
    uint32_t ord = rng.ordering();
    bool aq = (ord >> 1) & 1;
    bool rl = ord & 1;
    return encode_amo(funct5::AMOOR, aq, rl, rng.reg(), rng.reg(), rng.reg());
}

// AMOMIN.W rd, rs2, (rs1)
inline uint32_t gen_amomin_w(RNG& rng) {
    uint32_t ord = rng.ordering();
    bool aq = (ord >> 1) & 1;
    bool rl = ord & 1;
    return encode_amo(funct5::AMOMIN, aq, rl, rng.reg(), rng.reg(), rng.reg());
}

// AMOMAX.W rd, rs2, (rs1)
inline uint32_t gen_amomax_w(RNG& rng) {
    uint32_t ord = rng.ordering();
    bool aq = (ord >> 1) & 1;
    bool rl = ord & 1;
    return encode_amo(funct5::AMOMAX, aq, rl, rng.reg(), rng.reg(), rng.reg());
}

// AMOMINU.W rd, rs2, (rs1)
inline uint32_t gen_amominu_w(RNG& rng) {
    uint32_t ord = rng.ordering();
    bool aq = (ord >> 1) & 1;
    bool rl = ord & 1;
    return encode_amo(funct5::AMOMINU, aq, rl, rng.reg(), rng.reg(), rng.reg());
}

// AMOMAXU.W rd, rs2, (rs1)
inline uint32_t gen_amomaxu_w(RNG& rng) {
    uint32_t ord = rng.ordering();
    bool aq = (ord >> 1) & 1;
    bool rl = ord & 1;
    return encode_amo(funct5::AMOMAXU, aq, rl, rng.reg(), rng.reg(), rng.reg());
}

// ============================================================================
// Instruction Type Enum
// ============================================================================

enum class InstrType {
    LR_W,
    SC_W,
    AMOSWAP_W,
    AMOADD_W,
    AMOXOR_W,
    AMOAND_W,
    AMOOR_W,
    AMOMIN_W,
    AMOMAX_W,
    AMOMINU_W,
    AMOMAXU_W,
    
    COUNT
};

// Bit for a type in the enable mask
constexpr uint64_t type_bit(InstrType t) { return 1ull << static_cast<unsigned>(t); }

// Named instruction-group masks
namespace groups {
    constexpr uint64_t LR_SC      = type_bit(InstrType::LR_W) | type_bit(InstrType::SC_W);
    constexpr uint64_t AMO_LOGICAL = type_bit(InstrType::AMOSWAP_W) | type_bit(InstrType::AMOXOR_W) |
                                     type_bit(InstrType::AMOAND_W) | type_bit(InstrType::AMOOR_W);
    constexpr uint64_t AMO_ARITH  = type_bit(InstrType::AMOADD_W) |
                                    type_bit(InstrType::AMOMIN_W) | type_bit(InstrType::AMOMAX_W) |
                                    type_bit(InstrType::AMOMINU_W) | type_bit(InstrType::AMOMAXU_W);
    constexpr uint64_t ALL        = ~0ull;
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
        gen_lr_w, gen_sc_w,
        gen_amoswap_w, gen_amoadd_w, gen_amoxor_w, gen_amoand_w, gen_amoor_w,
        gen_amomin_w, gen_amomax_w, gen_amominu_w, gen_amomaxu_w
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
    
    // Generate a random A instruction, uniformly over the ENABLED types
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
    
    // Generate LR/SC pair (useful for testing)
    uint32_t generate_lr() {
        return generate(InstrType::LR_W);
    }
    
    uint32_t generate_sc() {
        return generate(InstrType::SC_W);
    }
    
    // Generate random AMO instruction (not LR/SC)
    uint32_t generate_amo() {
        static const InstrType amo_types[] = {
            InstrType::AMOSWAP_W, InstrType::AMOADD_W, InstrType::AMOXOR_W,
            InstrType::AMOAND_W, InstrType::AMOOR_W,
            InstrType::AMOMIN_W, InstrType::AMOMAX_W,
            InstrType::AMOMINU_W, InstrType::AMOMAXU_W
        };
        InstrType t = pick_enabled(amo_types);
        return (t == InstrType::COUNT) ? generate_random() : generate(t);
    }
    
    // Generate random arithmetic AMO (add, min, max variants)
    uint32_t generate_amo_arithmetic() {
        static const InstrType arith_types[] = {
            InstrType::AMOADD_W,
            InstrType::AMOMIN_W, InstrType::AMOMAX_W,
            InstrType::AMOMINU_W, InstrType::AMOMAXU_W
        };
        InstrType t = pick_enabled(arith_types);
        return (t == InstrType::COUNT) ? generate_random() : generate(t);
    }
    
    // Generate random logical AMO (and, or, xor, swap)
    uint32_t generate_amo_logical() {
        static const InstrType logic_types[] = {
            InstrType::AMOSWAP_W, InstrType::AMOXOR_W,
            InstrType::AMOAND_W, InstrType::AMOOR_W
        };
        InstrType t = pick_enabled(logic_types);
        return (t == InstrType::COUNT) ? generate_random() : generate(t);
    }
    
    // Generate with specific ordering
    uint32_t generate_with_ordering(InstrType type, bool aq, bool rl) {
        uint32_t instr = generate(type);
        // Clear existing aq/rl bits and set new ones
        instr &= ~(0x3 << 25);
        instr |= ((aq ? 1u : 0u) << 26) | ((rl ? 1u : 0u) << 25);
        return instr;
    }

private:
    InstrType random_enabled_type() {
        uint64_t m = enabled_ ? enabled_ : groups::ALL;   // empty -> ALL
        for (;;) {
            auto t = static_cast<InstrType>(rng.range(0, NUM_INSTR_TYPES - 1));
            if ((m >> static_cast<unsigned>(t)) & 1ull) return t;
        }
    }

public:
    // Generate acquire-release pair
    uint32_t generate_acquire() {
        return generate_with_ordering(random_enabled_type(), true, false);
    }
    
    uint32_t generate_release() {
        return generate_with_ordering(random_enabled_type(), false, true);
    }
    
    uint32_t generate_seq_cst() {
        return generate_with_ordering(random_enabled_type(), true, true);
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
            "LR.W", "SC.W",
            "AMOSWAP.W", "AMOADD.W", "AMOXOR.W", "AMOAND.W", "AMOOR.W",
            "AMOMIN.W", "AMOMAX.W", "AMOMINU.W", "AMOMAXU.W"
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
} // namespace rv32a
} // namespace riscv

#endif // RISCV_RV32A_OPGEN_HPP
