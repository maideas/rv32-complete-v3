/*******************************************************************************
 * RISC-V RV32F Random Opcode Generator
 * 
 * Generates valid, random RV32F floating-point instructions with all fields
 * randomized within legal bounds.
 ******************************************************************************/

#ifndef RISCV_RV32F_OPGEN_HPP
#define RISCV_RV32F_OPGEN_HPP

#include <cstdint>
#include <random>
#include <vector>

namespace riscv {
namespace rv32f {
namespace opgen {

// ============================================================================
// Opcodes and Function Codes
// ============================================================================

namespace opcode {
    constexpr uint32_t LOAD_FP  = 0b0000111;
    constexpr uint32_t STORE_FP = 0b0100111;
    constexpr uint32_t MADD     = 0b1000011;
    constexpr uint32_t MSUB     = 0b1000111;
    constexpr uint32_t NMSUB    = 0b1001011;
    constexpr uint32_t NMADD    = 0b1001111;
    constexpr uint32_t OP_FP    = 0b1010011;
}

namespace funct7 {
    constexpr uint32_t FADD_S    = 0b0000000;
    constexpr uint32_t FSUB_S    = 0b0000100;
    constexpr uint32_t FMUL_S    = 0b0001000;
    constexpr uint32_t FDIV_S    = 0b0001100;
    constexpr uint32_t FSQRT_S   = 0b0101100;
    constexpr uint32_t FSGNJ_S   = 0b0010000;
    constexpr uint32_t FMINMAX_S = 0b0010100;
    constexpr uint32_t FCVT_W_S  = 0b1100000;
    constexpr uint32_t FMV_FCLASS= 0b1110000;
    constexpr uint32_t FCMP_S    = 0b1010000;
    constexpr uint32_t FCVT_S_W  = 0b1101000;
    constexpr uint32_t FMV_W_X   = 0b1111000;
}

namespace funct3 {
    constexpr uint32_t W = 0b010;
}

// ============================================================================
// Encoding Helpers
// ============================================================================

inline uint32_t encode_i_type(uint32_t imm, uint32_t rs1, uint32_t funct3,
                               uint32_t rd, uint32_t op) {
    return ((imm & 0xFFF) << 20) |
           ((rs1 & 0x1F) << 15) |
           ((funct3 & 0x7) << 12) |
           ((rd & 0x1F) << 7) |
           (op & 0x7F);
}

inline uint32_t encode_s_type(uint32_t imm, uint32_t rs2, uint32_t rs1,
                               uint32_t funct3, uint32_t op) {
    return ((imm & 0xFE0) << 20) |
           ((rs2 & 0x1F) << 20) |
           ((rs1 & 0x1F) << 15) |
           ((funct3 & 0x7) << 12) |
           ((imm & 0x1F) << 7) |
           (op & 0x7F);
}

inline uint32_t encode_r_type(uint32_t funct7, uint32_t rs2, uint32_t rs1,
                               uint32_t rm, uint32_t rd, uint32_t op) {
    return ((funct7 & 0x7F) << 25) |
           ((rs2 & 0x1F) << 20) |
           ((rs1 & 0x1F) << 15) |
           ((rm & 0x7) << 12) |
           ((rd & 0x1F) << 7) |
           (op & 0x7F);
}

inline uint32_t encode_r4_type(uint32_t rs3, uint32_t rs2, uint32_t rs1,
                                uint32_t rm, uint32_t rd, uint32_t op) {
    return ((rs3 & 0x1F) << 27) |
           (0b00 << 25) |  // fmt = S
           ((rs2 & 0x1F) << 20) |
           ((rs1 & 0x1F) << 15) |
           ((rm & 0x7) << 12) |
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
    
    uint32_t ireg() { return std::uniform_int_distribution<uint32_t>(0, 31)(gen); }
    uint32_t freg() { return std::uniform_int_distribution<uint32_t>(0, 31)(gen); }
    uint32_t reg_nz() { return std::uniform_int_distribution<uint32_t>(1, 31)(gen); }
    
    int32_t imm12() { return std::uniform_int_distribution<int32_t>(-2048, 2047)(gen); }
    
    uint32_t rm() {
        static const uint32_t modes[] = {0, 1, 2, 3, 4, 7};
        return modes[std::uniform_int_distribution<size_t>(0, 5)(gen)];
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

inline uint32_t gen_flw(RNG& rng) {
    return encode_i_type(rng.imm12(), rng.ireg(), funct3::W, rng.freg(), opcode::LOAD_FP);
}

inline uint32_t gen_fsw(RNG& rng) {
    return encode_s_type(rng.imm12(), rng.freg(), rng.ireg(), funct3::W, opcode::STORE_FP);
}

inline uint32_t gen_fadd_s(RNG& rng) {
    return encode_r_type(funct7::FADD_S, rng.freg(), rng.freg(), rng.rm(), rng.freg(), opcode::OP_FP);
}

inline uint32_t gen_fsub_s(RNG& rng) {
    return encode_r_type(funct7::FSUB_S, rng.freg(), rng.freg(), rng.rm(), rng.freg(), opcode::OP_FP);
}

inline uint32_t gen_fmul_s(RNG& rng) {
    return encode_r_type(funct7::FMUL_S, rng.freg(), rng.freg(), rng.rm(), rng.freg(), opcode::OP_FP);
}

inline uint32_t gen_fdiv_s(RNG& rng) {
    return encode_r_type(funct7::FDIV_S, rng.freg(), rng.freg(), rng.rm(), rng.freg(), opcode::OP_FP);
}

inline uint32_t gen_fsqrt_s(RNG& rng) {
    return encode_r_type(funct7::FSQRT_S, 0, rng.freg(), rng.rm(), rng.freg(), opcode::OP_FP);
}

inline uint32_t gen_fmin_s(RNG& rng) {
    return encode_r_type(funct7::FMINMAX_S, rng.freg(), rng.freg(), 0b000, rng.freg(), opcode::OP_FP);
}

inline uint32_t gen_fmax_s(RNG& rng) {
    return encode_r_type(funct7::FMINMAX_S, rng.freg(), rng.freg(), 0b001, rng.freg(), opcode::OP_FP);
}

inline uint32_t gen_fmadd_s(RNG& rng) {
    return encode_r4_type(rng.freg(), rng.freg(), rng.freg(), rng.rm(), rng.freg(), opcode::MADD);
}

inline uint32_t gen_fmsub_s(RNG& rng) {
    return encode_r4_type(rng.freg(), rng.freg(), rng.freg(), rng.rm(), rng.freg(), opcode::MSUB);
}

inline uint32_t gen_fnmadd_s(RNG& rng) {
    return encode_r4_type(rng.freg(), rng.freg(), rng.freg(), rng.rm(), rng.freg(), opcode::NMADD);
}

inline uint32_t gen_fnmsub_s(RNG& rng) {
    return encode_r4_type(rng.freg(), rng.freg(), rng.freg(), rng.rm(), rng.freg(), opcode::NMSUB);
}

inline uint32_t gen_fcvt_w_s(RNG& rng) {
    return encode_r_type(funct7::FCVT_W_S, 0, rng.freg(), rng.rm(), rng.ireg(), opcode::OP_FP);
}

inline uint32_t gen_fcvt_wu_s(RNG& rng) {
    return encode_r_type(funct7::FCVT_W_S, 1, rng.freg(), rng.rm(), rng.ireg(), opcode::OP_FP);
}

inline uint32_t gen_fcvt_s_w(RNG& rng) {
    return encode_r_type(funct7::FCVT_S_W, 0, rng.ireg(), rng.rm(), rng.freg(), opcode::OP_FP);
}

inline uint32_t gen_fcvt_s_wu(RNG& rng) {
    return encode_r_type(funct7::FCVT_S_W, 1, rng.ireg(), rng.rm(), rng.freg(), opcode::OP_FP);
}

inline uint32_t gen_fmv_x_w(RNG& rng) {
    return encode_r_type(funct7::FMV_FCLASS, 0, rng.freg(), 0b000, rng.ireg(), opcode::OP_FP);
}

inline uint32_t gen_fmv_w_x(RNG& rng) {
    return encode_r_type(funct7::FMV_W_X, 0, rng.ireg(), 0b000, rng.freg(), opcode::OP_FP);
}

inline uint32_t gen_feq_s(RNG& rng) {
    return encode_r_type(funct7::FCMP_S, rng.freg(), rng.freg(), 0b010, rng.ireg(), opcode::OP_FP);
}

inline uint32_t gen_flt_s(RNG& rng) {
    return encode_r_type(funct7::FCMP_S, rng.freg(), rng.freg(), 0b001, rng.ireg(), opcode::OP_FP);
}

inline uint32_t gen_fle_s(RNG& rng) {
    return encode_r_type(funct7::FCMP_S, rng.freg(), rng.freg(), 0b000, rng.ireg(), opcode::OP_FP);
}

inline uint32_t gen_fsgnj_s(RNG& rng) {
    return encode_r_type(funct7::FSGNJ_S, rng.freg(), rng.freg(), 0b000, rng.freg(), opcode::OP_FP);
}

inline uint32_t gen_fsgnjn_s(RNG& rng) {
    return encode_r_type(funct7::FSGNJ_S, rng.freg(), rng.freg(), 0b001, rng.freg(), opcode::OP_FP);
}

inline uint32_t gen_fsgnjx_s(RNG& rng) {
    return encode_r_type(funct7::FSGNJ_S, rng.freg(), rng.freg(), 0b010, rng.freg(), opcode::OP_FP);
}

inline uint32_t gen_fclass_s(RNG& rng) {
    return encode_r_type(funct7::FMV_FCLASS, 0, rng.freg(), 0b001, rng.ireg(), opcode::OP_FP);
}

// ============================================================================
// Instruction Type Enum
// ============================================================================

enum class InstrType {
    FLW, FSW,
    FADD_S, FSUB_S, FMUL_S, FDIV_S, FSQRT_S,
    FMIN_S, FMAX_S,
    FMADD_S, FMSUB_S, FNMADD_S, FNMSUB_S,
    FCVT_W_S, FCVT_WU_S, FCVT_S_W, FCVT_S_WU,
    FMV_X_W, FMV_W_X,
    FEQ_S, FLT_S, FLE_S,
    FSGNJ_S, FSGNJN_S, FSGNJX_S,
    FCLASS_S,
    COUNT
};

// Bit for a type in the enable mask
constexpr uint64_t type_bit(InstrType t) { return 1ull << static_cast<unsigned>(t); }

// Named instruction-group masks
namespace groups {
    constexpr uint64_t LOAD_STORE = type_bit(InstrType::FLW) | type_bit(InstrType::FSW);
    constexpr uint64_t ARITH      = type_bit(InstrType::FADD_S) | type_bit(InstrType::FSUB_S) |
                                    type_bit(InstrType::FMUL_S) | type_bit(InstrType::FDIV_S) |
                                    type_bit(InstrType::FSQRT_S);
    constexpr uint64_t MINMAX     = type_bit(InstrType::FMIN_S) | type_bit(InstrType::FMAX_S);
    constexpr uint64_t FMA        = type_bit(InstrType::FMADD_S) | type_bit(InstrType::FMSUB_S) |
                                    type_bit(InstrType::FNMADD_S) | type_bit(InstrType::FNMSUB_S);
    constexpr uint64_t CVT        = type_bit(InstrType::FCVT_W_S) | type_bit(InstrType::FCVT_WU_S) |
                                    type_bit(InstrType::FCVT_S_W) | type_bit(InstrType::FCVT_S_WU);
    constexpr uint64_t MV         = type_bit(InstrType::FMV_X_W) | type_bit(InstrType::FMV_W_X);
    constexpr uint64_t CMP        = type_bit(InstrType::FEQ_S) | type_bit(InstrType::FLT_S) |
                                    type_bit(InstrType::FLE_S);
    constexpr uint64_t SGNJ       = type_bit(InstrType::FSGNJ_S) | type_bit(InstrType::FSGNJN_S) |
                                    type_bit(InstrType::FSGNJX_S);
    constexpr uint64_t CLASSIFY   = type_bit(InstrType::FCLASS_S);
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
        gen_flw, gen_fsw,
        gen_fadd_s, gen_fsub_s, gen_fmul_s, gen_fdiv_s, gen_fsqrt_s,
        gen_fmin_s, gen_fmax_s,
        gen_fmadd_s, gen_fmsub_s, gen_fnmadd_s, gen_fnmsub_s,
        gen_fcvt_w_s, gen_fcvt_wu_s, gen_fcvt_s_w, gen_fcvt_s_wu,
        gen_fmv_x_w, gen_fmv_w_x,
        gen_feq_s, gen_flt_s, gen_fle_s,
        gen_fsgnj_s, gen_fsgnjn_s, gen_fsgnjx_s,
        gen_fclass_s
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
    void set_enabled_mask(uint64_t mask) { enabled_ = mask; }
    uint64_t get_enabled_mask() const { return enabled_; }
    void enable(InstrType t, bool on = true) {
        if (on) enabled_ |= type_bit(t); else enabled_ &= ~type_bit(t);
    }
    bool is_enabled(InstrType t) const { return (enabled_ & type_bit(t)) != 0; }

    // Generate a specific instruction type (ignores the enable mask)
    uint32_t generate(InstrType type) {
        return generators[static_cast<size_t>(type)](rng);
    }
    
    // Generate a random F instruction, uniformly over the ENABLED types
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
    
    uint32_t generate_load_store() {
        bool flw = is_enabled(InstrType::FLW), fsw = is_enabled(InstrType::FSW);
        if (flw && fsw) return rng.chance(0.5) ? gen_flw(rng) : gen_fsw(rng);
        if (flw) return gen_flw(rng);
        if (fsw) return gen_fsw(rng);
        return generate_random();
    }
    
    uint32_t generate_arithmetic() {
        static const InstrType types[] = {
            InstrType::FADD_S, InstrType::FSUB_S, InstrType::FMUL_S,
            InstrType::FDIV_S, InstrType::FSQRT_S
        };
        InstrType t = pick_enabled(types);
        return (t == InstrType::COUNT) ? generate_random() : generate(t);
    }
    
    uint32_t generate_fma() {
        static const InstrType types[] = {
            InstrType::FMADD_S, InstrType::FMSUB_S,
            InstrType::FNMADD_S, InstrType::FNMSUB_S
        };
        InstrType t = pick_enabled(types);
        return (t == InstrType::COUNT) ? generate_random() : generate(t);
    }
    
    uint32_t generate_conversion() {
        static const InstrType types[] = {
            InstrType::FCVT_W_S, InstrType::FCVT_WU_S,
            InstrType::FCVT_S_W, InstrType::FCVT_S_WU
        };
        InstrType t = pick_enabled(types);
        return (t == InstrType::COUNT) ? generate_random() : generate(t);
    }
    
    uint32_t generate_compare() {
        static const InstrType types[] = {
            InstrType::FEQ_S, InstrType::FLT_S, InstrType::FLE_S
        };
        InstrType t = pick_enabled(types);
        return (t == InstrType::COUNT) ? generate_random() : generate(t);
    }
    
    uint32_t generate_sign_inject() {
        static const InstrType types[] = {
            InstrType::FSGNJ_S, InstrType::FSGNJN_S, InstrType::FSGNJX_S
        };
        InstrType t = pick_enabled(types);
        return (t == InstrType::COUNT) ? generate_random() : generate(t);
    }
    
    uint32_t generate_no_memory() {
        static const InstrType types[] = {
            InstrType::FADD_S, InstrType::FSUB_S, InstrType::FMUL_S,
            InstrType::FDIV_S, InstrType::FSQRT_S,
            InstrType::FMIN_S, InstrType::FMAX_S,
            InstrType::FMADD_S, InstrType::FMSUB_S, InstrType::FNMADD_S, InstrType::FNMSUB_S,
            InstrType::FCVT_W_S, InstrType::FCVT_WU_S, InstrType::FCVT_S_W, InstrType::FCVT_S_WU,
            InstrType::FMV_X_W, InstrType::FMV_W_X,
            InstrType::FEQ_S, InstrType::FLT_S, InstrType::FLE_S,
            InstrType::FSGNJ_S, InstrType::FSGNJN_S, InstrType::FSGNJX_S,
            InstrType::FCLASS_S
        };
        InstrType t = pick_enabled(types);
        return (t == InstrType::COUNT) ? generate_random() : generate(t);
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
            "FLW", "FSW",
            "FADD.S", "FSUB.S", "FMUL.S", "FDIV.S", "FSQRT.S",
            "FMIN.S", "FMAX.S",
            "FMADD.S", "FMSUB.S", "FNMADD.S", "FNMSUB.S",
            "FCVT.W.S", "FCVT.WU.S", "FCVT.S.W", "FCVT.S.WU",
            "FMV.X.W", "FMV.W.X",
            "FEQ.S", "FLT.S", "FLE.S",
            "FSGNJ.S", "FSGNJN.S", "FSGNJX.S",
            "FCLASS.S"
        };
        return names[static_cast<size_t>(type)];
    }
};

constexpr OpcodeGenerator::GeneratorFunc OpcodeGenerator::generators[];

// ============================================================================
// Convenience Functions
// ============================================================================

// Generate a single random RV32F opcode
inline uint32_t generate_random_opcode(uint32_t seed = std::random_device{}()) {
    OpcodeGenerator gen(seed);
    return gen.generate_random();
}

// Generate N random RV32F opcodes
inline std::vector<uint32_t> generate_random_opcodes(size_t n, uint32_t seed = std::random_device{}()) {
    OpcodeGenerator gen(seed);
    return gen.generate_sequence(n);
}

} // namespace opgen
} // namespace rv32f
} // namespace riscv

#endif // RISCV_RV32F_OPGEN_HPP
