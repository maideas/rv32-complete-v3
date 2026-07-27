/*******************************************************************************
 * RISC-V RV32M Multiply-Divide Extension Model
 * 
 * Decoder and executor for all 8 RV32M instructions.
 ******************************************************************************/

#ifndef RISCV_RV32M_HPP
#define RISCV_RV32M_HPP

#include "riscv_common.hpp"

namespace riscv {
namespace rv32m {

// ============================================================================
// Instruction Types
// ============================================================================

enum class InstrType {
    MUL,      // Multiply (lower 32 bits)
    MULH,     // Multiply High (signed × signed)
    MULHSU,   // Multiply High (signed × unsigned)
    MULHU,    // Multiply High (unsigned × unsigned)
    DIV,      // Divide (signed)
    DIVU,     // Divide (unsigned)
    REM,      // Remainder (signed)
    REMU,     // Remainder (unsigned)
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
    uint8_t funct3;
    uint8_t funct7;
    uint32_t raw;
    
    std::string mnemonic() const;
};

// ============================================================================
// Execution Result
// ============================================================================

struct ExecResult {
    uint32_t result;
    bool valid;
    std::string error;
};

// ============================================================================
// Opcodes and Function Codes
// ============================================================================

namespace opcode {
    constexpr uint8_t OP = 0b0110011;
}

namespace funct7 {
    constexpr uint8_t MULDIV = 0b0000001;
}

namespace funct3 {
    constexpr uint8_t MUL    = 0b000;
    constexpr uint8_t MULH   = 0b001;
    constexpr uint8_t MULHSU = 0b010;
    constexpr uint8_t MULHU  = 0b011;
    constexpr uint8_t DIV    = 0b100;
    constexpr uint8_t DIVU   = 0b101;
    constexpr uint8_t REM    = 0b110;
    constexpr uint8_t REMU   = 0b111;
}

// ============================================================================
// Decoder
// ============================================================================

class Decoder {
public:
    DecodedInstr decode(uint32_t instr) const {
        DecodedInstr d;
        d.raw = instr;
        d.rd = bits(instr, 11, 7);
        d.rs1 = bits(instr, 19, 15);
        d.rs2 = bits(instr, 24, 20);
        d.funct3 = bits(instr, 14, 12);
        d.funct7 = bits(instr, 31, 25);
        d.type = InstrType::ILLEGAL;
        
        uint8_t op = bits(instr, 6, 0);
        if (op != opcode::OP || d.funct7 != funct7::MULDIV) {
            return d;
        }
        
        switch (d.funct3) {
            case funct3::MUL:    d.type = InstrType::MUL;    break;
            case funct3::MULH:   d.type = InstrType::MULH;   break;
            case funct3::MULHSU: d.type = InstrType::MULHSU; break;
            case funct3::MULHU:  d.type = InstrType::MULHU;  break;
            case funct3::DIV:    d.type = InstrType::DIV;    break;
            case funct3::DIVU:   d.type = InstrType::DIVU;   break;
            case funct3::REM:    d.type = InstrType::REM;    break;
            case funct3::REMU:   d.type = InstrType::REMU;   break;
        }
        
        return d;
    }
    
    bool is_m_instruction(uint32_t instr) const {
        uint8_t op = bits(instr, 6, 0);
        uint8_t f7 = bits(instr, 31, 25);
        return (op == opcode::OP) && (f7 == funct7::MULDIV);
    }
};

// ============================================================================
// Executor
// ============================================================================

class Executor {
public:
    ExecResult execute(const DecodedInstr& instr, RegFile& regs) const {
        ExecResult result;
        result.valid = true;
        result.result = 0;
        
        if (instr.type == InstrType::ILLEGAL) {
            result.valid = false;
            result.error = "Illegal M instruction";
            return result;
        }
        
        uint32_t rs1_val = regs.read(instr.rs1);
        uint32_t rs2_val = regs.read(instr.rs2);
        int32_t rs1_signed = static_cast<int32_t>(rs1_val);
        int32_t rs2_signed = static_cast<int32_t>(rs2_val);
        
        switch (instr.type) {
            case InstrType::MUL: {
                result.result = rs1_val * rs2_val;
                break;
            }
            
            case InstrType::MULH: {
                int64_t product = static_cast<int64_t>(rs1_signed) * static_cast<int64_t>(rs2_signed);
                result.result = static_cast<uint32_t>(product >> 32);
                break;
            }
            
            case InstrType::MULHSU: {
                int64_t product = static_cast<int64_t>(rs1_signed) * static_cast<uint64_t>(rs2_val);
                result.result = static_cast<uint32_t>(product >> 32);
                break;
            }
            
            case InstrType::MULHU: {
                uint64_t product = static_cast<uint64_t>(rs1_val) * static_cast<uint64_t>(rs2_val);
                result.result = static_cast<uint32_t>(product >> 32);
                break;
            }
            
            case InstrType::DIV: {
                if (rs2_val == 0) {
                    result.result = 0xFFFFFFFF;  // -1
                } else if (rs1_val == 0x80000000 && rs2_signed == -1) {
                    result.result = 0x80000000;  // Overflow
                } else {
                    result.result = static_cast<uint32_t>(rs1_signed / rs2_signed);
                }
                break;
            }
            
            case InstrType::DIVU: {
                if (rs2_val == 0) {
                    result.result = 0xFFFFFFFF;  // MAX_UINT
                } else {
                    result.result = rs1_val / rs2_val;
                }
                break;
            }
            
            case InstrType::REM: {
                if (rs2_val == 0) {
                    result.result = rs1_val;  // Dividend
                } else if (rs1_val == 0x80000000 && rs2_signed == -1) {
                    result.result = 0;  // Overflow case
                } else {
                    result.result = static_cast<uint32_t>(rs1_signed % rs2_signed);
                }
                break;
            }
            
            case InstrType::REMU: {
                if (rs2_val == 0) {
                    result.result = rs1_val;  // Dividend
                } else {
                    result.result = rs1_val % rs2_val;
                }
                break;
            }
            
            default:
                result.valid = false;
                result.error = "Unknown M instruction";
                break;
        }
        
        if (result.valid) {
            regs.write(instr.rd, result.result);
        }
        
        return result;
    }
};

// ============================================================================
// Mnemonic Generation
// ============================================================================

inline std::string DecodedInstr::mnemonic() const {
    auto rd_s = reg_abi_name(rd);
    auto rs1_s = reg_abi_name(rs1);
    auto rs2_s = reg_abi_name(rs2);
    
    switch (type) {
        case InstrType::MUL:    return std::string("mul ") + rd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::MULH:   return std::string("mulh ") + rd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::MULHSU: return std::string("mulhsu ") + rd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::MULHU:  return std::string("mulhu ") + rd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::DIV:    return std::string("div ") + rd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::DIVU:   return std::string("divu ") + rd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::REM:    return std::string("rem ") + rd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::REMU:   return std::string("remu ") + rd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::ILLEGAL: return "ILLEGAL";
    }
    return "UNKNOWN";
}

// ============================================================================
// Instruction Encoding Helpers
// ============================================================================

namespace encode {
    inline uint32_t m_instr(uint8_t funct3, uint8_t rd, uint8_t rs1, uint8_t rs2) {
        return (static_cast<uint32_t>(funct7::MULDIV) << 25) |
               (static_cast<uint32_t>(rs2 & 0x1F) << 20) |
               (static_cast<uint32_t>(rs1 & 0x1F) << 15) |
               (static_cast<uint32_t>(funct3) << 12) |
               (static_cast<uint32_t>(rd & 0x1F) << 7) |
               opcode::OP;
    }
    
    inline uint32_t mul(uint8_t rd, uint8_t rs1, uint8_t rs2)    { return m_instr(funct3::MUL, rd, rs1, rs2); }
    inline uint32_t mulh(uint8_t rd, uint8_t rs1, uint8_t rs2)   { return m_instr(funct3::MULH, rd, rs1, rs2); }
    inline uint32_t mulhsu(uint8_t rd, uint8_t rs1, uint8_t rs2) { return m_instr(funct3::MULHSU, rd, rs1, rs2); }
    inline uint32_t mulhu(uint8_t rd, uint8_t rs1, uint8_t rs2)  { return m_instr(funct3::MULHU, rd, rs1, rs2); }
    inline uint32_t div(uint8_t rd, uint8_t rs1, uint8_t rs2)    { return m_instr(funct3::DIV, rd, rs1, rs2); }
    inline uint32_t divu(uint8_t rd, uint8_t rs1, uint8_t rs2)   { return m_instr(funct3::DIVU, rd, rs1, rs2); }
    inline uint32_t rem(uint8_t rd, uint8_t rs1, uint8_t rs2)    { return m_instr(funct3::REM, rd, rs1, rs2); }
    inline uint32_t remu(uint8_t rd, uint8_t rs1, uint8_t rs2)   { return m_instr(funct3::REMU, rd, rs1, rs2); }
}

} // namespace rv32m
} // namespace riscv

#endif // RISCV_RV32M_HPP
