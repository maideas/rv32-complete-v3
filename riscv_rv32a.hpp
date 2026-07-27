/*******************************************************************************
 * RISC-V RV32A Atomic Extension Model
 * 
 * Decoder and executor for all RV32A atomic memory operation instructions.
 * Uses injected Bus interface for memory access.
 * 
 * Instructions covered:
 *   - LR.W (Load-Reserved Word)
 *   - SC.W (Store-Conditional Word)
 *   - AMOSWAP.W (Atomic Swap)
 *   - AMOADD.W (Atomic Add)
 *   - AMOXOR.W (Atomic XOR)
 *   - AMOAND.W (Atomic AND)
 *   - AMOOR.W (Atomic OR)
 *   - AMOMIN.W (Atomic Minimum, signed)
 *   - AMOMAX.W (Atomic Maximum, signed)
 *   - AMOMINU.W (Atomic Minimum, unsigned)
 *   - AMOMAXU.W (Atomic Maximum, unsigned)
 * 
 * Memory ordering bits (aq/rl):
 *   - aq (acquire): Orders this operation before subsequent memory ops
 *   - rl (release): Orders this operation after prior memory ops
 ******************************************************************************/

#ifndef RISCV_RV32A_HPP
#define RISCV_RV32A_HPP

#include "riscv_common.hpp"
#include <unordered_map>

namespace riscv {
namespace rv32a {

// ============================================================================
// Instruction Types
// ============================================================================

enum class InstrType {
    LR_W,       // Load-Reserved Word
    SC_W,       // Store-Conditional Word
    AMOSWAP_W,  // Atomic Swap Word
    AMOADD_W,   // Atomic Add Word
    AMOXOR_W,   // Atomic XOR Word
    AMOAND_W,   // Atomic AND Word
    AMOOR_W,    // Atomic OR Word
    AMOMIN_W,   // Atomic Min Word (signed)
    AMOMAX_W,   // Atomic Max Word (signed)
    AMOMINU_W,  // Atomic Min Word (unsigned)
    AMOMAXU_W,  // Atomic Max Word (unsigned)
    
    ILLEGAL
};

// ============================================================================
// Decoded Instruction
// ============================================================================

struct DecodedInstr {
    InstrType type;
    uint8_t rd;         // Destination register
    uint8_t rs1;        // Address register
    uint8_t rs2;        // Source register (for SC and AMO ops)
    bool aq;            // Acquire ordering
    bool rl;            // Release ordering
    uint8_t funct5;     // Upper 5 bits of funct7
    uint8_t funct3;     // Should be 010 for .W operations
    uint32_t raw;       // Raw instruction
    
    std::string mnemonic() const;
    
    // Memory ordering suffix
    std::string ordering_suffix() const {
        if (aq && rl) return ".aqrl";
        if (aq) return ".aq";
        if (rl) return ".rl";
        return "";
    }
};

// ============================================================================
// Execution Result
// ============================================================================

struct ExecResult {
    bool valid;             // Instruction executed successfully
    uint32_t loaded_value;  // Value read from memory
    uint32_t stored_value;  // Value written to memory (if any)
    bool sc_success;        // For SC.W: true if store succeeded
    bool memory_read;       // Memory was read
    bool memory_written;    // Memory was written
    uint32_t trap_cause;    // Exception cause when !valid
    uint32_t trap_value;    // Value for mtval (faulting address / instruction)
    std::string error;      // Error message if not valid
};

// ============================================================================
// Reservation Set (for LR/SC)
// ============================================================================

/**
 * Tracks load reservations for LR/SC pairs.
 * In a real implementation, this would be per-hart and track
 * address ranges. This simplified version tracks a single reservation.
 */
class ReservationSet {
    bool valid = false;
    uint32_t reserved_addr = 0;
    
public:
    void reserve(uint32_t addr) {
        valid = true;
        reserved_addr = addr & ~0x3u;  // Word-aligned
    }
    
    void clear() {
        valid = false;
    }
    
    bool check(uint32_t addr) const {
        return valid && ((addr & ~0x3u) == reserved_addr);
    }
    
    bool has_reservation() const { return valid; }
    uint32_t get_address() const { return reserved_addr; }
};

// ============================================================================
// Opcodes and Function Codes
// ============================================================================

namespace opcode {
    constexpr uint8_t AMO = 0b0101111;  // Atomic Memory Operation
}

namespace funct3 {
    constexpr uint8_t W = 0b010;  // Word (32-bit)
}

namespace funct5 {
    constexpr uint8_t LR      = 0b00010;
    constexpr uint8_t SC      = 0b00011;
    constexpr uint8_t AMOSWAP = 0b00001;
    constexpr uint8_t AMOADD  = 0b00000;
    constexpr uint8_t AMOXOR  = 0b00100;
    constexpr uint8_t AMOAND  = 0b01100;
    constexpr uint8_t AMOOR   = 0b01000;
    constexpr uint8_t AMOMIN  = 0b10000;
    constexpr uint8_t AMOMAX  = 0b10100;
    constexpr uint8_t AMOMINU = 0b11000;
    constexpr uint8_t AMOMAXU = 0b11100;
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
        d.funct3 = bits(instr, 14, 12);
        d.funct5 = bits(instr, 31, 27);
        d.rl = (instr >> 25) & 1;
        d.aq = (instr >> 26) & 1;
        
        uint8_t op = bits(instr, 6, 0);
        
        // Must be AMO opcode with word width
        if (op != opcode::AMO || d.funct3 != funct3::W) {
            return d;
        }
        
        switch (d.funct5) {
            case funct5::LR:      d.type = InstrType::LR_W;      break;
            case funct5::SC:      d.type = InstrType::SC_W;      break;
            case funct5::AMOSWAP: d.type = InstrType::AMOSWAP_W; break;
            case funct5::AMOADD:  d.type = InstrType::AMOADD_W;  break;
            case funct5::AMOXOR:  d.type = InstrType::AMOXOR_W;  break;
            case funct5::AMOAND:  d.type = InstrType::AMOAND_W;  break;
            case funct5::AMOOR:   d.type = InstrType::AMOOR_W;   break;
            case funct5::AMOMIN:  d.type = InstrType::AMOMIN_W;  break;
            case funct5::AMOMAX:  d.type = InstrType::AMOMAX_W;  break;
            case funct5::AMOMINU: d.type = InstrType::AMOMINU_W; break;
            case funct5::AMOMAXU: d.type = InstrType::AMOMAXU_W; break;
            default: break;
        }
        
        // LR.W requires rs2 = 0
        if (d.type == InstrType::LR_W && d.rs2 != 0) {
            d.type = InstrType::ILLEGAL;
        }
        
        return d;
    }
    
    bool is_atomic_instruction(uint32_t instr) const {
        uint8_t op = bits(instr, 6, 0);
        uint8_t f3 = bits(instr, 14, 12);
        return (op == opcode::AMO) && (f3 == funct3::W);
    }
};

// ============================================================================
// Executor
// ============================================================================

class Executor {
public:
    ExecResult execute(const DecodedInstr& instr, RegFile& regs, Bus& bus, 
                       ReservationSet& reservation) const {
        ExecResult result = {};
        result.valid = true;
        result.sc_success = false;
        result.memory_read = false;
        result.memory_written = false;
        
        if (instr.type == InstrType::ILLEGAL) {
            result.valid = false;
            result.trap_cause = exception::ILLEGAL_INSTRUCTION;
            result.trap_value = instr.raw;
            result.error = "Illegal A instruction";
            return result;
        }
        
        uint32_t addr = regs.read(instr.rs1);
        
        // Atomics must be naturally aligned; misaligned atomics raise
        // address-misaligned exceptions (never emulated):
        // LR.W -> load-address-misaligned (4), SC.W/AMO -> store/AMO-
        // address-misaligned (6).
        if (addr & 0x3) {
            result.valid = false;
            result.trap_cause = (instr.type == InstrType::LR_W)
                                ? exception::LOAD_ADDR_MISALIGNED
                                : exception::STORE_ADDR_MISALIGNED;
            result.trap_value = addr;
            result.error = "Misaligned atomic access";
            return result;
        }
        
        uint32_t rs2_val = regs.read(instr.rs2);
        
        try {
        switch (instr.type) {
            case InstrType::LR_W: {
                // Load-Reserved: Load word and set reservation
                uint32_t value = bus.read32(addr);
                regs.write(instr.rd, value);
                reservation.reserve(addr);
                result.loaded_value = value;
                result.memory_read = true;
                break;
            }
            
            case InstrType::SC_W: {
                // Store-Conditional: Store if reservation valid
                if (reservation.check(addr)) {
                    bus.write32(addr, rs2_val);
                    regs.write(instr.rd, 0);  // Success
                    result.sc_success = true;
                    result.stored_value = rs2_val;
                    result.memory_written = true;
                } else {
                    regs.write(instr.rd, 1);  // Failure (non-zero)
                    result.sc_success = false;
                }
                reservation.clear();
                break;
            }
            
            case InstrType::AMOSWAP_W: {
                // Atomic swap: rd = mem[rs1]; mem[rs1] = rs2
                uint32_t old_val = bus.read32(addr);
                bus.write32(addr, rs2_val);
                regs.write(instr.rd, old_val);
                result.loaded_value = old_val;
                result.stored_value = rs2_val;
                result.memory_read = true;
                result.memory_written = true;
                break;
            }
            
            case InstrType::AMOADD_W: {
                // Atomic add: rd = mem[rs1]; mem[rs1] = rd + rs2
                uint32_t old_val = bus.read32(addr);
                uint32_t new_val = old_val + rs2_val;
                bus.write32(addr, new_val);
                regs.write(instr.rd, old_val);
                result.loaded_value = old_val;
                result.stored_value = new_val;
                result.memory_read = true;
                result.memory_written = true;
                break;
            }
            
            case InstrType::AMOXOR_W: {
                uint32_t old_val = bus.read32(addr);
                uint32_t new_val = old_val ^ rs2_val;
                bus.write32(addr, new_val);
                regs.write(instr.rd, old_val);
                result.loaded_value = old_val;
                result.stored_value = new_val;
                result.memory_read = true;
                result.memory_written = true;
                break;
            }
            
            case InstrType::AMOAND_W: {
                uint32_t old_val = bus.read32(addr);
                uint32_t new_val = old_val & rs2_val;
                bus.write32(addr, new_val);
                regs.write(instr.rd, old_val);
                result.loaded_value = old_val;
                result.stored_value = new_val;
                result.memory_read = true;
                result.memory_written = true;
                break;
            }
            
            case InstrType::AMOOR_W: {
                uint32_t old_val = bus.read32(addr);
                uint32_t new_val = old_val | rs2_val;
                bus.write32(addr, new_val);
                regs.write(instr.rd, old_val);
                result.loaded_value = old_val;
                result.stored_value = new_val;
                result.memory_read = true;
                result.memory_written = true;
                break;
            }
            
            case InstrType::AMOMIN_W: {
                // Signed minimum
                uint32_t old_val = bus.read32(addr);
                int32_t old_signed = static_cast<int32_t>(old_val);
                int32_t rs2_signed = static_cast<int32_t>(rs2_val);
                uint32_t new_val = (old_signed < rs2_signed) ? old_val : rs2_val;
                bus.write32(addr, new_val);
                regs.write(instr.rd, old_val);
                result.loaded_value = old_val;
                result.stored_value = new_val;
                result.memory_read = true;
                result.memory_written = true;
                break;
            }
            
            case InstrType::AMOMAX_W: {
                // Signed maximum
                uint32_t old_val = bus.read32(addr);
                int32_t old_signed = static_cast<int32_t>(old_val);
                int32_t rs2_signed = static_cast<int32_t>(rs2_val);
                uint32_t new_val = (old_signed > rs2_signed) ? old_val : rs2_val;
                bus.write32(addr, new_val);
                regs.write(instr.rd, old_val);
                result.loaded_value = old_val;
                result.stored_value = new_val;
                result.memory_read = true;
                result.memory_written = true;
                break;
            }
            
            case InstrType::AMOMINU_W: {
                // Unsigned minimum
                uint32_t old_val = bus.read32(addr);
                uint32_t new_val = (old_val < rs2_val) ? old_val : rs2_val;
                bus.write32(addr, new_val);
                regs.write(instr.rd, old_val);
                result.loaded_value = old_val;
                result.stored_value = new_val;
                result.memory_read = true;
                result.memory_written = true;
                break;
            }
            
            case InstrType::AMOMAXU_W: {
                // Unsigned maximum
                uint32_t old_val = bus.read32(addr);
                uint32_t new_val = (old_val > rs2_val) ? old_val : rs2_val;
                bus.write32(addr, new_val);
                regs.write(instr.rd, old_val);
                result.loaded_value = old_val;
                result.stored_value = new_val;
                result.memory_read = true;
                result.memory_written = true;
                break;
            }
            
            default:
                result.valid = false;
                result.trap_cause = exception::ILLEGAL_INSTRUCTION;
                result.trap_value = instr.raw;
                result.error = "Unknown A instruction";
                break;
        }
        } catch (const std::exception&) {
            // Bus access error -> load/store access fault.
            result.valid = false;
            result.trap_cause = (instr.type == InstrType::LR_W)
                                ? exception::LOAD_ACCESS_FAULT
                                : exception::STORE_ACCESS_FAULT;
            result.trap_value = addr;
            result.error = "Atomic access fault";
            result.memory_read = false;
            result.memory_written = false;
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
    std::string suffix = ordering_suffix();
    
    switch (type) {
        case InstrType::LR_W:
            return std::string("lr.w") + suffix + " " + rd_s + ", (" + rs1_s + ")";
        case InstrType::SC_W:
            return std::string("sc.w") + suffix + " " + rd_s + ", " + rs2_s + ", (" + rs1_s + ")";
        case InstrType::AMOSWAP_W:
            return std::string("amoswap.w") + suffix + " " + rd_s + ", " + rs2_s + ", (" + rs1_s + ")";
        case InstrType::AMOADD_W:
            return std::string("amoadd.w") + suffix + " " + rd_s + ", " + rs2_s + ", (" + rs1_s + ")";
        case InstrType::AMOXOR_W:
            return std::string("amoxor.w") + suffix + " " + rd_s + ", " + rs2_s + ", (" + rs1_s + ")";
        case InstrType::AMOAND_W:
            return std::string("amoand.w") + suffix + " " + rd_s + ", " + rs2_s + ", (" + rs1_s + ")";
        case InstrType::AMOOR_W:
            return std::string("amoor.w") + suffix + " " + rd_s + ", " + rs2_s + ", (" + rs1_s + ")";
        case InstrType::AMOMIN_W:
            return std::string("amomin.w") + suffix + " " + rd_s + ", " + rs2_s + ", (" + rs1_s + ")";
        case InstrType::AMOMAX_W:
            return std::string("amomax.w") + suffix + " " + rd_s + ", " + rs2_s + ", (" + rs1_s + ")";
        case InstrType::AMOMINU_W:
            return std::string("amominu.w") + suffix + " " + rd_s + ", " + rs2_s + ", (" + rs1_s + ")";
        case InstrType::AMOMAXU_W:
            return std::string("amomaxu.w") + suffix + " " + rd_s + ", " + rs2_s + ", (" + rs1_s + ")";
        case InstrType::ILLEGAL:
            return "ILLEGAL";
    }
    return "UNKNOWN";
}

// ============================================================================
// Instruction Encoding Helpers
// ============================================================================

namespace encode {
    // Generic AMO encoding
    // R-type format: funct5[31:27] | aq[26] | rl[25] | rs2[24:20] | rs1[19:15] | 
    //                funct3[14:12] | rd[11:7] | opcode[6:0]
    inline uint32_t amo_instr(uint8_t funct5, bool aq, bool rl, 
                               uint8_t rs2, uint8_t rs1, uint8_t rd) {
        return (static_cast<uint32_t>(funct5 & 0x1F) << 27) |
               (static_cast<uint32_t>(aq ? 1 : 0) << 26) |
               (static_cast<uint32_t>(rl ? 1 : 0) << 25) |
               (static_cast<uint32_t>(rs2 & 0x1F) << 20) |
               (static_cast<uint32_t>(rs1 & 0x1F) << 15) |
               (static_cast<uint32_t>(funct3::W) << 12) |
               (static_cast<uint32_t>(rd & 0x1F) << 7) |
               opcode::AMO;
    }
    
    // LR.W rd, (rs1)
    inline uint32_t lr_w(uint8_t rd, uint8_t rs1, bool aq = false, bool rl = false) {
        return amo_instr(funct5::LR, aq, rl, 0, rs1, rd);
    }
    
    // SC.W rd, rs2, (rs1)
    inline uint32_t sc_w(uint8_t rd, uint8_t rs2, uint8_t rs1, bool aq = false, bool rl = false) {
        return amo_instr(funct5::SC, aq, rl, rs2, rs1, rd);
    }
    
    // AMOSWAP.W rd, rs2, (rs1)
    inline uint32_t amoswap_w(uint8_t rd, uint8_t rs2, uint8_t rs1, bool aq = false, bool rl = false) {
        return amo_instr(funct5::AMOSWAP, aq, rl, rs2, rs1, rd);
    }
    
    // AMOADD.W rd, rs2, (rs1)
    inline uint32_t amoadd_w(uint8_t rd, uint8_t rs2, uint8_t rs1, bool aq = false, bool rl = false) {
        return amo_instr(funct5::AMOADD, aq, rl, rs2, rs1, rd);
    }
    
    // AMOXOR.W rd, rs2, (rs1)
    inline uint32_t amoxor_w(uint8_t rd, uint8_t rs2, uint8_t rs1, bool aq = false, bool rl = false) {
        return amo_instr(funct5::AMOXOR, aq, rl, rs2, rs1, rd);
    }
    
    // AMOAND.W rd, rs2, (rs1)
    inline uint32_t amoand_w(uint8_t rd, uint8_t rs2, uint8_t rs1, bool aq = false, bool rl = false) {
        return amo_instr(funct5::AMOAND, aq, rl, rs2, rs1, rd);
    }
    
    // AMOOR.W rd, rs2, (rs1)
    inline uint32_t amoor_w(uint8_t rd, uint8_t rs2, uint8_t rs1, bool aq = false, bool rl = false) {
        return amo_instr(funct5::AMOOR, aq, rl, rs2, rs1, rd);
    }
    
    // AMOMIN.W rd, rs2, (rs1)
    inline uint32_t amomin_w(uint8_t rd, uint8_t rs2, uint8_t rs1, bool aq = false, bool rl = false) {
        return amo_instr(funct5::AMOMIN, aq, rl, rs2, rs1, rd);
    }
    
    // AMOMAX.W rd, rs2, (rs1)
    inline uint32_t amomax_w(uint8_t rd, uint8_t rs2, uint8_t rs1, bool aq = false, bool rl = false) {
        return amo_instr(funct5::AMOMAX, aq, rl, rs2, rs1, rd);
    }
    
    // AMOMINU.W rd, rs2, (rs1)
    inline uint32_t amominu_w(uint8_t rd, uint8_t rs2, uint8_t rs1, bool aq = false, bool rl = false) {
        return amo_instr(funct5::AMOMINU, aq, rl, rs2, rs1, rd);
    }
    
    // AMOMAXU.W rd, rs2, (rs1)
    inline uint32_t amomaxu_w(uint8_t rd, uint8_t rs2, uint8_t rs1, bool aq = false, bool rl = false) {
        return amo_instr(funct5::AMOMAXU, aq, rl, rs2, rs1, rd);
    }
}

} // namespace rv32a
} // namespace riscv

#endif // RISCV_RV32A_HPP
