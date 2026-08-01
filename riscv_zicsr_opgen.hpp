/*******************************************************************************
 * RISC-V Zicsr Extension Opcode Generator
 * 
 * Generates random valid Zicsr (CSR access) instruction encodings for
 * verification stimulus:
 *   CSRRW, CSRRS, CSRRC    (register forms)
 *   CSRRWI, CSRRSI, CSRRCI (immediate forms)
 * 
 * CSR addresses are drawn from the set of CSRs implemented by the
 * reference model (see riscv_zicsr.hpp), so every generated instruction
 * targets an existing CSR. The M-mode CSR pool is always used;
 * medeleg/mideleg join it when S- or U-mode stimulus is enabled, and
 * the supervisor CSRs when S-mode stimulus is enabled
 * (OpcodeGenerator::set_s_mode / set_u_mode). Read-only CSRs are only
 * paired with non-writing forms (rs1 = x0 / uimm = 0) so that the
 * generated stimulus is trap-free on a correct implementation.
 ******************************************************************************/

#ifndef RISCV_ZICSR_OPGEN_HPP
#define RISCV_ZICSR_OPGEN_HPP

#include <cstdint>
#include <random>
#include <vector>

namespace riscv {
namespace zicsr {
namespace opgen {

// ============================================================================
// Opcodes and Function Codes
// ============================================================================

namespace opcode {
    constexpr uint32_t SYSTEM = 0b1110011;
}

namespace funct3 {
    constexpr uint32_t CSRRW  = 0b001;
    constexpr uint32_t CSRRS  = 0b010;
    constexpr uint32_t CSRRC  = 0b011;
    constexpr uint32_t CSRRWI = 0b101;
    constexpr uint32_t CSRRSI = 0b110;
    constexpr uint32_t CSRRCI = 0b111;
}

// ============================================================================
// Encoding Helper
// ============================================================================

// I-type CSR encoding: csr[11:0] | rs1/uimm | funct3 | rd | 1110011
inline uint32_t encode_csr(uint32_t csr, uint32_t rs1_or_uimm,
                           uint32_t funct3, uint32_t rd) {
    return ((csr & 0xFFF) << 20) |
           ((rs1_or_uimm & 0x1F) << 15) |
           ((funct3 & 0x7) << 12) |
           ((rd & 0x1F) << 7) |
           opcode::SYSTEM;
}

// ============================================================================
// Random Number Generator
// ============================================================================

class RNG {
    std::mt19937 gen;
    bool s_mode = false;   // include supervisor CSRs in the pools
    bool u_mode = false;   // include medeleg/mideleg (exist with S or U)

    // CSR pools mirroring the model's implemented set.
    // Machine-mode read/write CSRs that always exist:
    static constexpr uint32_t WR_BASE[] = {
        0x001,  // fflags
        0x002,  // frm
        0x003,  // fcsr
        0x300,  // mstatus
        0x304,  // mie
        0x305,  // mtvec
        0x306,  // mcounteren
        0x310,  // mstatush (WARL zero, writes legal)
        0x30A,  // menvcfg  (WARL zero, writes legal)
        0x31A,  // menvcfgh (WARL zero, writes legal)
        0x340,  // mscratch
        0x341,  // mepc
        0x342,  // mcause
        0x343,  // mtval
        0x344,  // mip
        0xB00,  // mcycle
        0xB02,  // minstret
        0xB80,  // mcycleh
        0xB82,  // minstreth
    };
    // Delegation CSRs: exist when S- or U-mode is implemented.
    static constexpr uint32_t WR_DELEG[] = {
        0x302,  // medeleg
        0x303,  // mideleg
    };
    // Supervisor read/write CSRs: exist when S-mode is implemented.
    // (Writing any of them from M-mode is legal, so they keep the
    // stimulus trap-free.)
    static constexpr uint32_t WR_SMODE[] = {
        0x100,  // sstatus
        0x104,  // sie
        0x105,  // stvec
        0x106,  // scounteren
        0x140,  // sscratch
        0x141,  // sepc
        0x142,  // scause
        0x143,  // stval
        0x144,  // sip
        0x180,  // satp
    };
    // Read-only CSRs that always exist.
    static constexpr uint32_t RO_POOL[] = {
        0xC00,  // cycle
        0xC01,  // time
        0xC02,  // instret
        0xC80,  // cycleh
        0xC81,  // timeh
        0xC82,  // instreth
        0xF11,  // mvendorid
        0xF12,  // marchid
        0xF13,  // mimpid
        0xF14,  // mhartid
        0xF15,  // mconfigptr
        0x301,  // misa (writes ignored, but reads fine)
    };

    template<size_t N>
    uint32_t pick(const uint32_t (&pool)[N]) {
        return pool[range(0, N - 1)];
    }

public:
    explicit RNG(uint32_t seed = std::random_device{}()) : gen(seed) {}

    void seed(uint32_t s) { gen.seed(s); }

    void set_s_mode(bool enabled) { s_mode = enabled; }
    void set_u_mode(bool enabled) { u_mode = enabled; }

    uint32_t reg() {
        return std::uniform_int_distribution<uint32_t>(0, 31)(gen);
    }

    uint32_t uimm5() {
        return std::uniform_int_distribution<uint32_t>(0, 31)(gen);
    }

    uint32_t range(uint32_t lo, uint32_t hi) {
        return std::uniform_int_distribution<uint32_t>(lo, hi)(gen);
    }

    bool chance(double p = 0.5) {
        return std::uniform_real_distribution<double>(0.0, 1.0)(gen) < p;
    }

    // A read/write CSR implemented by the reference model
    uint32_t writable_csr() {
        uint32_t n_base = sizeof(WR_BASE) / sizeof(WR_BASE[0]);
        uint32_t n_deleg = (s_mode || u_mode)
                           ? sizeof(WR_DELEG) / sizeof(WR_DELEG[0]) : 0;
        uint32_t n_smode = s_mode ? sizeof(WR_SMODE) / sizeof(WR_SMODE[0]) : 0;
        uint32_t idx = range(0, n_base + n_deleg + n_smode - 1);
        if (idx < n_base) return WR_BASE[idx];
        idx -= n_base;
        if (idx < n_deleg) return WR_DELEG[idx];
        idx -= n_deleg;
        return WR_SMODE[idx];
    }

    // A read-only CSR implemented by the reference model
    uint32_t readonly_csr() {
        return pick(RO_POOL);
    }

    // Any implemented CSR (mostly writable, sometimes read-only)
    uint32_t csr() {
        return chance(0.8) ? writable_csr() : readonly_csr();
    }
};

// ============================================================================
// Instruction Generators
// ============================================================================

inline uint32_t gen_csrrw(RNG& rng) {
    // CSRRW always writes: use a writable CSR
    return encode_csr(rng.writable_csr(), rng.reg(), funct3::CSRRW, rng.reg());
}

inline uint32_t gen_csrrs(RNG& rng) {
    // rs1 = x0 makes CSRRS a pure read, allowing read-only CSRs
    uint32_t rs1 = rng.chance(0.3) ? 0 : rng.reg();
    uint32_t csr = (rs1 == 0) ? rng.csr() : rng.writable_csr();
    return encode_csr(csr, rs1, funct3::CSRRS, rng.reg());
}

inline uint32_t gen_csrrc(RNG& rng) {
    uint32_t rs1 = rng.chance(0.3) ? 0 : rng.reg();
    uint32_t csr = (rs1 == 0) ? rng.csr() : rng.writable_csr();
    return encode_csr(csr, rs1, funct3::CSRRC, rng.reg());
}

inline uint32_t gen_csrrwi(RNG& rng) {
    return encode_csr(rng.writable_csr(), rng.uimm5(), funct3::CSRRWI, rng.reg());
}

inline uint32_t gen_csrrsi(RNG& rng) {
    uint32_t uimm = rng.chance(0.3) ? 0 : rng.uimm5();
    uint32_t csr = (uimm == 0) ? rng.csr() : rng.writable_csr();
    return encode_csr(csr, uimm, funct3::CSRRSI, rng.reg());
}

inline uint32_t gen_csrrci(RNG& rng) {
    uint32_t uimm = rng.chance(0.3) ? 0 : rng.uimm5();
    uint32_t csr = (uimm == 0) ? rng.csr() : rng.writable_csr();
    return encode_csr(csr, uimm, funct3::CSRRCI, rng.reg());
}

// ============================================================================
// Instruction Type Enum
// ============================================================================

enum class InstrType {
    CSRRW,
    CSRRS,
    CSRRC,
    CSRRWI,
    CSRRSI,
    CSRRCI,
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
        gen_csrrw,
        gen_csrrs,
        gen_csrrc,
        gen_csrrwi,
        gen_csrrsi,
        gen_csrrci
    };
    
    static constexpr size_t NUM_INSTR_TYPES = static_cast<size_t>(InstrType::COUNT);
    
public:
    explicit OpcodeGenerator(uint32_t seed = std::random_device{}()) : rng(seed) {}

    void seed(uint32_t s) { rng.seed(s); }

    // Include medeleg/mideleg and the supervisor CSRs in the generated
    // stimulus (match this to the CPU configuration to stay trap-free).
    void set_s_mode(bool enabled) { rng.set_s_mode(enabled); }
    void set_u_mode(bool enabled) { rng.set_u_mode(enabled); }

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
            "CSRRW",
            "CSRRS",
            "CSRRC",
            "CSRRWI",
            "CSRRSI",
            "CSRRCI"
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
} // namespace zicsr
} // namespace riscv

#endif // RISCV_ZICSR_OPGEN_HPP
