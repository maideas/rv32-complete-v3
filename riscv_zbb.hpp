/*******************************************************************************
 * RISC-V Zbb Extension Model
 * 
 * Basic Bit Manipulation extension (part of Bit Manipulation, Ratified 2021).
 * 
 * Instructions included:
 * 
 * Logical with Negate:
 *   - ANDN rd, rs1, rs2: rd = rs1 & ~rs2
 *   - ORN  rd, rs1, rs2: rd = rs1 | ~rs2
 *   - XNOR rd, rs1, rs2: rd = ~(rs1 ^ rs2)
 * 
 * Count Leading/Trailing Zeros, Population Count:
 *   - CLZ   rd, rs: rd = count_leading_zeros(rs)
 *   - CTZ   rd, rs: rd = count_trailing_zeros(rs)
 *   - CPOP  rd, rs: rd = population_count(rs)  (number of 1 bits)
 * 
 * Integer Min/Max:
 *   - MAX   rd, rs1, rs2: rd = max(rs1, rs2) signed
 *   - MAXU  rd, rs1, rs2: rd = max(rs1, rs2) unsigned
 *   - MIN   rd, rs1, rs2: rd = min(rs1, rs2) signed
 *   - MINU  rd, rs1, rs2: rd = min(rs1, rs2) unsigned
 * 
 * Sign/Zero Extension:
 *   - SEXT.B rd, rs: rd = sign_extend(rs[7:0])
 *   - SEXT.H rd, rs: rd = sign_extend(rs[15:0])
 *   - ZEXT.H rd, rs: rd = zero_extend(rs[15:0])  (RV32 only, pseudo for PACK)
 * 
 * Bitwise Rotation:
 *   - ROL rd, rs1, rs2: rd = rotate_left(rs1, rs2[4:0])
 *   - ROR rd, rs1, rs2: rd = rotate_right(rs1, rs2[4:0])
 *   - RORI rd, rs, shamt: rd = rotate_right(rs, shamt)
 * 
 * OR Combine / Byte Reverse:
 *   - ORC.B rd, rs: OR-combine bytes (set byte to 0xFF if any bit set)
 *   - REV8 rd, rs: Byte-reverse (endian swap)
 ******************************************************************************/

#ifndef RISCV_ZBB_HPP
#define RISCV_ZBB_HPP

#include "riscv_common.hpp"

namespace riscv {
namespace zbb {

// ============================================================================
// Instruction Types
// ============================================================================

enum class InstrType {
    // Logical with negate
    ANDN,
    ORN,
    XNOR,
    
    // Count bits
    CLZ,
    CTZ,
    CPOP,
    
    // Min/Max
    MAX,
    MAXU,
    MIN,
    MINU,
    
    // Sign/Zero extension
    SEXT_B,
    SEXT_H,
    ZEXT_H,
    
    // Rotation
    ROL,
    ROR,
    RORI,
    
    // Byte operations
    ORC_B,
    REV8,
    
    ILLEGAL
};

// ============================================================================
// Decoded Instruction
// ============================================================================

struct DecodedInstr {
    InstrType type;
    uint8_t rd;
    uint8_t rs1;
    uint8_t rs2;        // Also used for shamt in RORI
    uint8_t shamt;      // Shift amount for RORI
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
    constexpr uint8_t OP     = 0b0110011;  // R-type
    constexpr uint8_t OP_IMM = 0b0010011;  // I-type
}

namespace funct3 {
    constexpr uint8_t ANDN_ORN = 0b111;    // ANDN (funct7=0x20), ORN (funct7=0x20)
    constexpr uint8_t XNOR     = 0b100;    // XNOR (funct7=0x20)
    constexpr uint8_t CLZ_CTZ_CPOP = 0b001;
    constexpr uint8_t MAX_MIN  = 0b110;    // MAX (funct7=0x05), MIN (funct7=0x05)
    constexpr uint8_t MAXU_MINU = 0b111;   // MAXU (funct7=0x05), MINU (funct7=0x05)
    constexpr uint8_t ROL_ROR  = 0b001;    // ROL (funct7=0x30), ROR (funct7=0x30)
    constexpr uint8_t RORI     = 0b101;
    constexpr uint8_t ORC_B_REV8 = 0b101;
}

namespace funct7 {
    constexpr uint8_t ANDN     = 0b0100000;
    constexpr uint8_t ORN      = 0b0100000;
    constexpr uint8_t XNOR     = 0b0100000;
    constexpr uint8_t MINMAX   = 0b0000101;
    constexpr uint8_t ROL      = 0b0110000;
    constexpr uint8_t ROR      = 0b0110000;
    constexpr uint8_t RORI     = 0b0110000;
    constexpr uint8_t ORC_B    = 0b0010100;
    constexpr uint8_t REV8     = 0b0110100;
}

// rs2/shamt values for unary operations
namespace rs2_val {
    constexpr uint8_t CLZ  = 0b00000;
    constexpr uint8_t CTZ  = 0b00001;
    constexpr uint8_t CPOP = 0b00010;
    constexpr uint8_t SEXT_B = 0b00100;
    constexpr uint8_t SEXT_H = 0b00101;
    constexpr uint8_t ORC_B  = 0b00111;
    constexpr uint8_t REV8   = 0b11000;
    constexpr uint8_t ZEXT_H = 0b00000;  // With different funct7
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
        uint16_t funct12 = bits(instr, 31, 20);
        
        if (op == opcode::OP) {
            decode_r_type(d, funct3, funct7);
        } else if (op == opcode::OP_IMM) {
            decode_i_type(d, funct3, funct7, funct12);
        }
        
        return d;
    }
    
    bool is_zbb_instruction(uint32_t instr) const {
        DecodedInstr d = decode(instr);
        return d.type != InstrType::ILLEGAL;
    }
    
private:
    void decode_r_type(DecodedInstr& d, uint8_t funct3, uint8_t funct7) const {
        // ANDN, ORN, XNOR
        if (funct7 == funct7::ANDN) {
            if (funct3 == 0b111) d.type = InstrType::ANDN;
            else if (funct3 == 0b110) d.type = InstrType::ORN;
            else if (funct3 == 0b100) d.type = InstrType::XNOR;
        }
        // MIN, MAX, MINU, MAXU
        else if (funct7 == funct7::MINMAX) {
            switch (funct3) {
                case 0b100: d.type = InstrType::MIN; break;
                case 0b101: d.type = InstrType::MINU; break;
                case 0b110: d.type = InstrType::MAX; break;
                case 0b111: d.type = InstrType::MAXU; break;
            }
        }
        // ROL, ROR
        else if (funct7 == funct7::ROL) {
            if (funct3 == 0b001) d.type = InstrType::ROL;
            else if (funct3 == 0b101) d.type = InstrType::ROR;
        }
        // ZEXT.H (RV32 encoding: pack with rs2=x0)
        else if (funct7 == 0b0000100 && funct3 == 0b100 && d.rs2 == 0) {
            d.type = InstrType::ZEXT_H;
        }
    }
    
    void decode_i_type(DecodedInstr& d, uint8_t funct3, uint8_t funct7, uint16_t funct12) const {
        // CLZ, CTZ, CPOP, SEXT.B, SEXT.H
        if (funct3 == 0b001 && funct7 == 0b0110000) {
            switch (d.rs2) {
                case rs2_val::CLZ:    d.type = InstrType::CLZ; break;
                case rs2_val::CTZ:    d.type = InstrType::CTZ; break;
                case rs2_val::CPOP:   d.type = InstrType::CPOP; break;
                case rs2_val::SEXT_B: d.type = InstrType::SEXT_B; break;
                case rs2_val::SEXT_H: d.type = InstrType::SEXT_H; break;
            }
        }
        // RORI
        else if (funct3 == 0b101 && funct7 == funct7::RORI) {
            d.type = InstrType::RORI;
        }
        // ORC.B
        else if (funct3 == 0b101 && funct12 == 0b001010000111) {
            d.type = InstrType::ORC_B;
        }
        // REV8
        else if (funct3 == 0b101 && funct12 == 0b011010011000) {
            d.type = InstrType::REV8;
        }
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
            result.error = "Illegal Zbb instruction";
            return result;
        }
        
        uint32_t rs1_val = regs.read(instr.rs1);
        uint32_t rs2_val = regs.read(instr.rs2);
        
        switch (instr.type) {
            // Logical with negate
            case InstrType::ANDN:
                result.result = rs1_val & ~rs2_val;
                break;
            case InstrType::ORN:
                result.result = rs1_val | ~rs2_val;
                break;
            case InstrType::XNOR:
                result.result = ~(rs1_val ^ rs2_val);
                break;
            
            // Count bits
            case InstrType::CLZ:
                result.result = clz32(rs1_val);
                break;
            case InstrType::CTZ:
                result.result = ctz32(rs1_val);
                break;
            case InstrType::CPOP:
                result.result = popcount32(rs1_val);
                break;
            
            // Min/Max signed
            case InstrType::MIN:
                result.result = static_cast<uint32_t>(
                    std::min(static_cast<int32_t>(rs1_val), 
                             static_cast<int32_t>(rs2_val)));
                break;
            case InstrType::MAX:
                result.result = static_cast<uint32_t>(
                    std::max(static_cast<int32_t>(rs1_val),
                             static_cast<int32_t>(rs2_val)));
                break;
            
            // Min/Max unsigned
            case InstrType::MINU:
                result.result = std::min(rs1_val, rs2_val);
                break;
            case InstrType::MAXU:
                result.result = std::max(rs1_val, rs2_val);
                break;
            
            // Sign/Zero extension
            case InstrType::SEXT_B:
                result.result = sign_extend(rs1_val & 0xFF, 7);  // Sign bit at position 7
                break;
            case InstrType::SEXT_H:
                result.result = sign_extend(rs1_val & 0xFFFF, 15);  // Sign bit at position 15
                break;
            case InstrType::ZEXT_H:
                result.result = rs1_val & 0xFFFF;
                break;
            
            // Rotation (shamt = 0 handled separately to avoid the undefined
            // behavior of shifting a 32-bit value by 32)
            case InstrType::ROL: {
                uint32_t shamt = rs2_val & 0x1F;
                result.result = (shamt == 0) ? rs1_val
                    : ((rs1_val << shamt) | (rs1_val >> (32 - shamt)));
                break;
            }
            case InstrType::ROR: {
                uint32_t shamt = rs2_val & 0x1F;
                result.result = (shamt == 0) ? rs1_val
                    : ((rs1_val >> shamt) | (rs1_val << (32 - shamt)));
                break;
            }
            case InstrType::RORI: {
                uint32_t shamt = instr.shamt & 0x1F;
                result.result = (shamt == 0) ? rs1_val
                    : ((rs1_val >> shamt) | (rs1_val << (32 - shamt)));
                break;
            }
            
            // Byte operations
            case InstrType::ORC_B:
                result.result = orc_b(rs1_val);
                break;
            case InstrType::REV8:
                result.result = rev8(rs1_val);
                break;
            
            default:
                result.valid = false;
                result.error = "Unknown Zbb instruction";
                return result;
        }
        
        regs.write(instr.rd, result.result);
        return result;
    }
    
private:
    // Count leading zeros
    static uint32_t clz32(uint32_t x) {
        if (x == 0) return 32;
        uint32_t n = 0;
        if ((x & 0xFFFF0000) == 0) { n += 16; x <<= 16; }
        if ((x & 0xFF000000) == 0) { n += 8;  x <<= 8;  }
        if ((x & 0xF0000000) == 0) { n += 4;  x <<= 4;  }
        if ((x & 0xC0000000) == 0) { n += 2;  x <<= 2;  }
        if ((x & 0x80000000) == 0) { n += 1; }
        return n;
    }
    
    // Count trailing zeros
    static uint32_t ctz32(uint32_t x) {
        if (x == 0) return 32;
        uint32_t n = 0;
        if ((x & 0x0000FFFF) == 0) { n += 16; x >>= 16; }
        if ((x & 0x000000FF) == 0) { n += 8;  x >>= 8;  }
        if ((x & 0x0000000F) == 0) { n += 4;  x >>= 4;  }
        if ((x & 0x00000003) == 0) { n += 2;  x >>= 2;  }
        if ((x & 0x00000001) == 0) { n += 1; }
        return n;
    }
    
    // Population count (number of 1 bits)
    static uint32_t popcount32(uint32_t x) {
        x = x - ((x >> 1) & 0x55555555);
        x = (x & 0x33333333) + ((x >> 2) & 0x33333333);
        x = (x + (x >> 4)) & 0x0F0F0F0F;
        x = x + (x >> 8);
        x = x + (x >> 16);
        return x & 0x3F;
    }
    
    // OR-combine bytes (set byte to 0xFF if any bit set)
    static uint32_t orc_b(uint32_t x) {
        uint32_t result = 0;
        if (x & 0x000000FF) result |= 0x000000FF;
        if (x & 0x0000FF00) result |= 0x0000FF00;
        if (x & 0x00FF0000) result |= 0x00FF0000;
        if (x & 0xFF000000) result |= 0xFF000000;
        return result;
    }
    
    // Byte-reverse (endian swap)
    static uint32_t rev8(uint32_t x) {
        return ((x & 0xFF000000) >> 24) |
               ((x & 0x00FF0000) >> 8)  |
               ((x & 0x0000FF00) << 8)  |
               ((x & 0x000000FF) << 24);
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
        case InstrType::ANDN:
            return "andn " + rd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::ORN:
            return "orn " + rd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::XNOR:
            return "xnor " + rd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::CLZ:
            return "clz " + rd_s + ", " + rs1_s;
        case InstrType::CTZ:
            return "ctz " + rd_s + ", " + rs1_s;
        case InstrType::CPOP:
            return "cpop " + rd_s + ", " + rs1_s;
        case InstrType::MIN:
            return "min " + rd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::MINU:
            return "minu " + rd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::MAX:
            return "max " + rd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::MAXU:
            return "maxu " + rd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::SEXT_B:
            return "sext.b " + rd_s + ", " + rs1_s;
        case InstrType::SEXT_H:
            return "sext.h " + rd_s + ", " + rs1_s;
        case InstrType::ZEXT_H:
            return "zext.h " + rd_s + ", " + rs1_s;
        case InstrType::ROL:
            return "rol " + rd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::ROR:
            return "ror " + rd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::RORI:
            return "rori " + rd_s + ", " + rs1_s + ", " + std::to_string(shamt);
        case InstrType::ORC_B:
            return "orc.b " + rd_s + ", " + rs1_s;
        case InstrType::REV8:
            return "rev8 " + rd_s + ", " + rs1_s;
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
    
    inline uint32_t i_type(uint16_t imm12, uint8_t rs1, uint8_t funct3, 
                           uint8_t rd, uint8_t op) {
        return (static_cast<uint32_t>(imm12 & 0xFFF) << 20) |
               (static_cast<uint32_t>(rs1 & 0x1F) << 15) |
               (static_cast<uint32_t>(funct3) << 12) |
               (static_cast<uint32_t>(rd & 0x1F) << 7) |
               op;
    }
    
    // Logical with negate
    inline uint32_t andn(uint8_t rd, uint8_t rs1, uint8_t rs2) {
        return r_type(funct7::ANDN, rs2, rs1, 0b111, rd, opcode::OP);
    }
    inline uint32_t orn(uint8_t rd, uint8_t rs1, uint8_t rs2) {
        return r_type(funct7::ORN, rs2, rs1, 0b110, rd, opcode::OP);
    }
    inline uint32_t xnor(uint8_t rd, uint8_t rs1, uint8_t rs2) {
        return r_type(funct7::XNOR, rs2, rs1, 0b100, rd, opcode::OP);
    }
    
    // Count bits
    inline uint32_t clz(uint8_t rd, uint8_t rs1) {
        return i_type(0b011000000000, rs1, 0b001, rd, opcode::OP_IMM);
    }
    inline uint32_t ctz(uint8_t rd, uint8_t rs1) {
        return i_type(0b011000000001, rs1, 0b001, rd, opcode::OP_IMM);
    }
    inline uint32_t cpop(uint8_t rd, uint8_t rs1) {
        return i_type(0b011000000010, rs1, 0b001, rd, opcode::OP_IMM);
    }
    
    // Min/Max
    inline uint32_t min(uint8_t rd, uint8_t rs1, uint8_t rs2) {
        return r_type(funct7::MINMAX, rs2, rs1, 0b100, rd, opcode::OP);
    }
    inline uint32_t minu(uint8_t rd, uint8_t rs1, uint8_t rs2) {
        return r_type(funct7::MINMAX, rs2, rs1, 0b101, rd, opcode::OP);
    }
    inline uint32_t max(uint8_t rd, uint8_t rs1, uint8_t rs2) {
        return r_type(funct7::MINMAX, rs2, rs1, 0b110, rd, opcode::OP);
    }
    inline uint32_t maxu(uint8_t rd, uint8_t rs1, uint8_t rs2) {
        return r_type(funct7::MINMAX, rs2, rs1, 0b111, rd, opcode::OP);
    }
    
    // Sign/Zero extension
    inline uint32_t sext_b(uint8_t rd, uint8_t rs1) {
        return i_type(0b011000000100, rs1, 0b001, rd, opcode::OP_IMM);
    }
    inline uint32_t sext_h(uint8_t rd, uint8_t rs1) {
        return i_type(0b011000000101, rs1, 0b001, rd, opcode::OP_IMM);
    }
    inline uint32_t zext_h(uint8_t rd, uint8_t rs1) {
        return r_type(0b0000100, 0, rs1, 0b100, rd, opcode::OP);
    }
    
    // Rotation
    inline uint32_t rol(uint8_t rd, uint8_t rs1, uint8_t rs2) {
        return r_type(funct7::ROL, rs2, rs1, 0b001, rd, opcode::OP);
    }
    inline uint32_t ror(uint8_t rd, uint8_t rs1, uint8_t rs2) {
        return r_type(funct7::ROR, rs2, rs1, 0b101, rd, opcode::OP);
    }
    inline uint32_t rori(uint8_t rd, uint8_t rs1, uint8_t shamt) {
        return i_type((0b0110000 << 5) | (shamt & 0x1F), rs1, 0b101, rd, opcode::OP_IMM);
    }
    
    // Byte operations
    inline uint32_t orc_b(uint8_t rd, uint8_t rs1) {
        return i_type(0b001010000111, rs1, 0b101, rd, opcode::OP_IMM);
    }
    inline uint32_t rev8(uint8_t rd, uint8_t rs1) {
        return i_type(0b011010011000, rs1, 0b101, rd, opcode::OP_IMM);
    }
}

} // namespace zbb
} // namespace riscv

#endif // RISCV_ZBB_HPP
