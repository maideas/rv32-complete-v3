/*******************************************************************************
 * RISC-V Zifencei Extension Model
 * 
 * Instruction-Fetch Fence extension.
 * 
 * This extension adds a single instruction:
 *   - FENCE.I: Synchronize instruction and data streams
 * 
 * FENCE.I ensures that all subsequent instruction fetches see any previous
 * data stores. This is essential for:
 *   - Self-modifying code
 *   - JIT compilation
 *   - Bootloaders writing code to memory
 *   - Firmware updates
 * 
 * In hardware, FENCE.I typically:
 *   - Flushes the instruction cache
 *   - Ensures all pending stores are visible to instruction fetch
 *   - May stall until store buffers drain
 * 
 * In a software model, FENCE.I is typically a no-op since there's no
 * separate instruction cache, but we track it for correctness.
 ******************************************************************************/

#ifndef RISCV_ZIFENCEI_HPP
#define RISCV_ZIFENCEI_HPP

#include "riscv_common.hpp"

namespace riscv {
namespace zifencei {

// ============================================================================
// Instruction Types
// ============================================================================

enum class InstrType {
    FENCE_I,
    ILLEGAL
};

// ============================================================================
// Decoded Instruction
// ============================================================================

struct DecodedInstr {
    InstrType type;
    uint32_t raw;
    
    // FENCE.I has no meaningful operands - imm, rs1, rd should all be 0
    // but implementations must ignore them
    uint8_t rd;
    uint8_t rs1;
    uint16_t imm;
    
    std::string mnemonic() const {
        switch (type) {
            case InstrType::FENCE_I: return "fence.i";
            case InstrType::ILLEGAL: return "ILLEGAL";
        }
        return "UNKNOWN";
    }
};

// ============================================================================
// Execution Result
// ============================================================================

struct ExecResult {
    bool valid;
    bool fence_executed;    // FENCE.I was executed
    std::string error;
};

// ============================================================================
// Opcodes and Function Codes
// ============================================================================

namespace opcode {
    constexpr uint8_t MISC_MEM = 0b0001111;  // Same as FENCE
}

namespace funct3 {
    constexpr uint8_t FENCE_I = 0b001;
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
        d.imm = bits(instr, 31, 20);
        
        uint8_t op = bits(instr, 6, 0);
        uint8_t funct3 = bits(instr, 14, 12);
        
        if (op == opcode::MISC_MEM && funct3 == funct3::FENCE_I) {
            d.type = InstrType::FENCE_I;
        }
        
        return d;
    }
    
    bool is_zifencei_instruction(uint32_t instr) const {
        uint8_t op = bits(instr, 6, 0);
        uint8_t funct3 = bits(instr, 14, 12);
        return (op == opcode::MISC_MEM) && (funct3 == funct3::FENCE_I);
    }
};

// ============================================================================
// Executor
// ============================================================================

/**
 * Executor with callback for implementation-specific behavior.
 * 
 * The on_fence_i callback can be used to:
 *   - Flush instruction caches in a simulator
 *   - Signal pipeline flush in a cycle-accurate model
 *   - Track fence operations for debugging
 */
class Executor {
public:
    using FenceCallback = void(*)();
    
private:
    FenceCallback on_fence_i = nullptr;
    
public:
    void set_fence_callback(FenceCallback cb) {
        on_fence_i = cb;
    }
    
    ExecResult execute(const DecodedInstr& instr) const {
        ExecResult result = {};
        result.valid = true;
        result.fence_executed = false;
        
        if (instr.type == InstrType::ILLEGAL) {
            result.valid = false;
            result.error = "Illegal Zifencei instruction";
            return result;
        }
        
        switch (instr.type) {
            case InstrType::FENCE_I:
                result.fence_executed = true;
                if (on_fence_i) {
                    on_fence_i();
                }
                break;
                
            default:
                result.valid = false;
                result.error = "Unknown Zifencei instruction";
                break;
        }
        
        return result;
    }
};

// ============================================================================
// Instruction Encoding
// ============================================================================

namespace encode {
    // FENCE.I encoding: imm=0, rs1=0, funct3=001, rd=0, opcode=0001111
    inline uint32_t fence_i() {
        return (funct3::FENCE_I << 12) | opcode::MISC_MEM;
    }
}

} // namespace zifencei
} // namespace riscv

#endif // RISCV_ZIFENCEI_HPP
