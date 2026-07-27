/*******************************************************************************
 * RISC-V RV32FC Compressed Floating-Point Extension Model (Zfc)
 * 
 * Decoder for 16-bit compressed floating-point instructions.
 * These are part of the "Zfc" extension (subset of C extension for F).
 * 
 * Instructions covered:
 *   - C.FLW (Compressed Floating Load Word) - uses compressed registers
 *   - C.FSW (Compressed Floating Store Word) - uses compressed registers
 *   - C.FLWSP (Compressed Floating Load Word, Stack-Pointer relative)
 *   - C.FSWSP (Compressed Floating Store Word, Stack-Pointer relative)
 * 
 * Note: These instructions are only available in RV32. In RV64, the same
 * encodings are used for C.LD, C.SD, C.LDSP, C.SDSP (double-word integer).
 ******************************************************************************/

#ifndef RISCV_RV32FC_HPP
#define RISCV_RV32FC_HPP

#include "riscv_common.hpp"
#include "riscv_rv32f.hpp"

namespace riscv {
namespace rv32fc {

// ============================================================================
// Instruction Types
// ============================================================================

enum class InstrType {
    C_FLW,      // Compressed Floating Load Word (CL format)
    C_FSW,      // Compressed Floating Store Word (CS format)
    C_FLWSP,    // Compressed Floating Load Word, SP-relative (CI format)
    C_FSWSP,    // Compressed Floating Store Word, SP-relative (CSS format)
    
    ILLEGAL
};

// ============================================================================
// Decoded Instruction
// ============================================================================

struct DecodedInstr {
    InstrType type;
    uint8_t rd;         // Destination float register
    uint8_t rs1;        // Base address register (integer)
    uint8_t rs2;        // Source float register (for stores)
    uint32_t uimm;      // Unsigned immediate offset
    uint16_t raw;
    
    std::string mnemonic() const;
    
    // Get actual register number from compressed 3-bit encoding
    // Compressed regs 0-7 map to x8-x15 (or f8-f15)
    static uint8_t creg_to_reg(uint8_t creg) {
        return (creg & 0x7) + 8;
    }
};

// ============================================================================
// Execution Result
// ============================================================================

struct ExecResult {
    bool valid;
    bool memory_read;
    bool memory_written;
    uint32_t trap_cause;    // Exception cause when !valid
    uint32_t trap_value;    // Value for mtval (faulting address / instruction)
    std::string error;
};

// ============================================================================
// Decoder
// ============================================================================

class Decoder {
public:
    DecodedInstr decode(uint16_t instr) const {
        DecodedInstr d;
        d.raw = instr;
        d.type = InstrType::ILLEGAL;
        d.rd = 0;
        d.rs1 = 0;
        d.rs2 = 0;
        d.uimm = 0;
        
        uint8_t op = instr & 0x3;
        uint8_t funct3 = (instr >> 13) & 0x7;
        
        switch (op) {
            case 0b00:
                decode_quadrant0(d, instr, funct3);
                break;
            case 0b10:
                decode_quadrant2(d, instr, funct3);
                break;
        }
        
        return d;
    }
    
    bool is_compressed_float(uint16_t instr) const {
        uint8_t op = instr & 0x3;
        uint8_t funct3 = (instr >> 13) & 0x7;
        
        if (op == 0b00) {
            return (funct3 == 0b011 || funct3 == 0b111);  // C.FLW, C.FSW
        } else if (op == 0b10) {
            return (funct3 == 0b011 || funct3 == 0b111);  // C.FLWSP, C.FSWSP
        }
        return false;
    }
    
private:
    void decode_quadrant0(DecodedInstr& d, uint16_t instr, uint8_t funct3) const {
        // Quadrant 0: op = 00
        switch (funct3) {
            case 0b011: {
                // C.FLW: 011 | uimm[5:3] | rs1' | uimm[2|6] | rd' | 00
                d.type = InstrType::C_FLW;
                d.rd = DecodedInstr::creg_to_reg((instr >> 2) & 0x7);
                d.rs1 = DecodedInstr::creg_to_reg((instr >> 7) & 0x7);
                // uimm[5:3] in bits[12:10], uimm[2] in bit[6], uimm[6] in bit[5]
                d.uimm = (((instr >> 10) & 0x7) << 3) |  // uimm[5:3]
                         (((instr >> 6) & 0x1) << 2) |   // uimm[2]
                         (((instr >> 5) & 0x1) << 6);    // uimm[6]
                break;
            }
            case 0b111: {
                // C.FSW: 111 | uimm[5:3] | rs1' | uimm[2|6] | rs2' | 00
                d.type = InstrType::C_FSW;
                d.rs2 = DecodedInstr::creg_to_reg((instr >> 2) & 0x7);
                d.rs1 = DecodedInstr::creg_to_reg((instr >> 7) & 0x7);
                d.uimm = (((instr >> 10) & 0x7) << 3) |
                         (((instr >> 6) & 0x1) << 2) |
                         (((instr >> 5) & 0x1) << 6);
                break;
            }
        }
    }
    
    void decode_quadrant2(DecodedInstr& d, uint16_t instr, uint8_t funct3) const {
        // Quadrant 2: op = 10
        switch (funct3) {
            case 0b011: {
                // C.FLWSP: 011 | uimm[5] | rd | uimm[4:2|7:6] | 10
                d.type = InstrType::C_FLWSP;
                d.rd = (instr >> 7) & 0x1F;  // Full 5-bit register
                d.rs1 = 2;  // sp
                // uimm[5] in bit[12], uimm[4:2] in bits[6:4], uimm[7:6] in bits[3:2]
                d.uimm = (((instr >> 12) & 0x1) << 5) |  // uimm[5]
                         (((instr >> 4) & 0x7) << 2) |   // uimm[4:2]
                         (((instr >> 2) & 0x3) << 6);    // uimm[7:6]
                // Note: unlike C.LWSP, rd = 0 is VALID here — the
                // destination is float register f0, which is a normal
                // register (the x0-hardwired-zero rule does not apply).
                break;
            }
            case 0b111: {
                // C.FSWSP: 111 | uimm[5:2|7:6] | rs2 | 10
                d.type = InstrType::C_FSWSP;
                d.rs2 = (instr >> 2) & 0x1F;  // Full 5-bit register
                d.rs1 = 2;  // sp
                // uimm[5:2] in bits[12:9], uimm[7:6] in bits[8:7]
                d.uimm = (((instr >> 9) & 0xF) << 2) |   // uimm[5:2]
                         (((instr >> 7) & 0x3) << 6);    // uimm[7:6]
                break;
            }
        }
    }
};

// ============================================================================
// Executor
// ============================================================================

class Executor {
public:
    // When false, misaligned accesses raise address-misaligned exceptions.
    bool allow_misaligned = false;
    
    ExecResult execute(const DecodedInstr& instr, RegFile& iregs, 
                       rv32f::FRegFile& fregs, Bus& bus) const {
        ExecResult result = {};
        result.valid = true;
        result.memory_read = false;
        result.memory_written = false;
        
        if (instr.type == InstrType::ILLEGAL) {
            result.valid = false;
            result.trap_cause = exception::ILLEGAL_INSTRUCTION;
            result.trap_value = instr.raw;
            result.error = "Illegal compressed F instruction";
            return result;
        }
        
        switch (instr.type) {
            case InstrType::C_FLW:
            case InstrType::C_FLWSP: {
                uint32_t base = (instr.type == InstrType::C_FLW)
                                ? iregs.read(instr.rs1) : iregs.read(2);
                uint32_t addr = base + instr.uimm;
                uint32_t val;
                if (do_load32(bus, addr, val, result)) {
                    fregs.write_bits(instr.rd, val);
                    result.memory_read = true;
                }
                break;
            }
            
            case InstrType::C_FSW:
            case InstrType::C_FSWSP: {
                uint32_t base = (instr.type == InstrType::C_FSW)
                                ? iregs.read(instr.rs1) : iregs.read(2);
                uint32_t addr = base + instr.uimm;
                uint32_t val = fregs.read_bits(instr.rs2);
                if (do_store32(bus, addr, val, result)) {
                    result.memory_written = true;
                }
                break;
            }
            
            default:
                result.valid = false;
                result.trap_cause = exception::ILLEGAL_INSTRUCTION;
                result.trap_value = instr.raw;
                result.error = "Unknown compressed F instruction";
                break;
        }
        
        return result;
    }

private:
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
};

// ============================================================================
// Mnemonic Generation
// ============================================================================

inline std::string DecodedInstr::mnemonic() const {
    auto fd_s = "f" + std::to_string(rd);
    auto fs2_s = "f" + std::to_string(rs2);
    auto rs1_s = reg_abi_name(rs1);
    
    switch (type) {
        case InstrType::C_FLW:
            return "c.flw " + fd_s + ", " + std::to_string(uimm) + "(" + rs1_s + ")";
        case InstrType::C_FSW:
            return "c.fsw " + fs2_s + ", " + std::to_string(uimm) + "(" + rs1_s + ")";
        case InstrType::C_FLWSP:
            return "c.flwsp " + fd_s + ", " + std::to_string(uimm) + "(sp)";
        case InstrType::C_FSWSP:
            return "c.fswsp " + fs2_s + ", " + std::to_string(uimm) + "(sp)";
        case InstrType::ILLEGAL:
            return "ILLEGAL";
    }
    return "UNKNOWN";
}

// ============================================================================
// Instruction Encoding Helpers
// ============================================================================

namespace encode {
    // C.FLW: 011 | uimm[5:3] | rs1' | uimm[2|6] | rd' | 00
    // uimm is word-aligned (scaled by 4), range 0-124
    inline uint16_t c_flw(uint8_t rd, uint8_t rs1, uint32_t uimm) {
        // rd and rs1 must be in range 8-15 (compressed registers)
        uint8_t rd_c = (rd - 8) & 0x7;
        uint8_t rs1_c = (rs1 - 8) & 0x7;
        
        return 0b00 |                              // op
               (rd_c << 2) |                       // rd'
               (((uimm >> 6) & 0x1) << 5) |       // uimm[6]
               (((uimm >> 2) & 0x1) << 6) |       // uimm[2]
               (rs1_c << 7) |                      // rs1'
               (((uimm >> 3) & 0x7) << 10) |      // uimm[5:3]
               (0b011 << 13);                      // funct3
    }
    
    // C.FSW: 111 | uimm[5:3] | rs1' | uimm[2|6] | rs2' | 00
    inline uint16_t c_fsw(uint8_t rs2, uint8_t rs1, uint32_t uimm) {
        uint8_t rs2_c = (rs2 - 8) & 0x7;
        uint8_t rs1_c = (rs1 - 8) & 0x7;
        
        return 0b00 |
               (rs2_c << 2) |
               (((uimm >> 6) & 0x1) << 5) |
               (((uimm >> 2) & 0x1) << 6) |
               (rs1_c << 7) |
               (((uimm >> 3) & 0x7) << 10) |
               (0b111 << 13);
    }
    
    // C.FLWSP: 011 | uimm[5] | rd | uimm[4:2|7:6] | 10
    // uimm is word-aligned, range 0-252
    inline uint16_t c_flwsp(uint8_t rd, uint32_t uimm) {
        return 0b10 |                              // op
               (((uimm >> 6) & 0x3) << 2) |       // uimm[7:6]
               (((uimm >> 2) & 0x7) << 4) |       // uimm[4:2]
               ((rd & 0x1F) << 7) |               // rd (full 5-bit)
               (((uimm >> 5) & 0x1) << 12) |      // uimm[5]
               (0b011 << 13);                      // funct3
    }
    
    // C.FSWSP: 111 | uimm[5:2|7:6] | rs2 | 10
    inline uint16_t c_fswsp(uint8_t rs2, uint32_t uimm) {
        return 0b10 |
               ((rs2 & 0x1F) << 2) |              // rs2 (full 5-bit)
               (((uimm >> 6) & 0x3) << 7) |       // uimm[7:6]
               (((uimm >> 2) & 0xF) << 9) |       // uimm[5:2]
               (0b111 << 13);
    }
}

} // namespace rv32fc
} // namespace riscv

#endif // RISCV_RV32FC_HPP
