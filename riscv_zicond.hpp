/*******************************************************************************
 * RISC-V Zicond Extension Model
 * 
 * Integer Conditional Operations extension (Ratified 2023).
 * 
 * This extension adds two instructions for branchless conditional operations:
 *   - CZERO.EQZ rd, rs1, rs2: rd = (rs2 == 0) ? 0 : rs1
 *   - CZERO.NEZ rd, rs1, rs2: rd = (rs2 != 0) ? 0 : rs1
 * 
 * Use cases:
 *   - Branchless conditional moves (combined with other instructions)
 *   - Constant-time cryptographic code (avoiding timing side channels)
 *   - Avoiding branch misprediction penalties
 *   - Conditional select: (cond ? a : b) = CZERO.EQZ(a,cond) | CZERO.NEZ(b,cond)
 * 
 * Examples:
 *   Conditional move (rd = cond ? rs1 : rs2):
 *     CZERO.EQZ t0, rs1, cond   # t0 = (cond == 0) ? 0 : rs1
 *     CZERO.NEZ t1, rs2, cond   # t1 = (cond != 0) ? 0 : rs2
 *     OR rd, t0, t1             # rd = t0 | t1
 * 
 *   Conditional negate (rd = cond ? -rs1 : rs1):
 *     NEG t0, rs1               # t0 = -rs1
 *     CZERO.EQZ t0, t0, cond    # t0 = (cond == 0) ? 0 : -rs1
 *     CZERO.NEZ t1, rs1, cond   # t1 = (cond != 0) ? 0 : rs1
 *     OR rd, t0, t1
 ******************************************************************************/

#ifndef RISCV_ZICOND_HPP
#define RISCV_ZICOND_HPP

#include "riscv_common.hpp"

namespace riscv {
namespace zicond {

// ============================================================================
// Instruction Types
// ============================================================================

enum class InstrType {
    CZERO_EQZ,  // rd = (rs2 == 0) ? 0 : rs1
    CZERO_NEZ,  // rd = (rs2 != 0) ? 0 : rs1
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
    constexpr uint8_t OP = 0b0110011;  // R-type integer operations
}

// Ratified Zicond encoding: both instructions share funct7 = 0000111 and
// are distinguished by funct3 (czero.eqz = 101, czero.nez = 111).
namespace funct3 {
    constexpr uint8_t CZERO_EQZ = 0b101;
    constexpr uint8_t CZERO_NEZ = 0b111;
}

namespace funct7 {
    constexpr uint8_t CZERO = 0b0000111;
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
        
        if (op == opcode::OP && funct7 == funct7::CZERO) {
            switch (funct3) {
                case funct3::CZERO_EQZ:
                    d.type = InstrType::CZERO_EQZ;
                    break;
                case funct3::CZERO_NEZ:
                    d.type = InstrType::CZERO_NEZ;
                    break;
            }
        }
        
        return d;
    }
    
    bool is_zicond_instruction(uint32_t instr) const {
        uint8_t op = bits(instr, 6, 0);
        uint8_t funct3 = bits(instr, 14, 12);
        uint8_t funct7 = bits(instr, 31, 25);
        
        if (op != opcode::OP || funct7 != funct7::CZERO) return false;
        return (funct3 == funct3::CZERO_EQZ || funct3 == funct3::CZERO_NEZ);
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
            result.error = "Illegal Zicond instruction";
            return result;
        }
        
        uint32_t rs1_val = regs.read(instr.rs1);
        uint32_t rs2_val = regs.read(instr.rs2);
        
        switch (instr.type) {
            case InstrType::CZERO_EQZ:
                // rd = (rs2 == 0) ? 0 : rs1
                result.result = (rs2_val == 0) ? 0 : rs1_val;
                break;
                
            case InstrType::CZERO_NEZ:
                // rd = (rs2 != 0) ? 0 : rs1
                result.result = (rs2_val != 0) ? 0 : rs1_val;
                break;
                
            default:
                result.valid = false;
                result.error = "Unknown Zicond instruction";
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
        case InstrType::CZERO_EQZ:
            return "czero.eqz " + rd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::CZERO_NEZ:
            return "czero.nez " + rd_s + ", " + rs1_s + ", " + rs2_s;
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
    
    // CZERO.EQZ rd, rs1, rs2
    inline uint32_t czero_eqz(uint8_t rd, uint8_t rs1, uint8_t rs2) {
        return r_type(funct7::CZERO, rs2, rs1, funct3::CZERO_EQZ, rd, opcode::OP);
    }
    
    // CZERO.NEZ rd, rs1, rs2
    inline uint32_t czero_nez(uint8_t rd, uint8_t rs1, uint8_t rs2) {
        return r_type(funct7::CZERO, rs2, rs1, funct3::CZERO_NEZ, rd, opcode::OP);
    }
}

} // namespace zicond
} // namespace riscv

#endif // RISCV_ZICOND_HPP
