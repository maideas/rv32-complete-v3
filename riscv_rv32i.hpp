/*******************************************************************************
 * RISC-V RV32I Base Integer Instruction Set Model
 * 
 * Decoder and executor for all 40 RV32I instructions.
 * Uses injected Bus interface for memory access.
 ******************************************************************************/

#ifndef RISCV_RV32I_HPP
#define RISCV_RV32I_HPP

#include "riscv_common.hpp"

namespace riscv {
namespace rv32i {

// ============================================================================
// Instruction Types
// ============================================================================

enum class InstrType {
    // R-type (Register-Register)
    ADD, SUB, SLL, SLT, SLTU, XOR, SRL, SRA, OR, AND,
    
    // I-type (Immediate)
    ADDI, SLTI, SLTIU, XORI, ORI, ANDI, SLLI, SRLI, SRAI,
    
    // Load instructions (I-type)
    LB, LH, LW, LBU, LHU,
    
    // Store instructions (S-type)
    SB, SH, SW,
    
    // Branch instructions (B-type)
    BEQ, BNE, BLT, BGE, BLTU, BGEU,
    
    // Upper immediate (U-type)
    LUI, AUIPC,
    
    // Jump instructions
    JAL, JALR,
    
    // System instructions
    ECALL, EBREAK, MRET, SRET, WFI, SFENCE_VMA,

    // Fence (FENCE.I is decoded by the separate Zifencei module)
    FENCE,

    ILLEGAL
};

// Instruction format types
enum class InstrFormat {
    R_TYPE, I_TYPE, S_TYPE, B_TYPE, U_TYPE, J_TYPE, SYSTEM, UNKNOWN
};

// ============================================================================
// Decoded Instruction
// ============================================================================

struct DecodedInstr {
    InstrType type;
    InstrFormat format;
    uint8_t rd;
    uint8_t rs1;
    uint8_t rs2;
    int32_t imm;
    uint8_t funct3;
    uint8_t funct7;
    uint32_t raw;
    
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
    bool mret;              // Instruction was MRET (CPU performs the state change)
    bool sret;              // Instruction was SRET
    bool sfence_vma;        // Instruction was SFENCE.VMA
    bool wfi;               // Instruction was WFI
    std::string trap_info;
};

// ============================================================================
// Opcodes and Function Codes
// ============================================================================

namespace opcode {
    constexpr uint8_t LUI    = 0b0110111;
    constexpr uint8_t AUIPC  = 0b0010111;
    constexpr uint8_t JAL    = 0b1101111;
    constexpr uint8_t JALR   = 0b1100111;
    constexpr uint8_t BRANCH = 0b1100011;
    constexpr uint8_t LOAD   = 0b0000011;
    constexpr uint8_t STORE  = 0b0100011;
    constexpr uint8_t OP_IMM = 0b0010011;
    constexpr uint8_t OP     = 0b0110011;
    constexpr uint8_t MISC   = 0b0001111;
    constexpr uint8_t SYSTEM = 0b1110011;
}

// ============================================================================
// Decoder
// ============================================================================

class Decoder {
public:
    // Configuration: decode SRET/SFENCE.VMA only when S-mode is implemented.
    bool s_mode_enabled = false;

    DecodedInstr decode(uint32_t instr) const {
        DecodedInstr d;
        d.raw = instr;
        d.type = InstrType::ILLEGAL;
        d.format = InstrFormat::UNKNOWN;
        
        d.rd = bits(instr, 11, 7);
        d.rs1 = bits(instr, 19, 15);
        d.rs2 = bits(instr, 24, 20);
        d.funct3 = bits(instr, 14, 12);
        d.funct7 = bits(instr, 31, 25);
        d.imm = 0;
        
        uint8_t op = bits(instr, 6, 0);
        
        switch (op) {
            case opcode::LUI:
                d.type = InstrType::LUI;
                d.format = InstrFormat::U_TYPE;
                d.imm = instr & 0xFFFFF000;
                break;
                
            case opcode::AUIPC:
                d.type = InstrType::AUIPC;
                d.format = InstrFormat::U_TYPE;
                d.imm = instr & 0xFFFFF000;
                break;
                
            case opcode::JAL:
                d.type = InstrType::JAL;
                d.format = InstrFormat::J_TYPE;
                d.imm = decode_j_imm(instr);
                break;
                
            case opcode::JALR:
                d.type = InstrType::JALR;
                d.format = InstrFormat::I_TYPE;
                d.imm = decode_i_imm(instr);
                break;
                
            case opcode::BRANCH:
                d.format = InstrFormat::B_TYPE;
                d.imm = decode_b_imm(instr);
                decode_branch(d);
                break;
                
            case opcode::LOAD:
                d.format = InstrFormat::I_TYPE;
                d.imm = decode_i_imm(instr);
                decode_load(d);
                break;
                
            case opcode::STORE:
                d.format = InstrFormat::S_TYPE;
                d.imm = decode_s_imm(instr);
                decode_store(d);
                break;
                
            case opcode::OP_IMM:
                d.format = InstrFormat::I_TYPE;
                d.imm = decode_i_imm(instr);
                decode_op_imm(d);
                break;
                
            case opcode::OP:
                d.format = InstrFormat::R_TYPE;
                decode_op(d);
                break;
                
            case opcode::MISC:
                d.format = InstrFormat::I_TYPE;
                decode_fence(d);
                break;
                
            case opcode::SYSTEM:
                d.format = InstrFormat::SYSTEM;
                decode_system(d);
                break;
        }
        
        return d;
    }

private:
    static int32_t decode_i_imm(uint32_t instr) {
        return static_cast<int32_t>(instr) >> 20;
    }
    
    static int32_t decode_s_imm(uint32_t instr) {
        int32_t imm = (bits(instr, 31, 25) << 5) | bits(instr, 11, 7);
        return (imm & 0x800) ? (imm | 0xFFFFF000) : imm;
    }
    
    static int32_t decode_b_imm(uint32_t instr) {
        int32_t imm = (bits(instr, 31, 31) << 12) |
                      (bits(instr, 7, 7) << 11) |
                      (bits(instr, 30, 25) << 5) |
                      (bits(instr, 11, 8) << 1);
        return (imm & 0x1000) ? (imm | 0xFFFFE000) : imm;
    }
    
    static int32_t decode_j_imm(uint32_t instr) {
        int32_t imm = (bits(instr, 31, 31) << 20) |
                      (bits(instr, 19, 12) << 12) |
                      (bits(instr, 20, 20) << 11) |
                      (bits(instr, 30, 21) << 1);
        return (imm & 0x100000) ? (imm | 0xFFE00000) : imm;
    }
    
    void decode_branch(DecodedInstr& d) const {
        switch (d.funct3) {
            case 0b000: d.type = InstrType::BEQ;  break;
            case 0b001: d.type = InstrType::BNE;  break;
            case 0b100: d.type = InstrType::BLT;  break;
            case 0b101: d.type = InstrType::BGE;  break;
            case 0b110: d.type = InstrType::BLTU; break;
            case 0b111: d.type = InstrType::BGEU; break;
        }
    }
    
    void decode_load(DecodedInstr& d) const {
        switch (d.funct3) {
            case 0b000: d.type = InstrType::LB;  break;
            case 0b001: d.type = InstrType::LH;  break;
            case 0b010: d.type = InstrType::LW;  break;
            case 0b100: d.type = InstrType::LBU; break;
            case 0b101: d.type = InstrType::LHU; break;
        }
    }
    
    void decode_store(DecodedInstr& d) const {
        switch (d.funct3) {
            case 0b000: d.type = InstrType::SB; break;
            case 0b001: d.type = InstrType::SH; break;
            case 0b010: d.type = InstrType::SW; break;
        }
    }
    
    void decode_op_imm(DecodedInstr& d) const {
        switch (d.funct3) {
            case 0b000: d.type = InstrType::ADDI;  break;
            case 0b010: d.type = InstrType::SLTI;  break;
            case 0b011: d.type = InstrType::SLTIU; break;
            case 0b100: d.type = InstrType::XORI;  break;
            case 0b110: d.type = InstrType::ORI;   break;
            case 0b111: d.type = InstrType::ANDI;  break;
            case 0b001:
                // SLLI: funct7 (imm[11:5]) must be 0000000 on RV32;
                // imm[5]=1 (shamt >= 32) and all other funct7 values are reserved.
                if (d.funct7 == 0b0000000) {
                    d.type = InstrType::SLLI;
                    d.imm = d.rs2;
                }
                break;
            case 0b101:
                // SRLI: funct7 = 0000000, SRAI: funct7 = 0100000.
                // Everything else (incl. shamt >= 32 encodings) is reserved.
                if (d.funct7 == 0b0000000) {
                    d.type = InstrType::SRLI;
                    d.imm = d.rs2;
                } else if (d.funct7 == 0b0100000) {
                    d.type = InstrType::SRAI;
                    d.imm = d.rs2;
                }
                break;
        }
    }
    
    void decode_op(DecodedInstr& d) const {
        // Only funct7 = 0000000 (base) and 0100000 (SUB/SRA) belong to RV32I.
        // All other funct7 values on the OP opcode are other extensions or
        // reserved and must not decode here.
        if (d.funct7 == 0b0000000) {
            switch (d.funct3) {
                case 0b000: d.type = InstrType::ADD;  break;
                case 0b001: d.type = InstrType::SLL;  break;
                case 0b010: d.type = InstrType::SLT;  break;
                case 0b011: d.type = InstrType::SLTU; break;
                case 0b100: d.type = InstrType::XOR;  break;
                case 0b101: d.type = InstrType::SRL;  break;
                case 0b110: d.type = InstrType::OR;   break;
                case 0b111: d.type = InstrType::AND;  break;
            }
        } else if (d.funct7 == 0b0100000) {
            switch (d.funct3) {
                case 0b000: d.type = InstrType::SUB; break;
                case 0b101: d.type = InstrType::SRA; break;
            }
        }
    }
    
    void decode_fence(DecodedInstr& d) const {
        // MISC-MEM funct3 000 is FENCE (fm/pred/succ fields are free-form;
        // reserved fm values execute as a conservative fence per the spec).
        // funct3 001 (FENCE.I) belongs to the Zifencei module; all other
        // funct3 values are reserved and remain ILLEGAL here.
        if (d.funct3 == 0b000) {
            d.type = InstrType::FENCE;
        }
    }
    
    void decode_system(DecodedInstr& d) const {
        if (d.funct3 == 0) {
            switch (d.raw) {
                case 0x00000073: d.type = InstrType::ECALL;  break;
                case 0x00100073: d.type = InstrType::EBREAK; break;
                case 0x30200073: d.type = InstrType::MRET;   break;
                case 0x10500073: d.type = InstrType::WFI;    break;
                default:
                    if (s_mode_enabled) {
                        if (d.raw == 0x10200073u) {
                            d.type = InstrType::SRET;
                            break;
                        }
                        // SFENCE.VMA: funct7=0001001, funct3=0, rd=0, opcode=SYSTEM.
                        if ((d.raw & 0xFE007FFFu) == 0x12000073u) {
                            d.type = InstrType::SFENCE_VMA;
                            // rs1 and rs2 are decoded already by decode()
                        }
                    }
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
    // Configuration (set by the integrating CPU):
    // - c_ext_enabled: IALIGN is 16 when true, 32 when false. Controls
    //   instruction-address-misaligned checks on jump/branch targets.
    // - allow_misaligned: when false, misaligned data accesses raise
    //   load/store-address-misaligned exceptions (causes 4/6).
    // - priv: current privilege level, needed for ECALL encoding.
    bool c_ext_enabled = true;
    bool allow_misaligned = false;
    PrivilegeLevel priv = PrivilegeLevel::MACHINE;

    ExecResult execute(const DecodedInstr& instr, RegFile& regs, Bus& bus, uint32_t pc) const {
        ExecResult result;
        result.next_pc = pc + 4;
        result.branch_taken = false;
        result.trap = false;
        result.trap_cause = 0;
        result.trap_value = 0;
        result.mret = false;
        result.sret = false;
        result.sfence_vma = false;
        result.wfi = false;
        
        uint32_t rs1_val = regs.read(instr.rs1);
        uint32_t rs2_val = regs.read(instr.rs2);
        int32_t rs1_signed = static_cast<int32_t>(rs1_val);
        int32_t rs2_signed = static_cast<int32_t>(rs2_val);
        
        switch (instr.type) {
            // U-type
            case InstrType::LUI:
                regs.write(instr.rd, instr.imm);
                break;
            case InstrType::AUIPC:
                regs.write(instr.rd, pc + instr.imm);
                break;
                
            // Jumps (target alignment is checked before any register write,
            // so a trapping jump has no architectural side effects)
            case InstrType::JAL: {
                uint32_t target = pc + instr.imm;
                if (target_misaligned(target)) {
                    set_fetch_misaligned(result, target);
                    break;
                }
                regs.write(instr.rd, pc + 4);
                result.next_pc = target;
                result.branch_taken = true;
                break;
            }
            case InstrType::JALR: {
                uint32_t target = (rs1_val + instr.imm) & ~1u;
                if (target_misaligned(target)) {
                    set_fetch_misaligned(result, target);
                    break;
                }
                regs.write(instr.rd, pc + 4);
                result.next_pc = target;
                result.branch_taken = true;
                break;
            }
                
            // Branches
            case InstrType::BEQ:
                if (rs1_val == rs2_val) {
                    take_branch(result, pc + instr.imm);
                }
                break;
            case InstrType::BNE:
                if (rs1_val != rs2_val) {
                    take_branch(result, pc + instr.imm);
                }
                break;
            case InstrType::BLT:
                if (rs1_signed < rs2_signed) {
                    take_branch(result, pc + instr.imm);
                }
                break;
            case InstrType::BGE:
                if (rs1_signed >= rs2_signed) {
                    take_branch(result, pc + instr.imm);
                }
                break;
            case InstrType::BLTU:
                if (rs1_val < rs2_val) {
                    take_branch(result, pc + instr.imm);
                }
                break;
            case InstrType::BGEU:
                if (rs1_val >= rs2_val) {
                    take_branch(result, pc + instr.imm);
                }
                break;
                
            // Loads
            case InstrType::LB: {
                uint32_t addr = rs1_val + instr.imm;
                uint32_t val;
                if (do_load(bus, addr, 1, val, result)) {
                    regs.write(instr.rd, static_cast<uint32_t>(
                        static_cast<int32_t>(static_cast<int8_t>(val))));
                }
                break;
            }
            case InstrType::LH: {
                uint32_t addr = rs1_val + instr.imm;
                uint32_t val;
                if (do_load(bus, addr, 2, val, result)) {
                    regs.write(instr.rd, static_cast<uint32_t>(
                        static_cast<int32_t>(static_cast<int16_t>(val))));
                }
                break;
            }
            case InstrType::LW: {
                uint32_t addr = rs1_val + instr.imm;
                uint32_t val;
                if (do_load(bus, addr, 4, val, result)) {
                    regs.write(instr.rd, val);
                }
                break;
            }
            case InstrType::LBU: {
                uint32_t addr = rs1_val + instr.imm;
                uint32_t val;
                if (do_load(bus, addr, 1, val, result)) {
                    regs.write(instr.rd, val & 0xFF);
                }
                break;
            }
            case InstrType::LHU: {
                uint32_t addr = rs1_val + instr.imm;
                uint32_t val;
                if (do_load(bus, addr, 2, val, result)) {
                    regs.write(instr.rd, val & 0xFFFF);
                }
                break;
            }
            
            // Stores
            case InstrType::SB: {
                do_store(bus, rs1_val + instr.imm, 1, rs2_val, result);
                break;
            }
            case InstrType::SH: {
                do_store(bus, rs1_val + instr.imm, 2, rs2_val, result);
                break;
            }
            case InstrType::SW: {
                do_store(bus, rs1_val + instr.imm, 4, rs2_val, result);
                break;
            }
            
            // I-type ALU
            case InstrType::ADDI:
                regs.write(instr.rd, rs1_val + instr.imm);
                break;
            case InstrType::SLTI:
                regs.write(instr.rd, (rs1_signed < instr.imm) ? 1 : 0);
                break;
            case InstrType::SLTIU:
                regs.write(instr.rd, (rs1_val < static_cast<uint32_t>(instr.imm)) ? 1 : 0);
                break;
            case InstrType::XORI:
                regs.write(instr.rd, rs1_val ^ instr.imm);
                break;
            case InstrType::ORI:
                regs.write(instr.rd, rs1_val | instr.imm);
                break;
            case InstrType::ANDI:
                regs.write(instr.rd, rs1_val & instr.imm);
                break;
            case InstrType::SLLI:
                regs.write(instr.rd, rs1_val << (instr.imm & 0x1F));
                break;
            case InstrType::SRLI:
                regs.write(instr.rd, rs1_val >> (instr.imm & 0x1F));
                break;
            case InstrType::SRAI:
                regs.write(instr.rd, static_cast<uint32_t>(rs1_signed >> (instr.imm & 0x1F)));
                break;
                
            // R-type ALU
            case InstrType::ADD:
                regs.write(instr.rd, rs1_val + rs2_val);
                break;
            case InstrType::SUB:
                regs.write(instr.rd, rs1_val - rs2_val);
                break;
            case InstrType::SLL:
                regs.write(instr.rd, rs1_val << (rs2_val & 0x1F));
                break;
            case InstrType::SLT:
                regs.write(instr.rd, (rs1_signed < rs2_signed) ? 1 : 0);
                break;
            case InstrType::SLTU:
                regs.write(instr.rd, (rs1_val < rs2_val) ? 1 : 0);
                break;
            case InstrType::XOR:
                regs.write(instr.rd, rs1_val ^ rs2_val);
                break;
            case InstrType::SRL:
                regs.write(instr.rd, rs1_val >> (rs2_val & 0x1F));
                break;
            case InstrType::SRA:
                regs.write(instr.rd, static_cast<uint32_t>(rs1_signed >> (rs2_val & 0x1F)));
                break;
            case InstrType::OR:
                regs.write(instr.rd, rs1_val | rs2_val);
                break;
            case InstrType::AND:
                regs.write(instr.rd, rs1_val & rs2_val);
                break;
                
            // Fence (NOP in this single-hart model)
            case InstrType::FENCE:
                break;
                
            // System
            case InstrType::ECALL:
                result.trap = true;
                if (priv == PrivilegeLevel::USER)
                    result.trap_cause = exception::ECALL_FROM_U;
                else if (priv == PrivilegeLevel::SUPERVISOR)
                    result.trap_cause = exception::ECALL_FROM_S;
                else
                    result.trap_cause = exception::ECALL_FROM_M;
                result.trap_value = 0;
                result.trap_info = "ECALL";
                break;
            case InstrType::EBREAK:
                result.trap = true;
                result.trap_cause = exception::BREAKPOINT;
                result.trap_value = pc;   // mtval = address of the EBREAK
                result.trap_info = "EBREAK";
                break;
            case InstrType::MRET:
                // The CPU performs the mstatus/mepc state change since the
                // executor has no CSR access.
                result.mret = true;
                break;
            case InstrType::SRET:
                result.sret = true;
                break;
            case InstrType::SFENCE_VMA:
                result.sfence_vma = true;
                break;
            case InstrType::WFI:
                result.wfi = true;
                break;
                
            case InstrType::ILLEGAL:
                result.trap = true;
                result.trap_cause = exception::ILLEGAL_INSTRUCTION;
                result.trap_value = instr.raw;   // mtval = faulting instruction
                result.trap_info = "Illegal instruction";
                break;
        }
        
        return result;
    }

private:
    bool target_misaligned(uint32_t target) const {
        // IALIGN = 16 with C (bit 0 already cleared/zero by construction),
        // IALIGN = 32 without C.
        return !c_ext_enabled && (target & 0x2);
    }
    
    void set_fetch_misaligned(ExecResult& result, uint32_t target) const {
        result.trap = true;
        result.trap_cause = exception::INSTR_ADDR_MISALIGNED;
        result.trap_value = target;
        result.trap_info = "Instruction address misaligned";
    }
    
    void take_branch(ExecResult& result, uint32_t target) const {
        if (target_misaligned(target)) {
            set_fetch_misaligned(result, target);
            return;
        }
        result.next_pc = target;
        result.branch_taken = true;
    }
    
    // Load/store via the shared mem_access helpers (alignment check and
    // bus-fault mapping); on failure the trap is recorded in the result.
    bool do_load(Bus& bus, uint32_t addr, uint32_t size, uint32_t& val,
                 ExecResult& result) const {
        uint32_t cause, tval;
        if (mem_access::load(bus, addr, size, allow_misaligned, val, cause, tval)) {
            return true;
        }
        result.trap = true;
        result.trap_cause = cause;
        result.trap_value = tval;
        result.trap_info = (cause == exception::LOAD_ADDR_MISALIGNED)
                           ? "Load address misaligned" : "Load access fault";
        return false;
    }
    
    bool do_store(Bus& bus, uint32_t addr, uint32_t size, uint32_t data,
                  ExecResult& result) const {
        uint32_t cause, tval;
        if (mem_access::store(bus, addr, size, data, allow_misaligned, cause, tval)) {
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
        case InstrType::ADD:  return std::string("add ") + rd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::SUB:  return std::string("sub ") + rd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::SLL:  return std::string("sll ") + rd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::SLT:  return std::string("slt ") + rd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::SLTU: return std::string("sltu ") + rd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::XOR:  return std::string("xor ") + rd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::SRL:  return std::string("srl ") + rd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::SRA:  return std::string("sra ") + rd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::OR:   return std::string("or ") + rd_s + ", " + rs1_s + ", " + rs2_s;
        case InstrType::AND:  return std::string("and ") + rd_s + ", " + rs1_s + ", " + rs2_s;
        
        case InstrType::ADDI:  return std::string("addi ") + rd_s + ", " + rs1_s + ", " + std::to_string(imm);
        case InstrType::SLTI:  return std::string("slti ") + rd_s + ", " + rs1_s + ", " + std::to_string(imm);
        case InstrType::SLTIU: return std::string("sltiu ") + rd_s + ", " + rs1_s + ", " + std::to_string(imm);
        case InstrType::XORI:  return std::string("xori ") + rd_s + ", " + rs1_s + ", " + std::to_string(imm);
        case InstrType::ORI:   return std::string("ori ") + rd_s + ", " + rs1_s + ", " + std::to_string(imm);
        case InstrType::ANDI:  return std::string("andi ") + rd_s + ", " + rs1_s + ", " + std::to_string(imm);
        case InstrType::SLLI:  return std::string("slli ") + rd_s + ", " + rs1_s + ", " + std::to_string(imm);
        case InstrType::SRLI:  return std::string("srli ") + rd_s + ", " + rs1_s + ", " + std::to_string(imm);
        case InstrType::SRAI:  return std::string("srai ") + rd_s + ", " + rs1_s + ", " + std::to_string(imm);
        
        case InstrType::LB:  return std::string("lb ") + rd_s + ", " + std::to_string(imm) + "(" + rs1_s + ")";
        case InstrType::LH:  return std::string("lh ") + rd_s + ", " + std::to_string(imm) + "(" + rs1_s + ")";
        case InstrType::LW:  return std::string("lw ") + rd_s + ", " + std::to_string(imm) + "(" + rs1_s + ")";
        case InstrType::LBU: return std::string("lbu ") + rd_s + ", " + std::to_string(imm) + "(" + rs1_s + ")";
        case InstrType::LHU: return std::string("lhu ") + rd_s + ", " + std::to_string(imm) + "(" + rs1_s + ")";
        
        case InstrType::SB: return std::string("sb ") + rs2_s + ", " + std::to_string(imm) + "(" + rs1_s + ")";
        case InstrType::SH: return std::string("sh ") + rs2_s + ", " + std::to_string(imm) + "(" + rs1_s + ")";
        case InstrType::SW: return std::string("sw ") + rs2_s + ", " + std::to_string(imm) + "(" + rs1_s + ")";
        
        case InstrType::BEQ:  return std::string("beq ") + rs1_s + ", " + rs2_s + ", " + std::to_string(imm);
        case InstrType::BNE:  return std::string("bne ") + rs1_s + ", " + rs2_s + ", " + std::to_string(imm);
        case InstrType::BLT:  return std::string("blt ") + rs1_s + ", " + rs2_s + ", " + std::to_string(imm);
        case InstrType::BGE:  return std::string("bge ") + rs1_s + ", " + rs2_s + ", " + std::to_string(imm);
        case InstrType::BLTU: return std::string("bltu ") + rs1_s + ", " + rs2_s + ", " + std::to_string(imm);
        case InstrType::BGEU: return std::string("bgeu ") + rs1_s + ", " + rs2_s + ", " + std::to_string(imm);
        
        case InstrType::LUI:   return std::string("lui ") + rd_s + ", " + std::to_string(static_cast<uint32_t>(imm) >> 12);
        case InstrType::AUIPC: return std::string("auipc ") + rd_s + ", " + std::to_string(static_cast<uint32_t>(imm) >> 12);
        
        case InstrType::JAL:  return std::string("jal ") + rd_s + ", " + std::to_string(imm);
        case InstrType::JALR: return std::string("jalr ") + rd_s + ", " + rs1_s + ", " + std::to_string(imm);
        
        case InstrType::ECALL:      return "ecall";
        case InstrType::EBREAK:     return "ebreak";
        case InstrType::MRET:       return "mret";
        case InstrType::SRET:       return "sret";
        case InstrType::SFENCE_VMA: return "sfence.vma";
        case InstrType::WFI:        return "wfi";
        case InstrType::FENCE:      return "fence";
        case InstrType::ILLEGAL:    return "ILLEGAL";
    }
    return "UNKNOWN";
}

} // namespace rv32i
} // namespace riscv

#endif // RISCV_RV32I_HPP
