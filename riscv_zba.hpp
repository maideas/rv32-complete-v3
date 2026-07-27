/*******************************************************************************
 * RISC-V Zba Extension Model
 * 
 * Address Generation extension (part of Bit Manipulation, Ratified 2021).
 * 
 * This extension adds shifted-add instructions for efficient address calculation:
 *   - SH1ADD rd, rs1, rs2: rd = (rs1 << 1) + rs2
 *   - SH2ADD rd, rs1, rs2: rd = (rs1 << 2) + rs2
 *   - SH3ADD rd, rs1, rs2: rd = (rs1 << 3) + rs2
 * 
 * Use cases:
 *   - Array indexing: &arr[i] = base + (i << log2(sizeof(element)))
 *   - Structure field access with scaled offsets
 *   - Polynomial evaluation (Horner's method)
 *   - LEA-style address generation (like x86)
 * 
 * Examples:
 *   // Access int array: int *arr; arr[i]
 *   SH2ADD t0, i_reg, base_reg   // t0 = base + i*4
 *   LW a0, 0(t0)
 * 
 *   // Access struct with 8-byte elements
 *   SH3ADD t0, index, base       // t0 = base + index*8
 * 
 * Note: RV64 also has SH1ADD.UW, SH2ADD.UW, SH3ADD.UW, ADD.UW, SLLI.UW
 * which zero-extend the first operand. These are not in RV32 Zba.
 ******************************************************************************/

#ifndef RISCV_ZBA_HPP
#define RISCV_ZBA_HPP

#include "riscv_common.hpp"

namespace riscv {
namespace zba {

// ============================================================================
// Instruction Types
// ============================================================================

enum class InstrType {
    SH1ADD,     // rd = (rs1 << 1) + rs2
    SH2ADD,     // rd = (rs1 << 2) + rs2
    SH3ADD,     // rd = (rs1 << 3) + rs2
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
    constexpr uint8_t OP = 0b0110011;
}

namespace funct3 {
    constexpr uint8_t SH1ADD = 0b010;
    constexpr uint8_t SH2ADD = 0b100;
    constexpr uint8_t SH3ADD = 0b110;
}

namespace funct7 {
    constexpr uint8_t SHADD = 0b0010000;  // Same for all SHxADD
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
        
        uint8_t op = bits(instr, 6, 0);
        uint8_t funct3 = bits(instr, 14, 12);
        uint8_t funct7 = bits(instr, 31, 25);
        
        if (op == opcode::OP && funct7 == funct7::SHADD) {
            switch (funct3) {
                case funct3::SH1ADD: d.type = InstrType::SH1ADD; break;
                case funct3::SH2ADD: d.type = InstrType::SH2ADD; break;
                case funct3::SH3ADD: d.type = InstrType::SH3ADD; break;
            }
        }
        
        return d;
    }
    
    bool is_zba_instruction(uint32_t instr) const {
        uint8_t op = bits(instr, 6, 0);
        uint8_t funct3 = bits(instr, 14, 12);
        uint8_t funct7 = bits(instr, 31, 25);
        
        if (op != opcode::OP || funct7 != funct7::SHADD) return false;
        return (funct3 == funct3::SH1ADD || 
                funct3 == funct3::SH2ADD || 
                funct3 == funct3::SH3ADD);
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
            result.error = "Illegal Zba instruction";
            return result;
        }
        
        uint32_t rs1_val = regs.read(instr.rs1);
        uint32_t rs2_val = regs.read(instr.rs2);
        
        switch (instr.type) {
            case InstrType::SH1ADD:
                result.result = (rs1_val << 1) + rs2_val;
                break;
            case InstrType::SH2ADD:
                result.result = (rs1_val << 2) + rs2_val;
                break;
            case InstrType::SH3ADD:
                result.result = (rs1_val << 3) + rs2_val;
                break;
            default:
                result.valid = false;
                result.error = "Unknown Zba instruction";
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
        case InstrType::SH1ADD:
            return "sh1add " + rd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::SH2ADD:
            return "sh2add " + rd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::SH3ADD:
            return "sh3add " + rd_s + ", " + rs1_s + ", " + rs2_s;
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
    
    inline uint32_t sh1add(uint8_t rd, uint8_t rs1, uint8_t rs2) {
        return r_type(funct7::SHADD, rs2, rs1, funct3::SH1ADD, rd, opcode::OP);
    }
    
    inline uint32_t sh2add(uint8_t rd, uint8_t rs1, uint8_t rs2) {
        return r_type(funct7::SHADD, rs2, rs1, funct3::SH2ADD, rd, opcode::OP);
    }
    
    inline uint32_t sh3add(uint8_t rd, uint8_t rs1, uint8_t rs2) {
        return r_type(funct7::SHADD, rs2, rs1, funct3::SH3ADD, rd, opcode::OP);
    }
}

} // namespace zba
} // namespace riscv

#endif // RISCV_ZBA_HPP
