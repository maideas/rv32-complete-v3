/*******************************************************************************
 * RISC-V RV32F Single-Precision Floating Point Extension Model
 * 
 * Decoder and executor for all RV32F floating-point instructions.
 * Uses injected Bus interface for memory access.
 * 
 * Instructions covered:
 *   Load/Store:
 *     - FLW (Floating Load Word)
 *     - FSW (Floating Store Word)
 *   
 *   Computational:
 *     - FADD.S, FSUB.S, FMUL.S, FDIV.S (Arithmetic)
 *     - FSQRT.S (Square Root)
 *     - FMIN.S, FMAX.S (Min/Max)
 *     - FMADD.S, FMSUB.S, FNMADD.S, FNMSUB.S (Fused Multiply-Add)
 *   
 *   Conversion:
 *     - FCVT.W.S, FCVT.WU.S (Float to Integer)
 *     - FCVT.S.W, FCVT.S.WU (Integer to Float)
 *   
 *   Move:
 *     - FMV.X.W (Float bits to Integer register)
 *     - FMV.W.X (Integer to Float bits)
 *   
 *   Compare:
 *     - FEQ.S, FLT.S, FLE.S (Comparison to integer register)
 *   
 *   Sign Injection:
 *     - FSGNJ.S, FSGNJN.S, FSGNJX.S
 *   
 *   Classification:
 *     - FCLASS.S (Classify floating-point value)
 * 
 * Rounding Modes (rm field):
 *   000 = RNE (Round to Nearest, ties to Even)
 *   001 = RTZ (Round towards Zero)
 *   010 = RDN (Round Down, towards -∞)
 *   011 = RUP (Round Up, towards +∞)
 *   100 = RMM (Round to Nearest, ties to Max Magnitude)
 *   111 = DYN (Dynamic, use frm CSR)
 * 
 * Implementation notes (host-FP based model):
 *   - All arithmetic results that are NaN are canonicalized to 0x7FC00000
 *     as required by the RISC-V specification.
 *   - RNE/RTZ/RDN/RUP arithmetic is bit-exact via the host FPU (IEEE 754
 *     binary32 with single rounding; FMA uses fmaf). RMM arithmetic is
 *     approximated with RNE because hosts lack that mode; RMM differs from
 *     RNE only for exact ties of the infinitely precise result. Integer
 *     conversions implement all five rounding modes exactly.
 *   - The host rounding mode is saved and restored around each instruction.
 ******************************************************************************/

#ifndef RISCV_RV32F_HPP
#define RISCV_RV32F_HPP

#include "riscv_common.hpp"
#include <cmath>
#include <cfenv>
#include <limits>
#include <cstring>

namespace riscv {
namespace rv32f {

// ============================================================================
// Floating Point Register File
// ============================================================================

class FRegFile {
    uint32_t regs[32] = {0};  // Store as raw bits
    
public:
    // Read raw bits
    uint32_t read_bits(uint8_t idx) const {
        return regs[idx & 0x1F];
    }
    
    // Write raw bits
    void write_bits(uint8_t idx, uint32_t val) {
        regs[idx & 0x1F] = val;
    }
    
    // Read as float
    float read(uint8_t idx) const {
        uint32_t bits = regs[idx & 0x1F];
        float f;
        std::memcpy(&f, &bits, sizeof(f));
        return f;
    }
    
    // Write as float
    void write(uint8_t idx, float val) {
        uint32_t bits;
        std::memcpy(&bits, &val, sizeof(bits));
        regs[idx & 0x1F] = bits;
    }
    
    // NaN boxing helpers (for RV64F compatibility, not strictly needed for RV32F)
    static bool is_nan_boxed(uint64_t val) {
        return (val >> 32) == 0xFFFFFFFF;
    }
};

// ============================================================================
// Floating Point CSRs (FCSR)
// ============================================================================

// Rounding modes
enum class RoundingMode : uint8_t {
    RNE = 0b000,  // Round to Nearest, ties to Even
    RTZ = 0b001,  // Round towards Zero
    RDN = 0b010,  // Round Down (towards -∞)
    RUP = 0b011,  // Round Up (towards +∞)
    RMM = 0b100,  // Round to Nearest, ties to Max Magnitude
    // 101, 110 reserved
    DYN = 0b111   // Dynamic (use frm CSR)
};

// Exception flags (fflags)
struct FFlags {
    bool NX : 1;  // Inexact
    bool UF : 1;  // Underflow
    bool OF : 1;  // Overflow
    bool DZ : 1;  // Divide by Zero
    bool NV : 1;  // Invalid Operation
    
    FFlags() : NX(false), UF(false), OF(false), DZ(false), NV(false) {}
    
    uint8_t to_bits() const {
        return (NV << 4) | (DZ << 3) | (OF << 2) | (UF << 1) | NX;
    }
    
    void from_bits(uint8_t b) {
        NX = b & 1;
        UF = (b >> 1) & 1;
        OF = (b >> 2) & 1;
        DZ = (b >> 3) & 1;
        NV = (b >> 4) & 1;
    }
    
    void clear() { NX = UF = OF = DZ = NV = false; }
    
    void merge(const FFlags& other) {
        NX |= other.NX;
        UF |= other.UF;
        OF |= other.OF;
        DZ |= other.DZ;
        NV |= other.NV;
    }
};

// Combined FCSR
class FCSR {
public:
    FFlags fflags;
    RoundingMode frm = RoundingMode::RNE;
    
    uint32_t read() const {
        return (static_cast<uint32_t>(frm) << 5) | fflags.to_bits();
    }
    
    void write(uint32_t val) {
        fflags.from_bits(val & 0x1F);
        frm = static_cast<RoundingMode>((val >> 5) & 0x7);
    }
    
    // Get effective rounding mode (resolve DYN)
    RoundingMode effective_rm(RoundingMode instr_rm) const {
        return (instr_rm == RoundingMode::DYN) ? frm : instr_rm;
    }
};

// ============================================================================
// Float Classification
// ============================================================================

enum class FClass : uint32_t {
    NEG_INF      = 1 << 0,   // Negative infinity
    NEG_NORMAL   = 1 << 1,   // Negative normal
    NEG_SUBNORM  = 1 << 2,   // Negative subnormal
    NEG_ZERO     = 1 << 3,   // Negative zero
    POS_ZERO     = 1 << 4,   // Positive zero
    POS_SUBNORM  = 1 << 5,   // Positive subnormal
    POS_NORMAL   = 1 << 6,   // Positive normal
    POS_INF      = 1 << 7,   // Positive infinity
    SIGNALING_NAN= 1 << 8,   // Signaling NaN
    QUIET_NAN    = 1 << 9    // Quiet NaN
};

inline uint32_t classify_float(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    
    bool sign = (bits >> 31) & 1;
    uint32_t exp = (bits >> 23) & 0xFF;
    uint32_t frac = bits & 0x7FFFFF;
    
    if (exp == 0xFF) {
        if (frac == 0) {
            return sign ? static_cast<uint32_t>(FClass::NEG_INF) 
                        : static_cast<uint32_t>(FClass::POS_INF);
        } else if (frac & 0x400000) {
            return static_cast<uint32_t>(FClass::QUIET_NAN);
        } else {
            return static_cast<uint32_t>(FClass::SIGNALING_NAN);
        }
    } else if (exp == 0) {
        if (frac == 0) {
            return sign ? static_cast<uint32_t>(FClass::NEG_ZERO)
                        : static_cast<uint32_t>(FClass::POS_ZERO);
        } else {
            return sign ? static_cast<uint32_t>(FClass::NEG_SUBNORM)
                        : static_cast<uint32_t>(FClass::POS_SUBNORM);
        }
    } else {
        return sign ? static_cast<uint32_t>(FClass::NEG_NORMAL)
                    : static_cast<uint32_t>(FClass::POS_NORMAL);
    }
}

// Canonical NaN
inline float canonical_nan() {
    uint32_t bits = 0x7FC00000;  // Quiet NaN
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

// ============================================================================
// Instruction Types
// ============================================================================

enum class InstrType {
    // Load/Store
    FLW,
    FSW,
    
    // Computational
    FADD_S,
    FSUB_S,
    FMUL_S,
    FDIV_S,
    FSQRT_S,
    FMIN_S,
    FMAX_S,
    
    // Fused Multiply-Add
    FMADD_S,
    FMSUB_S,
    FNMADD_S,
    FNMSUB_S,
    
    // Conversion
    FCVT_W_S,   // Float to signed int
    FCVT_WU_S,  // Float to unsigned int
    FCVT_S_W,   // Signed int to float
    FCVT_S_WU,  // Unsigned int to float
    
    // Move
    FMV_X_W,    // Float bits to integer reg
    FMV_W_X,    // Integer to float bits
    
    // Compare
    FEQ_S,
    FLT_S,
    FLE_S,
    
    // Sign Injection
    FSGNJ_S,
    FSGNJN_S,
    FSGNJX_S,
    
    // Classification
    FCLASS_S,
    
    ILLEGAL
};

// ============================================================================
// Decoded Instruction
// ============================================================================

struct DecodedInstr {
    InstrType type;
    uint8_t rd;         // Destination register (int or float depending on instr)
    uint8_t rs1;        // Source register 1
    uint8_t rs2;        // Source register 2
    uint8_t rs3;        // Source register 3 (for fused multiply-add)
    uint8_t rm;         // Rounding mode
    int32_t imm;        // Immediate (for load/store)
    uint8_t funct7;
    uint8_t funct3;
    uint32_t raw;
    
    std::string mnemonic() const;
    
    RoundingMode rounding_mode() const {
        return static_cast<RoundingMode>(rm);
    }
};

// ============================================================================
// Execution Result
// ============================================================================

struct ExecResult {
    bool valid;
    FFlags flags;           // Exception flags raised
    uint32_t int_result;    // Integer result (for FMV.X.W, FCVT, compare)
    float float_result;     // Float result
    bool wrote_int_reg;     // Result goes to integer register
    bool wrote_float_reg;   // Result goes to float register
    bool memory_read;
    bool memory_written;
    uint32_t trap_cause;    // Exception cause when !valid
    uint32_t trap_value;    // Value for mtval (faulting address / instruction)
    std::string error;
};

// ============================================================================
// Opcodes and Function Codes
// ============================================================================

namespace opcode {
    constexpr uint8_t LOAD_FP  = 0b0000111;  // FLW
    constexpr uint8_t STORE_FP = 0b0100111;  // FSW
    constexpr uint8_t MADD     = 0b1000011;  // FMADD.S
    constexpr uint8_t MSUB     = 0b1000111;  // FMSUB.S
    constexpr uint8_t NMSUB    = 0b1001011;  // FNMSUB.S
    constexpr uint8_t NMADD    = 0b1001111;  // FNMADD.S
    constexpr uint8_t OP_FP    = 0b1010011;  // All other F instructions
}

namespace funct7 {
    constexpr uint8_t FADD_S    = 0b0000000;
    constexpr uint8_t FSUB_S    = 0b0000100;
    constexpr uint8_t FMUL_S    = 0b0001000;
    constexpr uint8_t FDIV_S    = 0b0001100;
    constexpr uint8_t FSQRT_S   = 0b0101100;
    constexpr uint8_t FSGNJ_S   = 0b0010000;
    constexpr uint8_t FMINMAX_S = 0b0010100;
    constexpr uint8_t FCVT_W_S  = 0b1100000;
    constexpr uint8_t FMV_FCLASS= 0b1110000;
    constexpr uint8_t FCMP_S    = 0b1010000;
    constexpr uint8_t FCVT_S_W  = 0b1101000;
    constexpr uint8_t FMV_W_X   = 0b1111000;
}

namespace funct3 {
    constexpr uint8_t W = 0b010;  // Word width for load/store
}

// ============================================================================
// Decoder
// ============================================================================

class Decoder {
public:
    DecodedInstr decode(uint32_t instr) const {
        DecodedInstr d;
        d.raw = instr;
        d.type = InstrType::ILLEGAL;
        
        d.rd = bits(instr, 11, 7);
        d.rs1 = bits(instr, 19, 15);
        d.rs2 = bits(instr, 24, 20);
        d.rs3 = bits(instr, 31, 27);
        d.rm = bits(instr, 14, 12);
        d.funct7 = bits(instr, 31, 25);
        d.funct3 = bits(instr, 14, 12);
        d.imm = 0;
        
        uint8_t op = bits(instr, 6, 0);
        
        switch (op) {
            case opcode::LOAD_FP:
                if (d.funct3 == funct3::W) {
                    d.type = InstrType::FLW;
                    d.imm = sign_extend(bits(instr, 31, 20), 12);
                }
                break;
                
            case opcode::STORE_FP:
                if (d.funct3 == funct3::W) {
                    d.type = InstrType::FSW;
                    d.imm = sign_extend(
                        (bits(instr, 31, 25) << 5) | bits(instr, 11, 7), 12);
                }
                break;
                
            case opcode::MADD: {
                uint8_t fmt = bits(instr, 26, 25);
                if (fmt == 0b00) d.type = InstrType::FMADD_S;
                break;
            }
                
            case opcode::MSUB: {
                uint8_t fmt = bits(instr, 26, 25);
                if (fmt == 0b00) d.type = InstrType::FMSUB_S;
                break;
            }
                
            case opcode::NMSUB: {
                uint8_t fmt = bits(instr, 26, 25);
                if (fmt == 0b00) d.type = InstrType::FNMSUB_S;
                break;
            }
                
            case opcode::NMADD: {
                uint8_t fmt = bits(instr, 26, 25);
                if (fmt == 0b00) d.type = InstrType::FNMADD_S;
                break;
            }
                
            case opcode::OP_FP:
                decode_op_fp(d);
                break;
        }
        
        // The statically-reserved rounding modes 101 and 110 make any
        // rm-using instruction illegal (DYN = 111 is validated against
        // frm at execution time).
        if (uses_rm(d.type) && (d.rm == 0b101 || d.rm == 0b110)) {
            d.type = InstrType::ILLEGAL;
        }
        
        return d;
    }
    
public:
    bool is_f_instruction(uint32_t instr) const {
        uint8_t op = bits(instr, 6, 0);
        switch (op) {
            case opcode::LOAD_FP:
            case opcode::STORE_FP:
            case opcode::MADD:
            case opcode::MSUB:
            case opcode::NMSUB:
            case opcode::NMADD:
            case opcode::OP_FP:
                return true;
            default:
                return false;
        }
    }
    
public:
    // Does this instruction type use the rm field as a rounding mode?
    static bool uses_rm(InstrType t) {
        switch (t) {
            case InstrType::FADD_S: case InstrType::FSUB_S:
            case InstrType::FMUL_S: case InstrType::FDIV_S:
            case InstrType::FSQRT_S:
            case InstrType::FMADD_S: case InstrType::FMSUB_S:
            case InstrType::FNMADD_S: case InstrType::FNMSUB_S:
            case InstrType::FCVT_W_S: case InstrType::FCVT_WU_S:
            case InstrType::FCVT_S_W: case InstrType::FCVT_S_WU:
                return true;
            default:
                return false;
        }
    }
    
private:
    void decode_op_fp(DecodedInstr& d) const {
        switch (d.funct7) {
            case funct7::FADD_S:
                d.type = InstrType::FADD_S;
                break;
            case funct7::FSUB_S:
                d.type = InstrType::FSUB_S;
                break;
            case funct7::FMUL_S:
                d.type = InstrType::FMUL_S;
                break;
            case funct7::FDIV_S:
                d.type = InstrType::FDIV_S;
                break;
            case funct7::FSQRT_S:
                if (d.rs2 == 0) d.type = InstrType::FSQRT_S;
                break;
            case funct7::FSGNJ_S:
                switch (d.funct3) {
                    case 0b000: d.type = InstrType::FSGNJ_S; break;
                    case 0b001: d.type = InstrType::FSGNJN_S; break;
                    case 0b010: d.type = InstrType::FSGNJX_S; break;
                }
                break;
            case funct7::FMINMAX_S:
                switch (d.funct3) {
                    case 0b000: d.type = InstrType::FMIN_S; break;
                    case 0b001: d.type = InstrType::FMAX_S; break;
                }
                break;
            case funct7::FCVT_W_S:
                switch (d.rs2) {
                    case 0: d.type = InstrType::FCVT_W_S; break;
                    case 1: d.type = InstrType::FCVT_WU_S; break;
                }
                break;
            case funct7::FMV_FCLASS:
                switch (d.funct3) {
                    case 0b000: 
                        if (d.rs2 == 0) d.type = InstrType::FMV_X_W;
                        break;
                    case 0b001:
                        if (d.rs2 == 0) d.type = InstrType::FCLASS_S;
                        break;
                }
                break;
            case funct7::FCMP_S:
                switch (d.funct3) {
                    case 0b010: d.type = InstrType::FEQ_S; break;
                    case 0b001: d.type = InstrType::FLT_S; break;
                    case 0b000: d.type = InstrType::FLE_S; break;
                }
                break;
            case funct7::FCVT_S_W:
                switch (d.rs2) {
                    case 0: d.type = InstrType::FCVT_S_W; break;
                    case 1: d.type = InstrType::FCVT_S_WU; break;
                }
                break;
            case funct7::FMV_W_X:
                if (d.funct3 == 0 && d.rs2 == 0) d.type = InstrType::FMV_W_X;
                break;
        }
    }
};

// ============================================================================
// Executor
// ============================================================================

class Executor {
public:
    // When false, misaligned FLW/FSW raise address-misaligned exceptions.
    bool allow_misaligned = false;
    
    ExecResult execute(const DecodedInstr& instr, RegFile& iregs, FRegFile& fregs,
                       Bus& bus, FCSR& fcsr) const {
        ExecResult result = {};
        result.valid = true;
        result.wrote_int_reg = false;
        result.wrote_float_reg = false;
        result.memory_read = false;
        result.memory_written = false;
        
        if (instr.type == InstrType::ILLEGAL) {
            result.valid = false;
            result.trap_cause = exception::ILLEGAL_INSTRUCTION;
            result.trap_value = instr.raw;
            result.error = "Illegal F instruction";
            return result;
        }
        
        // Resolve the rounding mode. An instruction with rm = DYN is illegal
        // if frm itself holds an invalid rounding mode (101, 110, 111).
        RoundingMode rm = fcsr.effective_rm(instr.rounding_mode());
        if (Decoder::uses_rm(instr.type) &&
            static_cast<uint8_t>(rm) > static_cast<uint8_t>(RoundingMode::RMM)) {
            result.valid = false;
            result.trap_cause = exception::ILLEGAL_INSTRUCTION;
            result.trap_value = instr.raw;
            result.error = "Invalid dynamic rounding mode";
            return result;
        }
        
        // Save/restore the host FP rounding mode so the model does not
        // perturb the embedding application.
        HostRoundingGuard rounding_guard(rm);
        
        switch (instr.type) {
            case InstrType::FLW: {
                uint32_t addr = iregs.read(instr.rs1) + instr.imm;
                uint32_t val;
                if (do_load32(bus, addr, val, result)) {
                    fregs.write_bits(instr.rd, val);
                    result.wrote_float_reg = true;
                    result.memory_read = true;
                }
                break;
            }
            
            case InstrType::FSW: {
                uint32_t addr = iregs.read(instr.rs1) + instr.imm;
                uint32_t val = fregs.read_bits(instr.rs2);
                if (do_store32(bus, addr, val, result)) {
                    result.memory_written = true;
                }
                break;
            }
            
            case InstrType::FADD_S: {
                float a = fregs.read(instr.rs1);
                float b = fregs.read(instr.rs2);
                std::feclearexcept(FE_ALL_EXCEPT);
                float r = a + b;
                result.float_result = r;
                write_canonical(fregs, instr.rd, r);
                result.wrote_float_reg = true;
                update_flags(result.flags);
                break;
            }
            
            case InstrType::FSUB_S: {
                float a = fregs.read(instr.rs1);
                float b = fregs.read(instr.rs2);
                std::feclearexcept(FE_ALL_EXCEPT);
                float r = a - b;
                result.float_result = r;
                write_canonical(fregs, instr.rd, r);
                result.wrote_float_reg = true;
                update_flags(result.flags);
                break;
            }
            
            case InstrType::FMUL_S: {
                float a = fregs.read(instr.rs1);
                float b = fregs.read(instr.rs2);
                std::feclearexcept(FE_ALL_EXCEPT);
                float r = a * b;
                result.float_result = r;
                write_canonical(fregs, instr.rd, r);
                result.wrote_float_reg = true;
                update_flags(result.flags);
                break;
            }
            
            case InstrType::FDIV_S: {
                float a = fregs.read(instr.rs1);
                float b = fregs.read(instr.rs2);
                std::feclearexcept(FE_ALL_EXCEPT);
                float r = a / b;
                result.float_result = r;
                write_canonical(fregs, instr.rd, r);
                result.wrote_float_reg = true;
                update_flags(result.flags);
                break;
            }
            
            case InstrType::FSQRT_S: {
                float a = fregs.read(instr.rs1);
                std::feclearexcept(FE_ALL_EXCEPT);
                float r = std::sqrt(a);
                result.float_result = r;
                write_canonical(fregs, instr.rd, r);
                result.wrote_float_reg = true;
                update_flags(result.flags);
                break;
            }
            
            case InstrType::FMIN_S: {
                float a = fregs.read(instr.rs1);
                float b = fregs.read(instr.rs2);
                float r;
                if (std::isnan(a) && std::isnan(b)) {
                    r = canonical_nan();
                } else if (std::isnan(a)) {
                    r = b;
                } else if (std::isnan(b)) {
                    r = a;
                } else if (a == 0 && b == 0) {
                    // -0 < +0
                    uint32_t a_bits = fregs.read_bits(instr.rs1);
                    (void)fregs.read_bits(instr.rs2);  // b_bits not needed
                    r = (a_bits & 0x80000000) ? a : b;
                } else {
                    r = (a < b) ? a : b;
                }
                // Signal if either is signaling NaN
                if (is_snan(a) || is_snan(b)) result.flags.NV = true;
                result.float_result = r;
                write_canonical(fregs, instr.rd, r);
                result.wrote_float_reg = true;
                break;
            }
            
            case InstrType::FMAX_S: {
                float a = fregs.read(instr.rs1);
                float b = fregs.read(instr.rs2);
                float r;
                if (std::isnan(a) && std::isnan(b)) {
                    r = canonical_nan();
                } else if (std::isnan(a)) {
                    r = b;
                } else if (std::isnan(b)) {
                    r = a;
                } else if (a == 0 && b == 0) {
                    uint32_t a_bits = fregs.read_bits(instr.rs1);
                    (void)fregs.read_bits(instr.rs2);  // b_bits not needed
                    r = (a_bits & 0x80000000) ? b : a;
                } else {
                    r = (a > b) ? a : b;
                }
                if (is_snan(a) || is_snan(b)) result.flags.NV = true;
                result.float_result = r;
                write_canonical(fregs, instr.rd, r);
                result.wrote_float_reg = true;
                break;
            }
            
            case InstrType::FMADD_S: {
                float a = fregs.read(instr.rs1);
                float b = fregs.read(instr.rs2);
                float c = fregs.read(instr.rs3);
                std::feclearexcept(FE_ALL_EXCEPT);
                float r = std::fma(a, b, c);
                result.float_result = r;
                write_canonical(fregs, instr.rd, r);
                result.wrote_float_reg = true;
                update_flags(result.flags);
                break;
            }
            
            case InstrType::FMSUB_S: {
                float a = fregs.read(instr.rs1);
                float b = fregs.read(instr.rs2);
                float c = fregs.read(instr.rs3);
                std::feclearexcept(FE_ALL_EXCEPT);
                float r = std::fma(a, b, -c);
                result.float_result = r;
                write_canonical(fregs, instr.rd, r);
                result.wrote_float_reg = true;
                update_flags(result.flags);
                break;
            }
            
            case InstrType::FNMADD_S: {
                float a = fregs.read(instr.rs1);
                float b = fregs.read(instr.rs2);
                float c = fregs.read(instr.rs3);
                std::feclearexcept(FE_ALL_EXCEPT);
                float r = -std::fma(a, b, c);
                result.float_result = r;
                write_canonical(fregs, instr.rd, r);
                result.wrote_float_reg = true;
                update_flags(result.flags);
                break;
            }
            
            case InstrType::FNMSUB_S: {
                float a = fregs.read(instr.rs1);
                float b = fregs.read(instr.rs2);
                float c = fregs.read(instr.rs3);
                std::feclearexcept(FE_ALL_EXCEPT);
                float r = -std::fma(a, b, -c);
                result.float_result = r;
                write_canonical(fregs, instr.rd, r);
                result.wrote_float_reg = true;
                update_flags(result.flags);
                break;
            }
            
            case InstrType::FCVT_W_S: {
                // Saturating conversion. NV is raised iff the input is NaN or
                // the *rounded* value is out of [-2^31, 2^31-1]; note that
                // exactly -2^31 is a valid in-range result. NX is raised for
                // any in-range inexact conversion.
                float a = fregs.read(instr.rs1);
                int32_t r;
                if (std::isnan(a)) {
                    r = INT32_MAX;
                    result.flags.NV = true;
                } else {
                    float rounded = round_to_int(a, rm);
                    if (rounded >= 2147483648.0f) {          // >= 2^31
                        r = INT32_MAX;
                        result.flags.NV = true;
                    } else if (rounded < -2147483648.0f) {   // < -2^31
                        r = INT32_MIN;
                        result.flags.NV = true;
                    } else {
                        r = static_cast<int32_t>(rounded);
                        if (rounded != a) result.flags.NX = true;
                    }
                }
                result.int_result = static_cast<uint32_t>(r);
                iregs.write(instr.rd, result.int_result);
                result.wrote_int_reg = true;
                break;
            }
            
            case InstrType::FCVT_WU_S: {
                // Saturating conversion. NV is raised iff the input is NaN or
                // the *rounded* value is out of [0, 2^32-1]; e.g. -0.5 with
                // RTZ rounds to 0 and raises only NX, while -1.0 raises NV.
                float a = fregs.read(instr.rs1);
                uint32_t r;
                if (std::isnan(a)) {
                    r = UINT32_MAX;
                    result.flags.NV = true;
                } else {
                    float rounded = round_to_int(a, rm);
                    if (rounded >= 4294967296.0f) {          // >= 2^32
                        r = UINT32_MAX;
                        result.flags.NV = true;
                    } else if (rounded < 0.0f) {             // rounded below 0
                        r = 0;
                        result.flags.NV = true;
                    } else {
                        r = static_cast<uint32_t>(rounded);
                        if (rounded != a) result.flags.NX = true;
                    }
                }
                result.int_result = r;
                iregs.write(instr.rd, result.int_result);
                result.wrote_int_reg = true;
                break;
            }
            
            case InstrType::FCVT_S_W: {
                int32_t a = static_cast<int32_t>(iregs.read(instr.rs1));
                std::feclearexcept(FE_ALL_EXCEPT);
                float r = static_cast<float>(a);
                result.float_result = r;
                write_canonical(fregs, instr.rd, r);
                result.wrote_float_reg = true;
                update_flags(result.flags);
                break;
            }
            
            case InstrType::FCVT_S_WU: {
                uint32_t a = iregs.read(instr.rs1);
                std::feclearexcept(FE_ALL_EXCEPT);
                float r = static_cast<float>(a);
                result.float_result = r;
                write_canonical(fregs, instr.rd, r);
                result.wrote_float_reg = true;
                update_flags(result.flags);
                break;
            }
            
            case InstrType::FMV_X_W: {
                result.int_result = fregs.read_bits(instr.rs1);
                iregs.write(instr.rd, result.int_result);
                result.wrote_int_reg = true;
                break;
            }
            
            case InstrType::FMV_W_X: {
                fregs.write_bits(instr.rd, iregs.read(instr.rs1));
                result.wrote_float_reg = true;
                break;
            }
            
            case InstrType::FEQ_S: {
                float a = fregs.read(instr.rs1);
                float b = fregs.read(instr.rs2);
                if (is_snan(a) || is_snan(b)) result.flags.NV = true;
                result.int_result = (a == b) ? 1 : 0;
                iregs.write(instr.rd, result.int_result);
                result.wrote_int_reg = true;
                break;
            }
            
            case InstrType::FLT_S: {
                float a = fregs.read(instr.rs1);
                float b = fregs.read(instr.rs2);
                if (std::isnan(a) || std::isnan(b)) result.flags.NV = true;
                result.int_result = (a < b) ? 1 : 0;
                iregs.write(instr.rd, result.int_result);
                result.wrote_int_reg = true;
                break;
            }
            
            case InstrType::FLE_S: {
                float a = fregs.read(instr.rs1);
                float b = fregs.read(instr.rs2);
                if (std::isnan(a) || std::isnan(b)) result.flags.NV = true;
                result.int_result = (a <= b) ? 1 : 0;
                iregs.write(instr.rd, result.int_result);
                result.wrote_int_reg = true;
                break;
            }
            
            case InstrType::FSGNJ_S: {
                uint32_t a = fregs.read_bits(instr.rs1);
                uint32_t b = fregs.read_bits(instr.rs2);
                uint32_t r = (a & 0x7FFFFFFF) | (b & 0x80000000);
                fregs.write_bits(instr.rd, r);
                result.wrote_float_reg = true;
                break;
            }
            
            case InstrType::FSGNJN_S: {
                uint32_t a = fregs.read_bits(instr.rs1);
                uint32_t b = fregs.read_bits(instr.rs2);
                uint32_t r = (a & 0x7FFFFFFF) | (~b & 0x80000000);
                fregs.write_bits(instr.rd, r);
                result.wrote_float_reg = true;
                break;
            }
            
            case InstrType::FSGNJX_S: {
                uint32_t a = fregs.read_bits(instr.rs1);
                uint32_t b = fregs.read_bits(instr.rs2);
                uint32_t r = (a & 0x7FFFFFFF) | ((a ^ b) & 0x80000000);
                fregs.write_bits(instr.rd, r);
                result.wrote_float_reg = true;
                break;
            }
            
            case InstrType::FCLASS_S: {
                float a = fregs.read(instr.rs1);
                result.int_result = classify_float(a);
                iregs.write(instr.rd, result.int_result);
                result.wrote_int_reg = true;
                break;
            }
            
            default:
                result.valid = false;
                result.trap_cause = exception::ILLEGAL_INSTRUCTION;
                result.trap_value = instr.raw;
                result.error = "Unknown F instruction";
                break;
        }
        
        // Merge flags into FCSR
        fcsr.fflags.merge(result.flags);
        
        return result;
    }
    
private:
    /**
     * RAII guard: sets the host FP rounding mode for the duration of one
     * instruction and restores the previous mode afterwards.
     *
     * NOTE ON RMM: the host FPU has no "round to nearest, ties to max
     * magnitude" mode, so RMM arithmetic is approximated with RNE (they
     * differ only for exact ties of the infinitely-precise result).
     * Integer conversions implement RMM exactly via std::round. For
     * bit-exact RMM arithmetic a softfloat backend would be required.
     */
    class HostRoundingGuard {
        int saved;
    public:
        explicit HostRoundingGuard(RoundingMode rm) : saved(std::fegetround()) {
            int host_rm;
            switch (rm) {
                case RoundingMode::RTZ: host_rm = FE_TOWARDZERO; break;
                case RoundingMode::RDN: host_rm = FE_DOWNWARD; break;
                case RoundingMode::RUP: host_rm = FE_UPWARD; break;
                case RoundingMode::RNE:
                case RoundingMode::RMM:   // approximated, see note above
                default:                 host_rm = FE_TONEAREST; break;
            }
            std::fesetround(host_rm);
        }
        ~HostRoundingGuard() { std::fesetround(saved); }
        HostRoundingGuard(const HostRoundingGuard&) = delete;
        HostRoundingGuard& operator=(const HostRoundingGuard&) = delete;
    };
    
    // Write a float result with RISC-V NaN canonicalization: any NaN
    // produced by an arithmetic operation is stored as 0x7FC00000.
    static void write_canonical(FRegFile& fregs, uint8_t rd, float r) {
        if (std::isnan(r)) {
            fregs.write_bits(rd, 0x7FC00000u);
        } else {
            fregs.write(rd, r);
        }
    }
    
    // Word load/store via the shared mem_access helpers.
    bool do_load32(Bus& bus, uint32_t addr, uint32_t& val, ExecResult& result) const {
        uint32_t cause, tval;
        if (mem_access::load(bus, addr, 4, allow_misaligned, val, cause, tval)) {
            return true;
        }
        result.valid = false;
        result.trap_cause = cause;
        result.trap_value = tval;
        result.error = (cause == exception::LOAD_ADDR_MISALIGNED)
                       ? "Load address misaligned" : "Load access fault";
        return false;
    }
    
    bool do_store32(Bus& bus, uint32_t addr, uint32_t data, ExecResult& result) const {
        uint32_t cause, tval;
        if (mem_access::store(bus, addr, 4, data, allow_misaligned, cause, tval)) {
            return true;
        }
        result.valid = false;
        result.trap_cause = cause;
        result.trap_value = tval;
        result.error = (cause == exception::STORE_ADDR_MISALIGNED)
                       ? "Store address misaligned" : "Store access fault";
        return false;
    }
    
    void update_flags(FFlags& flags) const {
        if (std::fetestexcept(FE_INEXACT)) flags.NX = true;
        if (std::fetestexcept(FE_UNDERFLOW)) flags.UF = true;
        if (std::fetestexcept(FE_OVERFLOW)) flags.OF = true;
        if (std::fetestexcept(FE_DIVBYZERO)) flags.DZ = true;
        if (std::fetestexcept(FE_INVALID)) flags.NV = true;
    }
    
    bool is_snan(float f) const {
        uint32_t bits;
        std::memcpy(&bits, &f, sizeof(bits));
        uint32_t exp = (bits >> 23) & 0xFF;
        uint32_t frac = bits & 0x7FFFFF;
        return (exp == 0xFF) && (frac != 0) && !(frac & 0x400000);
    }
    
    // Round f to an integral value under the given RISC-V rounding mode.
    // Every case is independent of the ambient host rounding mode: RNE
    // pins FE_TONEAREST around nearbyint, so this helper stays correct
    // even if called outside a HostRoundingGuard.
    float round_to_int(float f, RoundingMode rm) const {
        switch (rm) {
            case RoundingMode::RTZ: return std::trunc(f);
            case RoundingMode::RDN: return std::floor(f);
            case RoundingMode::RUP: return std::ceil(f);
            case RoundingMode::RMM: return std::round(f);
            case RoundingMode::RNE:
            default: {
                int saved = std::fegetround();
                std::fesetround(FE_TONEAREST);
                float r = std::nearbyint(f);
                std::fesetround(saved);
                return r;
            }
        }
    }
};

// ============================================================================
// Mnemonic Generation
// ============================================================================

inline std::string rm_suffix(uint8_t rm) {
    switch (rm) {
        case 0b000: return ", rne";
        case 0b001: return ", rtz";
        case 0b010: return ", rdn";
        case 0b011: return ", rup";
        case 0b100: return ", rmm";
        case 0b111: return "";  // dynamic (default)
        default: return ", ???";
    }
}

inline std::string DecodedInstr::mnemonic() const {
    auto rd_s = "f" + std::to_string(rd);
    auto rs1_s = "f" + std::to_string(rs1);
    auto rs2_s = "f" + std::to_string(rs2);
    auto rs3_s = "f" + std::to_string(rs3);
    std::string xrd_s = reg_abi_name(rd);
    std::string xrs1_s = reg_abi_name(rs1);
    
    switch (type) {
        case InstrType::FLW:
            return "flw " + rd_s + ", " + std::to_string(imm) + "(" + xrs1_s + ")";
        case InstrType::FSW:
            return "fsw " + rs2_s + ", " + std::to_string(imm) + "(" + xrs1_s + ")";
        case InstrType::FADD_S:
            return "fadd.s " + rd_s + ", " + rs1_s + ", " + rs2_s + rm_suffix(rm);
        case InstrType::FSUB_S:
            return "fsub.s " + rd_s + ", " + rs1_s + ", " + rs2_s + rm_suffix(rm);
        case InstrType::FMUL_S:
            return "fmul.s " + rd_s + ", " + rs1_s + ", " + rs2_s + rm_suffix(rm);
        case InstrType::FDIV_S:
            return "fdiv.s " + rd_s + ", " + rs1_s + ", " + rs2_s + rm_suffix(rm);
        case InstrType::FSQRT_S:
            return "fsqrt.s " + rd_s + ", " + rs1_s + rm_suffix(rm);
        case InstrType::FMIN_S:
            return "fmin.s " + rd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::FMAX_S:
            return "fmax.s " + rd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::FMADD_S:
            return "fmadd.s " + rd_s + ", " + rs1_s + ", " + rs2_s + ", " + rs3_s + rm_suffix(rm);
        case InstrType::FMSUB_S:
            return "fmsub.s " + rd_s + ", " + rs1_s + ", " + rs2_s + ", " + rs3_s + rm_suffix(rm);
        case InstrType::FNMADD_S:
            return "fnmadd.s " + rd_s + ", " + rs1_s + ", " + rs2_s + ", " + rs3_s + rm_suffix(rm);
        case InstrType::FNMSUB_S:
            return "fnmsub.s " + rd_s + ", " + rs1_s + ", " + rs2_s + ", " + rs3_s + rm_suffix(rm);
        case InstrType::FCVT_W_S:
            return "fcvt.w.s " + xrd_s + ", " + rs1_s + rm_suffix(rm);
        case InstrType::FCVT_WU_S:
            return "fcvt.wu.s " + xrd_s + ", " + rs1_s + rm_suffix(rm);
        case InstrType::FCVT_S_W:
            return "fcvt.s.w " + rd_s + ", " + xrs1_s + rm_suffix(rm);
        case InstrType::FCVT_S_WU:
            return "fcvt.s.wu " + rd_s + ", " + xrs1_s + rm_suffix(rm);
        case InstrType::FMV_X_W:
            return "fmv.x.w " + xrd_s + ", " + rs1_s;
        case InstrType::FMV_W_X:
            return "fmv.w.x " + rd_s + ", " + xrs1_s;
        case InstrType::FEQ_S:
            return "feq.s " + xrd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::FLT_S:
            return "flt.s " + xrd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::FLE_S:
            return "fle.s " + xrd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::FSGNJ_S:
            if (rs1 == rs2) return "fmv.s " + rd_s + ", " + rs1_s;  // Pseudo
            return "fsgnj.s " + rd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::FSGNJN_S:
            if (rs1 == rs2) return "fneg.s " + rd_s + ", " + rs1_s;  // Pseudo
            return "fsgnjn.s " + rd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::FSGNJX_S:
            if (rs1 == rs2) return "fabs.s " + rd_s + ", " + rs1_s;  // Pseudo
            return "fsgnjx.s " + rd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::FCLASS_S:
            return "fclass.s " + xrd_s + ", " + rs1_s;
        case InstrType::ILLEGAL:
            return "ILLEGAL";
    }
    return "UNKNOWN";
}

// ============================================================================
// Instruction Encoding Helpers
// ============================================================================

namespace encode {
    // I-type for FLW: imm[11:0] | rs1 | 010 | rd | 0000111
    inline uint32_t flw(uint8_t rd, uint8_t rs1, int32_t imm) {
        return ((imm & 0xFFF) << 20) |
               ((rs1 & 0x1F) << 15) |
               (funct3::W << 12) |
               ((rd & 0x1F) << 7) |
               opcode::LOAD_FP;
    }
    
    // S-type for FSW: imm[11:5] | rs2 | rs1 | 010 | imm[4:0] | 0100111
    inline uint32_t fsw(uint8_t rs2, uint8_t rs1, int32_t imm) {
        return ((imm & 0xFE0) << 20) |
               ((rs2 & 0x1F) << 20) |
               ((rs1 & 0x1F) << 15) |
               (funct3::W << 12) |
               ((imm & 0x1F) << 7) |
               opcode::STORE_FP;
    }
    
    // R-type for most F ops
    inline uint32_t r_type_f(uint8_t funct7, uint8_t rs2, uint8_t rs1, 
                             uint8_t rm, uint8_t rd, uint8_t op) {
        return ((funct7 & 0x7F) << 25) |
               ((rs2 & 0x1F) << 20) |
               ((rs1 & 0x1F) << 15) |
               ((rm & 0x7) << 12) |
               ((rd & 0x1F) << 7) |
               op;
    }
    
    // R4-type for fused multiply-add
    inline uint32_t r4_type(uint8_t rs3, uint8_t rs2, uint8_t rs1,
                            uint8_t rm, uint8_t rd, uint8_t op) {
        return ((rs3 & 0x1F) << 27) |
               (0b00 << 25) |  // fmt = S
               ((rs2 & 0x1F) << 20) |
               ((rs1 & 0x1F) << 15) |
               ((rm & 0x7) << 12) |
               ((rd & 0x1F) << 7) |
               op;
    }
    
    inline uint32_t fadd_s(uint8_t rd, uint8_t rs1, uint8_t rs2, uint8_t rm = 0b111) {
        return r_type_f(funct7::FADD_S, rs2, rs1, rm, rd, opcode::OP_FP);
    }
    
    inline uint32_t fsub_s(uint8_t rd, uint8_t rs1, uint8_t rs2, uint8_t rm = 0b111) {
        return r_type_f(funct7::FSUB_S, rs2, rs1, rm, rd, opcode::OP_FP);
    }
    
    inline uint32_t fmul_s(uint8_t rd, uint8_t rs1, uint8_t rs2, uint8_t rm = 0b111) {
        return r_type_f(funct7::FMUL_S, rs2, rs1, rm, rd, opcode::OP_FP);
    }
    
    inline uint32_t fdiv_s(uint8_t rd, uint8_t rs1, uint8_t rs2, uint8_t rm = 0b111) {
        return r_type_f(funct7::FDIV_S, rs2, rs1, rm, rd, opcode::OP_FP);
    }
    
    inline uint32_t fsqrt_s(uint8_t rd, uint8_t rs1, uint8_t rm = 0b111) {
        return r_type_f(funct7::FSQRT_S, 0, rs1, rm, rd, opcode::OP_FP);
    }
    
    inline uint32_t fmin_s(uint8_t rd, uint8_t rs1, uint8_t rs2) {
        return r_type_f(funct7::FMINMAX_S, rs2, rs1, 0b000, rd, opcode::OP_FP);
    }
    
    inline uint32_t fmax_s(uint8_t rd, uint8_t rs1, uint8_t rs2) {
        return r_type_f(funct7::FMINMAX_S, rs2, rs1, 0b001, rd, opcode::OP_FP);
    }
    
    inline uint32_t fmadd_s(uint8_t rd, uint8_t rs1, uint8_t rs2, uint8_t rs3, uint8_t rm = 0b111) {
        return r4_type(rs3, rs2, rs1, rm, rd, opcode::MADD);
    }
    
    inline uint32_t fmsub_s(uint8_t rd, uint8_t rs1, uint8_t rs2, uint8_t rs3, uint8_t rm = 0b111) {
        return r4_type(rs3, rs2, rs1, rm, rd, opcode::MSUB);
    }
    
    inline uint32_t fnmadd_s(uint8_t rd, uint8_t rs1, uint8_t rs2, uint8_t rs3, uint8_t rm = 0b111) {
        return r4_type(rs3, rs2, rs1, rm, rd, opcode::NMADD);
    }
    
    inline uint32_t fnmsub_s(uint8_t rd, uint8_t rs1, uint8_t rs2, uint8_t rs3, uint8_t rm = 0b111) {
        return r4_type(rs3, rs2, rs1, rm, rd, opcode::NMSUB);
    }
    
    inline uint32_t fcvt_w_s(uint8_t rd, uint8_t rs1, uint8_t rm = 0b111) {
        return r_type_f(funct7::FCVT_W_S, 0, rs1, rm, rd, opcode::OP_FP);
    }
    
    inline uint32_t fcvt_wu_s(uint8_t rd, uint8_t rs1, uint8_t rm = 0b111) {
        return r_type_f(funct7::FCVT_W_S, 1, rs1, rm, rd, opcode::OP_FP);
    }
    
    inline uint32_t fcvt_s_w(uint8_t rd, uint8_t rs1, uint8_t rm = 0b111) {
        return r_type_f(funct7::FCVT_S_W, 0, rs1, rm, rd, opcode::OP_FP);
    }
    
    inline uint32_t fcvt_s_wu(uint8_t rd, uint8_t rs1, uint8_t rm = 0b111) {
        return r_type_f(funct7::FCVT_S_W, 1, rs1, rm, rd, opcode::OP_FP);
    }
    
    inline uint32_t fmv_x_w(uint8_t rd, uint8_t rs1) {
        return r_type_f(funct7::FMV_FCLASS, 0, rs1, 0b000, rd, opcode::OP_FP);
    }
    
    inline uint32_t fmv_w_x(uint8_t rd, uint8_t rs1) {
        return r_type_f(funct7::FMV_W_X, 0, rs1, 0b000, rd, opcode::OP_FP);
    }
    
    inline uint32_t feq_s(uint8_t rd, uint8_t rs1, uint8_t rs2) {
        return r_type_f(funct7::FCMP_S, rs2, rs1, 0b010, rd, opcode::OP_FP);
    }
    
    inline uint32_t flt_s(uint8_t rd, uint8_t rs1, uint8_t rs2) {
        return r_type_f(funct7::FCMP_S, rs2, rs1, 0b001, rd, opcode::OP_FP);
    }
    
    inline uint32_t fle_s(uint8_t rd, uint8_t rs1, uint8_t rs2) {
        return r_type_f(funct7::FCMP_S, rs2, rs1, 0b000, rd, opcode::OP_FP);
    }
    
    inline uint32_t fsgnj_s(uint8_t rd, uint8_t rs1, uint8_t rs2) {
        return r_type_f(funct7::FSGNJ_S, rs2, rs1, 0b000, rd, opcode::OP_FP);
    }
    
    inline uint32_t fsgnjn_s(uint8_t rd, uint8_t rs1, uint8_t rs2) {
        return r_type_f(funct7::FSGNJ_S, rs2, rs1, 0b001, rd, opcode::OP_FP);
    }
    
    inline uint32_t fsgnjx_s(uint8_t rd, uint8_t rs1, uint8_t rs2) {
        return r_type_f(funct7::FSGNJ_S, rs2, rs1, 0b010, rd, opcode::OP_FP);
    }
    
    inline uint32_t fclass_s(uint8_t rd, uint8_t rs1) {
        return r_type_f(funct7::FMV_FCLASS, 0, rs1, 0b001, rd, opcode::OP_FP);
    }
    
    // Pseudo-instructions
    inline uint32_t fmv_s(uint8_t rd, uint8_t rs) { return fsgnj_s(rd, rs, rs); }
    inline uint32_t fneg_s(uint8_t rd, uint8_t rs) { return fsgnjn_s(rd, rs, rs); }
    inline uint32_t fabs_s(uint8_t rd, uint8_t rs) { return fsgnjx_s(rd, rs, rs); }
}

} // namespace rv32f
} // namespace riscv

#endif // RISCV_RV32F_HPP
