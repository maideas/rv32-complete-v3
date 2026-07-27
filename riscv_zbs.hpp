/*******************************************************************************
 * RISC-V Zbs Extension Model
 * 
 * Single-Bit Operations extension (part of Bit Manipulation, Ratified 2021).
 * 
 * This extension adds single-bit manipulation instructions:
 *   - BSET  rd, rs1, rs2: rd = rs1 | (1 << (rs2 & 31))     Set bit
 *   - BSETI rd, rs1, shamt: rd = rs1 | (1 << shamt)        Set bit immediate
 *   - BCLR  rd, rs1, rs2: rd = rs1 & ~(1 << (rs2 & 31))    Clear bit
 *   - BCLRI rd, rs1, shamt: rd = rs1 & ~(1 << shamt)       Clear bit immediate
 *   - BINV  rd, rs1, rs2: rd = rs1 ^ (1 << (rs2 & 31))     Invert bit
 *   - BINVI rd, rs1, shamt: rd = rs1 ^ (1 << shamt)        Invert bit immediate
 *   - BEXT  rd, rs1, rs2: rd = (rs1 >> (rs2 & 31)) & 1     Extract bit
 *   - BEXTI rd, rs1, shamt: rd = (rs1 >> shamt) & 1        Extract bit immediate
 * 
 * Use cases:
 *   - GPIO register manipulation
 *   - Bitfield extraction and modification
 *   - Flag manipulation in status registers
 *   - Efficient bit testing
 ******************************************************************************/

#ifndef RISCV_ZBS_HPP
#define RISCV_ZBS_HPP

#include "riscv_common.hpp"

namespace riscv {
namespace zbs {

// ============================================================================
// Instruction Types
// ============================================================================

enum class InstrType {
    BSET,       // Set bit (register)
    BSETI,      // Set bit (immediate)
    BCLR,       // Clear bit (register)
    BCLRI,      // Clear bit (immediate)
    BINV,       // Invert bit (register)
    BINVI,      // Invert bit (immediate)
    BEXT,       // Extract bit (register)
    BEXTI,      // Extract bit (immediate)
    ILLEGAL
};

// ============================================================================
// Decoded Instruction
// ============================================================================

struct DecodedInstr {
    InstrType type;
    uint8_t rd;
    uint8_t rs1;
    uint8_t rs2;
    uint8_t shamt;
    uint32_t raw;
    
    std::string mnemonic() const;
};

// ============================================================================
// Execution Result
// ============================================================================

struct ExecResult {
    bool valid;
    uint32_t result;
    std::string error;
};

// ============================================================================
// Opcodes and Function Codes
// ============================================================================

namespace opcode {
    constexpr uint8_t OP     = 0b0110011;
    constexpr uint8_t OP_IMM = 0b0010011;
}

namespace funct3 {
    constexpr uint8_t BSET_BCLR_BINV = 0b001;
    constexpr uint8_t BEXT = 0b101;
}

namespace funct7 {
    constexpr uint8_t BSET = 0b0010100;
    constexpr uint8_t BCLR = 0b0100100;
    constexpr uint8_t BINV = 0b0110100;
    constexpr uint8_t BEXT = 0b0100100;
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
        d.shamt = bits(instr, 24, 20);
        
        uint8_t op = bits(instr, 6, 0);
        uint8_t funct3 = bits(instr, 14, 12);
        uint8_t funct7 = bits(instr, 31, 25);
        
        if (op == opcode::OP) {
            if (funct3 == funct3::BSET_BCLR_BINV) {
                switch (funct7) {
                    case funct7::BSET: d.type = InstrType::BSET; break;
                    case funct7::BCLR: d.type = InstrType::BCLR; break;
                    case funct7::BINV: d.type = InstrType::BINV; break;
                }
            } else if (funct3 == funct3::BEXT && funct7 == funct7::BEXT) {
                d.type = InstrType::BEXT;
            }
        } else if (op == opcode::OP_IMM) {
            if (funct3 == funct3::BSET_BCLR_BINV) {
                switch (funct7) {
                    case funct7::BSET: d.type = InstrType::BSETI; break;
                    case funct7::BCLR: d.type = InstrType::BCLRI; break;
                    case funct7::BINV: d.type = InstrType::BINVI; break;
                }
            } else if (funct3 == funct3::BEXT && funct7 == funct7::BEXT) {
                d.type = InstrType::BEXTI;
            }
        }
        
        return d;
    }
    
    bool is_zbs_instruction(uint32_t instr) const {
        DecodedInstr d = decode(instr);
        return d.type != InstrType::ILLEGAL;
    }
};

// ============================================================================
// Executor
// ============================================================================

class Executor {
public:
    ExecResult execute(const DecodedInstr& instr, RegFile& regs) const {
        ExecResult result = {};
        result.valid = true;
        
        if (instr.type == InstrType::ILLEGAL) {
            result.valid = false;
            result.error = "Illegal Zbs instruction";
            return result;
        }
        
        uint32_t rs1_val = regs.read(instr.rs1);
        uint32_t bit_pos;
        
        switch (instr.type) {
            case InstrType::BSET:
            case InstrType::BCLR:
            case InstrType::BINV:
            case InstrType::BEXT:
                bit_pos = regs.read(instr.rs2) & 0x1F;
                break;
            default:
                bit_pos = instr.shamt & 0x1F;
                break;
        }
        
        uint32_t bit_mask = 1u << bit_pos;
        
        switch (instr.type) {
            case InstrType::BSET:
            case InstrType::BSETI:
                result.result = rs1_val | bit_mask;
                break;
            case InstrType::BCLR:
            case InstrType::BCLRI:
                result.result = rs1_val & ~bit_mask;
                break;
            case InstrType::BINV:
            case InstrType::BINVI:
                result.result = rs1_val ^ bit_mask;
                break;
            case InstrType::BEXT:
            case InstrType::BEXTI:
                result.result = (rs1_val >> bit_pos) & 1;
                break;
            default:
                result.valid = false;
                result.error = "Unknown Zbs instruction";
                return result;
        }
        
        regs.write(instr.rd, result.result);
        return result;
    }
};

// ============================================================================
// Mnemonic Generation
// ============================================================================

inline std::string DecodedInstr::mnemonic() const {
    std::string rd_s = reg_abi_name(rd);
    std::string rs1_s = reg_abi_name(rs1);
    std::string rs2_s = reg_abi_name(rs2);
    
    switch (type) {
        case InstrType::BSET:
            return "bset " + rd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::BSETI:
            return "bseti " + rd_s + ", " + rs1_s + ", " + std::to_string(shamt);
        case InstrType::BCLR:
            return "bclr " + rd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::BCLRI:
            return "bclri " + rd_s + ", " + rs1_s + ", " + std::to_string(shamt);
        case InstrType::BINV:
            return "binv " + rd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::BINVI:
            return "binvi " + rd_s + ", " + rs1_s + ", " + std::to_string(shamt);
        case InstrType::BEXT:
            return "bext " + rd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::BEXTI:
            return "bexti " + rd_s + ", " + rs1_s + ", " + std::to_string(shamt);
        case InstrType::ILLEGAL:
            return "ILLEGAL";
    }
    return "UNKNOWN";
}

// ============================================================================
// Instruction Encoding
// ============================================================================

namespace encode {
    inline uint32_t r_type(uint8_t funct7, uint8_t rs2, uint8_t rs1,
                           uint8_t funct3, uint8_t rd, uint8_t op) {
        return (static_cast<uint32_t>(funct7) << 25) |
               (static_cast<uint32_t>(rs2 & 0x1F) << 20) |
               (static_cast<uint32_t>(rs1 & 0x1F) << 15) |
               (static_cast<uint32_t>(funct3) << 12) |
               (static_cast<uint32_t>(rd & 0x1F) << 7) |
               op;
    }
    
    inline uint32_t i_type_shamt(uint8_t funct7, uint8_t shamt, uint8_t rs1,
                                  uint8_t funct3, uint8_t rd, uint8_t op) {
        return (static_cast<uint32_t>(funct7) << 25) |
               (static_cast<uint32_t>(shamt & 0x1F) << 20) |
               (static_cast<uint32_t>(rs1 & 0x1F) << 15) |
               (static_cast<uint32_t>(funct3) << 12) |
               (static_cast<uint32_t>(rd & 0x1F) << 7) |
               op;
    }
    
    inline uint32_t bset(uint8_t rd, uint8_t rs1, uint8_t rs2) {
        return r_type(funct7::BSET, rs2, rs1, funct3::BSET_BCLR_BINV, rd, opcode::OP);
    }
    inline uint32_t bclr(uint8_t rd, uint8_t rs1, uint8_t rs2) {
        return r_type(funct7::BCLR, rs2, rs1, funct3::BSET_BCLR_BINV, rd, opcode::OP);
    }
    inline uint32_t binv(uint8_t rd, uint8_t rs1, uint8_t rs2) {
        return r_type(funct7::BINV, rs2, rs1, funct3::BSET_BCLR_BINV, rd, opcode::OP);
    }
    inline uint32_t bext(uint8_t rd, uint8_t rs1, uint8_t rs2) {
        return r_type(funct7::BEXT, rs2, rs1, funct3::BEXT, rd, opcode::OP);
    }
    
    inline uint32_t bseti(uint8_t rd, uint8_t rs1, uint8_t shamt) {
        return i_type_shamt(funct7::BSET, shamt, rs1, funct3::BSET_BCLR_BINV, rd, opcode::OP_IMM);
    }
    inline uint32_t bclri(uint8_t rd, uint8_t rs1, uint8_t shamt) {
        return i_type_shamt(funct7::BCLR, shamt, rs1, funct3::BSET_BCLR_BINV, rd, opcode::OP_IMM);
    }
    inline uint32_t binvi(uint8_t rd, uint8_t rs1, uint8_t shamt) {
        return i_type_shamt(funct7::BINV, shamt, rs1, funct3::BSET_BCLR_BINV, rd, opcode::OP_IMM);
    }
    inline uint32_t bexti(uint8_t rd, uint8_t rs1, uint8_t shamt) {
        return i_type_shamt(funct7::BEXT, shamt, rs1, funct3::BEXT, rd, opcode::OP_IMM);
    }
}

} // namespace zbs
} // namespace riscv

#endif // RISCV_ZBS_HPP
