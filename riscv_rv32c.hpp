/*******************************************************************************
 * RISC-V RV32C Compressed Extension Model
 * 
 * Decoder and executor for all compressed (16-bit) instructions.
 * Uses injected Bus interface for memory access.
 ******************************************************************************/

#ifndef RISCV_RV32C_HPP
#define RISCV_RV32C_HPP

#include "riscv_common.hpp"

namespace riscv {
namespace rv32c {

// ============================================================================
// Instruction Types
// ============================================================================

enum class InstrType {
    // Quadrant 0 (C0)
    C_ADDI4SPN, C_LW, C_SW,
    
    // Quadrant 1 (C1)
    C_NOP, C_ADDI, C_JAL, C_LI, C_ADDI16SP, C_LUI,
    C_SRLI, C_SRAI, C_ANDI, C_SUB, C_XOR, C_OR, C_AND,
    C_J, C_BEQZ, C_BNEZ,
    
    // Quadrant 2 (C2)
    C_SLLI, C_LWSP, C_JR, C_MV, C_EBREAK, C_JALR, C_ADD, C_SWSP,
    
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
    int32_t imm;
    uint16_t raw;
    
    std::string mnemonic() const;
};

// ============================================================================
// Execution Result
// ============================================================================

struct ExecResult {
    uint32_t next_pc;
    bool branch_taken;
    bool trap;
    uint32_t trap_cause;
    uint32_t trap_value;    // Value for mtval (faulting address / instruction)
    std::string trap_info;
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
        d.imm = 0;
        
        uint8_t op = instr & 0x3;
        
        switch (op) {
            case 0b00: decode_c0(instr, d); break;
            case 0b01: decode_c1(instr, d); break;
            case 0b10: decode_c2(instr, d); break;
        }
        
        return d;
    }
    
    bool is_compressed(uint16_t instr) const {
        return (instr & 0x3) != 0x3;
    }

private:
    void decode_c0(uint16_t instr, DecodedInstr& d) const {
        uint8_t funct3 = (instr >> 13) & 0x7;
        
        switch (funct3) {
            case 0b000: {  // C.ADDI4SPN
                uint32_t imm = ((instr >> 5) & 0x1) << 3 |
                               ((instr >> 6) & 0x1) << 2 |
                               ((instr >> 7) & 0xF) << 6 |
                               ((instr >> 11) & 0x3) << 4;
                if (imm != 0) {
                    d.type = InstrType::C_ADDI4SPN;
                    d.rd = creg_to_reg((instr >> 2) & 0x7);
                    d.rs1 = 2;
                    d.imm = imm;
                }
                break;
            }
            case 0b010: {  // C.LW
                d.type = InstrType::C_LW;
                d.rd = creg_to_reg((instr >> 2) & 0x7);
                d.rs1 = creg_to_reg((instr >> 7) & 0x7);
                d.imm = ((instr >> 5) & 0x1) << 6 |
                        ((instr >> 6) & 0x1) << 2 |
                        ((instr >> 10) & 0x7) << 3;
                break;
            }
            case 0b110: {  // C.SW
                d.type = InstrType::C_SW;
                d.rs2 = creg_to_reg((instr >> 2) & 0x7);
                d.rs1 = creg_to_reg((instr >> 7) & 0x7);
                d.imm = ((instr >> 5) & 0x1) << 6 |
                        ((instr >> 6) & 0x1) << 2 |
                        ((instr >> 10) & 0x7) << 3;
                break;
            }
        }
    }
    
    void decode_c1(uint16_t instr, DecodedInstr& d) const {
        uint8_t funct3 = (instr >> 13) & 0x7;
        
        switch (funct3) {
            case 0b000: {  // C.NOP / C.ADDI
                d.rd = (instr >> 7) & 0x1F;
                d.rs1 = d.rd;
                d.imm = ((instr >> 2) & 0x1F) | (((instr >> 12) & 0x1) << 5);
                if (d.imm & 0x20) d.imm |= 0xFFFFFFC0;
                d.type = (d.rd == 0) ? InstrType::C_NOP : InstrType::C_ADDI;
                break;
            }
            case 0b001: {  // C.JAL
                d.type = InstrType::C_JAL;
                d.rd = 1;
                d.imm = decode_cj_imm(instr);
                break;
            }
            case 0b010: {  // C.LI
                d.type = InstrType::C_LI;
                d.rd = (instr >> 7) & 0x1F;
                d.imm = ((instr >> 2) & 0x1F) | (((instr >> 12) & 0x1) << 5);
                if (d.imm & 0x20) d.imm |= 0xFFFFFFC0;
                break;
            }
            case 0b011: {  // C.ADDI16SP / C.LUI
                d.rd = (instr >> 7) & 0x1F;
                if (d.rd == 2) {
                    d.type = InstrType::C_ADDI16SP;
                    d.rs1 = 2;
                    d.imm = ((instr >> 2) & 0x1) << 5 |
                            ((instr >> 3) & 0x3) << 7 |
                            ((instr >> 5) & 0x1) << 6 |
                            ((instr >> 6) & 0x1) << 4 |
                            ((instr >> 12) & 0x1) << 9;
                    if (d.imm & 0x200) d.imm |= 0xFFFFFC00;
                } else if (d.rd != 0) {
                    // C.LUI with nzimm = 0 is a reserved encoding.
                    uint32_t nzimm6 = ((instr >> 2) & 0x1F) | (((instr >> 12) & 0x1) << 5);
                    if (nzimm6 != 0) {
                        d.type = InstrType::C_LUI;
                        d.imm = static_cast<int32_t>(nzimm6 << 12);
                        if (instr & 0x1000) d.imm |= 0xFFFC0000;
                    }
                }
                break;
            }
            case 0b100: {  // ALU operations
                uint8_t funct2 = (instr >> 10) & 0x3;
                d.rd = creg_to_reg((instr >> 7) & 0x7);
                d.rs1 = d.rd;
                
                switch (funct2) {
                    case 0b00:
                        // shamt[5] (bit 12) = 1 is reserved on RV32
                        if (((instr >> 12) & 0x1) == 0) {
                            d.type = InstrType::C_SRLI;
                            d.imm = (instr >> 2) & 0x1F;
                        }
                        break;
                    case 0b01:
                        if (((instr >> 12) & 0x1) == 0) {
                            d.type = InstrType::C_SRAI;
                            d.imm = (instr >> 2) & 0x1F;
                        }
                        break;
                    case 0b10:
                        d.type = InstrType::C_ANDI;
                        d.imm = ((instr >> 2) & 0x1F) | (((instr >> 12) & 0x1) << 5);
                        if (d.imm & 0x20) d.imm |= 0xFFFFFFC0;
                        break;
                    case 0b11: {
                        d.rs2 = creg_to_reg((instr >> 2) & 0x7);
                        uint8_t funct1 = (instr >> 12) & 0x1;
                        uint8_t funct2b = (instr >> 5) & 0x3;
                        if (funct1 == 0) {
                            switch (funct2b) {
                                case 0b00: d.type = InstrType::C_SUB; break;
                                case 0b01: d.type = InstrType::C_XOR; break;
                                case 0b10: d.type = InstrType::C_OR;  break;
                                case 0b11: d.type = InstrType::C_AND; break;
                            }
                        }
                        break;
                    }
                }
                break;
            }
            case 0b101: {  // C.J
                d.type = InstrType::C_J;
                d.imm = decode_cj_imm(instr);
                break;
            }
            case 0b110: {  // C.BEQZ
                d.type = InstrType::C_BEQZ;
                d.rs1 = creg_to_reg((instr >> 7) & 0x7);
                d.imm = decode_cb_imm(instr);
                break;
            }
            case 0b111: {  // C.BNEZ
                d.type = InstrType::C_BNEZ;
                d.rs1 = creg_to_reg((instr >> 7) & 0x7);
                d.imm = decode_cb_imm(instr);
                break;
            }
        }
    }
    
    void decode_c2(uint16_t instr, DecodedInstr& d) const {
        uint8_t funct3 = (instr >> 13) & 0x7;
        
        switch (funct3) {
            case 0b000: {  // C.SLLI
                // shamt[5] (bit 12) = 1 is reserved on RV32
                if (((instr >> 12) & 0x1) == 0) {
                    d.type = InstrType::C_SLLI;
                    d.rd = (instr >> 7) & 0x1F;
                    d.rs1 = d.rd;
                    d.imm = (instr >> 2) & 0x1F;
                }
                break;
            }
            case 0b010: {  // C.LWSP (rd = x0 is a reserved encoding)
                uint8_t rd = (instr >> 7) & 0x1F;
                if (rd != 0) {
                    d.type = InstrType::C_LWSP;
                    d.rd = rd;
                    d.rs1 = 2;
                    d.imm = ((instr >> 2) & 0x3) << 6 |
                            ((instr >> 4) & 0x7) << 2 |
                            ((instr >> 12) & 0x1) << 5;
                }
                break;
            }
            case 0b100: {
                uint8_t rs1 = (instr >> 7) & 0x1F;
                uint8_t rs2 = (instr >> 2) & 0x1F;
                uint8_t funct1 = (instr >> 12) & 0x1;
                
                if (funct1 == 0) {
                    if (rs2 == 0) {
                        // C.JR with rs1 = x0 is a reserved encoding
                        if (rs1 != 0) {
                            d.type = InstrType::C_JR;
                            d.rs1 = rs1;
                        }
                    } else {
                        d.type = InstrType::C_MV;
                        d.rd = rs1;
                        d.rs2 = rs2;
                    }
                } else {
                    if (rs1 == 0 && rs2 == 0) {
                        d.type = InstrType::C_EBREAK;
                    } else if (rs2 == 0) {
                        d.type = InstrType::C_JALR;
                        d.rd = 1;
                        d.rs1 = rs1;
                    } else {
                        d.type = InstrType::C_ADD;
                        d.rd = rs1;
                        d.rs1 = rs1;
                        d.rs2 = rs2;
                    }
                }
                break;
            }
            case 0b110: {  // C.SWSP
                d.type = InstrType::C_SWSP;
                d.rs1 = 2;
                d.rs2 = (instr >> 2) & 0x1F;
                d.imm = ((instr >> 7) & 0x3) << 6 |
                        ((instr >> 9) & 0xF) << 2;
                break;
            }
        }
    }
    
    static int32_t decode_cj_imm(uint16_t instr) {
        int32_t imm = ((instr >> 2) & 0x1) << 5 |
                      ((instr >> 3) & 0x7) << 1 |
                      ((instr >> 6) & 0x1) << 7 |
                      ((instr >> 7) & 0x1) << 6 |
                      ((instr >> 8) & 0x1) << 10 |
                      ((instr >> 9) & 0x3) << 8 |
                      ((instr >> 11) & 0x1) << 4 |
                      ((instr >> 12) & 0x1) << 11;
        if (imm & 0x800) imm |= 0xFFFFF000;
        return imm;
    }
    
    static int32_t decode_cb_imm(uint16_t instr) {
        int32_t imm = ((instr >> 2) & 0x1) << 5 |
                      ((instr >> 3) & 0x3) << 1 |
                      ((instr >> 5) & 0x3) << 6 |
                      ((instr >> 10) & 0x3) << 3 |
                      ((instr >> 12) & 0x1) << 8;
        if (imm & 0x100) imm |= 0xFFFFFE00;
        return imm;
    }
};

// ============================================================================
// Executor
// ============================================================================

class Executor {
public:
    // When false, misaligned data accesses raise address-misaligned exceptions.
    bool allow_misaligned = false;
    
    ExecResult execute(const DecodedInstr& instr, RegFile& regs, Bus& bus, uint32_t pc) const {
        ExecResult result;
        result.next_pc = pc + 2;
        result.branch_taken = false;
        result.trap = false;
        result.trap_cause = 0;
        result.trap_value = 0;
        
        switch (instr.type) {
            case InstrType::C_ADDI4SPN:
                regs.write(instr.rd, regs.read(2) + instr.imm);
                break;
            case InstrType::C_LW: {
                uint32_t val;
                if (do_load32(bus, regs.read(instr.rs1) + instr.imm, val, result)) {
                    regs.write(instr.rd, val);
                }
                break;
            }
            case InstrType::C_SW:
                do_store32(bus, regs.read(instr.rs1) + instr.imm,
                           regs.read(instr.rs2), result);
                break;
            case InstrType::C_NOP:
                break;
            case InstrType::C_ADDI:
                regs.write(instr.rd, regs.read(instr.rs1) + instr.imm);
                break;
            case InstrType::C_JAL:
                regs.write(1, pc + 2);
                result.next_pc = pc + instr.imm;
                result.branch_taken = true;
                break;
            case InstrType::C_LI:
                regs.write(instr.rd, instr.imm);
                break;
            case InstrType::C_ADDI16SP:
                regs.write(2, regs.read(2) + instr.imm);
                break;
            case InstrType::C_LUI:
                regs.write(instr.rd, instr.imm);
                break;
            case InstrType::C_SRLI:
                regs.write(instr.rd, regs.read(instr.rs1) >> (instr.imm & 0x1F));
                break;
            case InstrType::C_SRAI:
                regs.write(instr.rd, static_cast<uint32_t>(
                    static_cast<int32_t>(regs.read(instr.rs1)) >> (instr.imm & 0x1F)));
                break;
            case InstrType::C_ANDI:
                regs.write(instr.rd, regs.read(instr.rs1) & instr.imm);
                break;
            case InstrType::C_SUB:
                regs.write(instr.rd, regs.read(instr.rs1) - regs.read(instr.rs2));
                break;
            case InstrType::C_XOR:
                regs.write(instr.rd, regs.read(instr.rs1) ^ regs.read(instr.rs2));
                break;
            case InstrType::C_OR:
                regs.write(instr.rd, regs.read(instr.rs1) | regs.read(instr.rs2));
                break;
            case InstrType::C_AND:
                regs.write(instr.rd, regs.read(instr.rs1) & regs.read(instr.rs2));
                break;
            case InstrType::C_J:
                result.next_pc = pc + instr.imm;
                result.branch_taken = true;
                break;
            case InstrType::C_BEQZ:
                if (regs.read(instr.rs1) == 0) {
                    result.next_pc = pc + instr.imm;
                    result.branch_taken = true;
                }
                break;
            case InstrType::C_BNEZ:
                if (regs.read(instr.rs1) != 0) {
                    result.next_pc = pc + instr.imm;
                    result.branch_taken = true;
                }
                break;
            case InstrType::C_SLLI:
                regs.write(instr.rd, regs.read(instr.rs1) << (instr.imm & 0x1F));
                break;
            case InstrType::C_LWSP: {
                uint32_t val;
                if (do_load32(bus, regs.read(2) + instr.imm, val, result)) {
                    regs.write(instr.rd, val);
                }
                break;
            }
            case InstrType::C_JR:
                result.next_pc = regs.read(instr.rs1) & ~1u;
                result.branch_taken = true;
                break;
            case InstrType::C_MV:
                regs.write(instr.rd, regs.read(instr.rs2));
                break;
            case InstrType::C_EBREAK:
                result.trap = true;
                result.trap_cause = exception::BREAKPOINT;
                result.trap_value = pc;   // mtval = address of the EBREAK
                result.trap_info = "C.EBREAK";
                break;
            case InstrType::C_JALR: {
                // Read the jump target BEFORE writing the link register,
                // so that c.jalr ra (rs1 == x1) uses the old value of ra.
                uint32_t target = regs.read(instr.rs1) & ~1u;
                regs.write(1, pc + 2);
                result.next_pc = target;
                result.branch_taken = true;
                break;
            }
            case InstrType::C_ADD:
                regs.write(instr.rd, regs.read(instr.rs1) + regs.read(instr.rs2));
                break;
            case InstrType::C_SWSP:
                do_store32(bus, regs.read(2) + instr.imm,
                           regs.read(instr.rs2), result);
                break;
            case InstrType::ILLEGAL:
                result.trap = true;
                result.trap_cause = exception::ILLEGAL_INSTRUCTION;
                result.trap_value = instr.raw;   // mtval = faulting instruction
                result.trap_info = "Illegal C instruction";
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
        result.trap = true;
        result.trap_cause = cause;
        result.trap_value = tval;
        result.trap_info = (cause == exception::LOAD_ADDR_MISALIGNED)
                           ? "Load address misaligned" : "Load access fault";
        return false;
    }
    
    bool do_store32(Bus& bus, uint32_t addr, uint32_t data, ExecResult& result) const {
        uint32_t cause, tval;
        if (mem_access::store(bus, addr, 4, data, allow_misaligned, cause, tval)) {
            return true;
        }
        result.trap = true;
        result.trap_cause = cause;
        result.trap_value = tval;
        result.trap_info = (cause == exception::STORE_ADDR_MISALIGNED)
                           ? "Store address misaligned" : "Store access fault";
        return false;
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
        case InstrType::C_ADDI4SPN: return std::string("c.addi4spn ") + rd_s + ", sp, " + std::to_string(imm);
        case InstrType::C_LW:       return std::string("c.lw ") + rd_s + ", " + std::to_string(imm) + "(" + rs1_s + ")";
        case InstrType::C_SW:       return std::string("c.sw ") + rs2_s + ", " + std::to_string(imm) + "(" + rs1_s + ")";
        case InstrType::C_NOP:      return "c.nop";
        case InstrType::C_ADDI:     return std::string("c.addi ") + rd_s + ", " + std::to_string(imm);
        case InstrType::C_JAL:      return std::string("c.jal ") + std::to_string(imm);
        case InstrType::C_LI:       return std::string("c.li ") + rd_s + ", " + std::to_string(imm);
        case InstrType::C_ADDI16SP: return std::string("c.addi16sp sp, ") + std::to_string(imm);
        case InstrType::C_LUI:      return std::string("c.lui ") + rd_s + ", " + std::to_string(imm >> 12);
        case InstrType::C_SRLI:     return std::string("c.srli ") + rd_s + ", " + std::to_string(imm);
        case InstrType::C_SRAI:     return std::string("c.srai ") + rd_s + ", " + std::to_string(imm);
        case InstrType::C_ANDI:     return std::string("c.andi ") + rd_s + ", " + std::to_string(imm);
        case InstrType::C_SUB:      return std::string("c.sub ") + rd_s + ", " + rs2_s;
        case InstrType::C_XOR:      return std::string("c.xor ") + rd_s + ", " + rs2_s;
        case InstrType::C_OR:       return std::string("c.or ") + rd_s + ", " + rs2_s;
        case InstrType::C_AND:      return std::string("c.and ") + rd_s + ", " + rs2_s;
        case InstrType::C_J:        return std::string("c.j ") + std::to_string(imm);
        case InstrType::C_BEQZ:     return std::string("c.beqz ") + rs1_s + ", " + std::to_string(imm);
        case InstrType::C_BNEZ:     return std::string("c.bnez ") + rs1_s + ", " + std::to_string(imm);
        case InstrType::C_SLLI:     return std::string("c.slli ") + rd_s + ", " + std::to_string(imm);
        case InstrType::C_LWSP:     return std::string("c.lwsp ") + rd_s + ", " + std::to_string(imm) + "(sp)";
        case InstrType::C_JR:       return std::string("c.jr ") + rs1_s;
        case InstrType::C_MV:       return std::string("c.mv ") + rd_s + ", " + rs2_s;
        case InstrType::C_EBREAK:   return "c.ebreak";
        case InstrType::C_JALR:     return std::string("c.jalr ") + rs1_s;
        case InstrType::C_ADD:      return std::string("c.add ") + rd_s + ", " + rs2_s;
        case InstrType::C_SWSP:     return std::string("c.swsp ") + rs2_s + ", " + std::to_string(imm) + "(sp)";
        case InstrType::ILLEGAL:    return "ILLEGAL";
    }
    return "UNKNOWN";
}

} // namespace rv32c
} // namespace riscv

#endif // RISCV_RV32C_HPP
