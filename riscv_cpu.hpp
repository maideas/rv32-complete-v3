/*******************************************************************************
 * RISC-V RV32IMAFC_Zicsr_Zifencei_Zba_Zbb_Zbs_Zicond CPU Model
 * 
 * Top-level CPU model integrating:
 *   - RV32I Base Integer Instruction Set (incl. MRET / SRET / SFENCE.VMA / WFI)
 *   - M  Extension (Multiply-Divide)
 *   - A  Extension (Atomics: LR/SC and AMOs)
 *   - F  Extension (Single-Precision Floating-Point, incl. compressed
 *        C.FLW/C.FSW/C.FLWSP/C.FSWSP when C is also enabled)
 *   - C  Extension (Compressed Instructions)
 *   - Zicsr    (CSR Instructions)
 *   - Zifencei (FENCE.I)
 *   - Zba / Zbb / Zbs (Bit-Manipulation)
 *   - Zicond   (Conditional Zero)
 * 
 * Machine-mode hart with optional Supervisor and User modes
 * (CPUConfig::enable_s_mode / enable_u_mode):
 *   - Synchronous exception handling (xepc/xcause/xtval/xstatus update,
 *     redirection to mtvec/stvec) — execution continues in the trap handler.
 *   - Trap delegation from S/U to S mode via medeleg/mideleg.
 *   - M-mode interrupts (MEI/MSI/MTI) and S-mode interrupts (SEI/SSI/STI)
 *     with mstatus.MIE/SIE / mie / mip gating and vectored-mtvec/stvec
 *     support; standard priority order MEI > MSI > MTI > SEI > SSI > STI
 *     within a target privilege, M-target interrupts taking precedence
 *     over S-target ones regardless of cause priority.
 *   - MRET/SRET privilege transitions and the sstatus subset of mstatus.
 *   - Precise mtval/stval values (faulting address / instruction).
 *   - 16-bit-granular instruction fetch, so a compressed instruction in
 *     the last halfword of memory does not over-fetch.
 *   - Optional opcode-injection stepping (step(bus, opcode)) that bypasses
 *     instruction fetch for cosimulation / trace replay / direct stimulus.
 * 
 * Uses the injected Bus interface for all memory access. Bus errors
 * (riscv::BusFault or any std::exception) become access-fault exceptions.
 ******************************************************************************/

#ifndef RISCV_CPU_HPP
#define RISCV_CPU_HPP

#include "riscv_common.hpp"
#include "riscv_rv32i.hpp"
#include "riscv_rv32m.hpp"
#include "riscv_rv32a.hpp"
#include "riscv_rv32c.hpp"
#include "riscv_rv32f.hpp"
#include "riscv_rv32fc.hpp"
#include "riscv_zicsr.hpp"
#include "riscv_zifencei.hpp"
#include "riscv_zba.hpp"
#include "riscv_zbb.hpp"
#include "riscv_zbs.hpp"
#include "riscv_zicond.hpp"

namespace riscv {

// ============================================================================
// CPU Configuration
// ============================================================================

struct CPUConfig {
    bool enable_m_extension = true;   // Multiply-Divide
    bool enable_a_extension = true;   // Atomics
    bool enable_f_extension = true;   // Single-precision floating point
    bool enable_c_extension = true;   // Compressed
    bool enable_zicsr = true;         // CSR instructions
    bool enable_zifencei = true;      // FENCE.I
    bool enable_zba = true;           // Address-generation bitmanip
    bool enable_zbb = true;           // Basic bitmanip
    bool enable_zbs = true;           // Single-bit bitmanip
    bool enable_zicond = true;        // Conditional zero
    bool enable_s_mode = false;       // Supervisor mode
    bool enable_u_mode = false;       // User mode
    bool allow_misaligned_data = false; // false: misaligned data accesses trap
    uint32_t reset_vector = 0;        // Initial PC value
    uint32_t mtvec_reset = 0;         // Initial trap vector
};

// ============================================================================
// CPU Execution Result
// ============================================================================

struct CPUExecResult {
    uint32_t pc;
    uint32_t next_pc;
    uint32_t instruction;
    uint8_t instr_size;       // 2 for compressed, 4 for regular (0 on interrupt)
    bool branch_taken;
    bool trap;                // A synchronous exception was taken
    bool interrupt;           // An interrupt was taken (no instruction executed)
    uint32_t trap_cause;      // mcause value (interrupt bit included for interrupts)
    uint32_t trap_value;      // mtval value
    std::string trap_info;
    std::string mnemonic;
};

// ============================================================================
// RV32IMAFC_Zicsr_Zifencei_Zba_Zbb_Zbs_Zicond CPU
// ============================================================================

class CPU {
public:
    // ========================================================================
    // CPU State
    // ========================================================================
    
    RegFile regs;                     // Integer register file
    rv32f::FRegFile fregs;            // Floating-point register file
    zicsr::CSRFile csrs;              // Control and Status Registers
    uint32_t pc = 0;                  // Program counter
    rv32a::ReservationSet reservation; // LR/SC reservation
    
    // ========================================================================
    // Configuration
    // ========================================================================
    
    CPUConfig config;
    
    // ========================================================================
    // Sub-module decoders and executors
    // ========================================================================
    
    rv32i::Decoder i_decoder;
    rv32i::Executor i_executor;
    
    rv32m::Decoder m_decoder;
    rv32m::Executor m_executor;
    
    rv32a::Decoder a_decoder;
    rv32a::Executor a_executor;
    
    rv32c::Decoder c_decoder;
    rv32c::Executor c_executor;
    
    rv32f::Decoder f_decoder;
    rv32f::Executor f_executor;
    
    rv32fc::Decoder fc_decoder;
    rv32fc::Executor fc_executor;
    
    zicsr::Decoder csr_decoder;
    zicsr::Executor csr_executor;
    
    zifencei::Decoder zifencei_decoder;
    zifencei::Executor zifencei_executor;
    
    zba::Decoder zba_decoder;
    zba::Executor zba_executor;
    
    zbb::Decoder zbb_decoder;
    zbb::Executor zbb_executor;
    
    zbs::Decoder zbs_decoder;
    zbs::Executor zbs_executor;
    
    zicond::Decoder zicond_decoder;
    zicond::Executor zicond_executor;
    
    // ========================================================================
    // Constructor
    // ========================================================================
    
    explicit CPU(const CPUConfig& cfg = CPUConfig()) : config(cfg) {
        reset();
    }
    
    // ========================================================================
    // Reset
    // ========================================================================
    
    void reset() {
        pc = config.reset_vector;
        regs.reset();
        fregs = rv32f::FRegFile();
        reservation.clear();
        
        // Configure the CSR file first, then (re-)initialize it so the
        // hardwired fields reflect the configuration.  Note: the S/U-mode
        // flags must be set before set_f_extension(), because the latter
        // re-legalizes mstatus using the mode-dependent legal mask.
        csrs.set_s_mode(config.enable_s_mode);
        csrs.set_u_mode(config.enable_u_mode);
        csrs.set_f_extension(config.enable_f_extension);
        csrs.set_mepc_mask(config.enable_c_extension ? ~1u : ~3u);
        csrs.reset();
        csrs.write(zicsr::csr_addr::MTVEC, config.mtvec_reset);

        sync_executor_config();

        // Set MISA based on enabled extensions
        uint32_t misa = 0x40000100;  // RV32 (MXL=1), I
        if (config.enable_a_extension) misa |= (1u << 0);   // A
        if (config.enable_c_extension) misa |= (1u << 2);   // C
        if (config.enable_f_extension) misa |= (1u << 5);   // F
        if (config.enable_m_extension) misa |= (1u << 12);  // M
        if (config.enable_s_mode)      misa |= (1u << 18);  // S
        if (config.enable_u_mode)      misa |= (1u << 20);  // U
        csrs.set_misa(misa);
    }
    
    // ========================================================================
    // Interrupt input lines (level-sensitive; drive mip)
    // ========================================================================

    void set_external_interrupt(bool level) {
        set_mip_bit(zicsr::CSRFile::MI_MEI, level);
    }
    void set_timer_interrupt(bool level) {
        set_mip_bit(zicsr::CSRFile::MI_MTI, level);
    }
    void set_software_interrupt(bool level) {
        set_mip_bit(zicsr::CSRFile::MI_MSI, level);
    }

    void set_supervisor_external_interrupt(bool level) {
        set_mip_bit(zicsr::CSRFile::MI_SEI, level);
    }
    void set_supervisor_timer_interrupt(bool level) {
        set_mip_bit(zicsr::CSRFile::MI_STI, level);
    }
    void set_supervisor_software_interrupt(bool level) {
        set_mip_bit(zicsr::CSRFile::MI_SSI, level);
    }
    
    // ========================================================================
    // Single Step Execution
    // ========================================================================

    // Fetch the instruction at pc from the bus and execute it.
    CPUExecResult step(Bus& bus) {
        CPUExecResult result;
        if (begin_step(result)) return result;   // interrupt taken

        // --------------------------------------------------------------
        // Fetch (16-bit granular so that a compressed instruction in
        // the last halfword of memory does not over-fetch).
        // --------------------------------------------------------------
        uint16_t parcel0;
        try {
            parcel0 = bus.fetch16(pc);
        } catch (const std::exception&) {
            take_exception(result, exception::INSTR_ACCESS_FAULT, pc,
                           "Instruction access fault");
            finish_step(result);
            return result;
        }

        uint32_t instr = parcel0;
        bool is_32bit = ((parcel0 & 0x3) == 0x3);

        if (is_32bit) {
            uint16_t parcel1;
            try {
                parcel1 = bus.fetch16(pc + 2);
            } catch (const std::exception&) {
                // mtval holds the address of the PORTION of the instruction
                // that faulted (pc + 2); mepc still points at the start.
                take_exception(result, exception::INSTR_ACCESS_FAULT, pc + 2,
                               "Instruction access fault");
                finish_step(result);
                return result;
            }
            instr |= static_cast<uint32_t>(parcel1) << 16;
        }
        result.instruction = instr;

        execute_fetched(instr, is_32bit, bus, result);
        return result;
    }

    // Execute a caller-supplied opcode instead of fetching from the bus
    // (compressed or 32-bit, selected by the two low bits as usual).
    // Useful for cosimulation, trace replay, and direct opcode injection;
    // the bus is still used for data loads/stores and interrupts are still
    // sampled first. Because no fetch happens, instruction access faults
    // cannot occur on this path and fetch side effects are not modeled.
    // The caller keeps pc coherent with the injected stream.
    CPUExecResult step(Bus& bus, uint32_t opcode) {
        CPUExecResult result;
        if (begin_step(result)) return result;   // interrupt taken

        bool is_32bit = ((opcode & 0x3) == 0x3);
        result.instruction = opcode;

        execute_fetched(opcode, is_32bit, bus, result);
        return result;
    }
    
    // ========================================================================
    // Run for up to N steps.
    //
    // - Handled traps redirect to mtvec and execution continues; set
    //   stop_on_trap to stop at the first trap/interrupt instead.
    // - The return value counts STEPS: retired instructions, trapped
    //   instructions, and interrupt deliveries all count as one step.
    // - CAUTION: the default bound is effectively infinite. A guest
    //   program that loops forever (including a trap loop at mtvec)
    //   will hang the caller — pass a finite max_instructions when
    //   executing untrusted or randomly generated stimulus.
    // ========================================================================
    
    uint64_t run(Bus& bus, uint64_t max_instructions = UINT64_MAX,
                 bool stop_on_trap = false) {
        uint64_t count = 0;
        while (count < max_instructions) {
            auto result = step(bus);
            count++;
            if (stop_on_trap && (result.trap || result.interrupt)) break;
        }
        return count;
    }
    
    // ========================================================================
    // Disassemble instruction at address
    // ========================================================================
    
    // Debug utility: never throws. Returns "<fetch fault>" if the address
    // is not readable on the given bus.
    std::string disasm(Bus& bus, uint32_t addr) {
        i_decoder.s_mode_enabled = config.enable_s_mode;
        uint32_t lo;
        try {
            lo = bus.read16(addr);
        } catch (const std::exception&) {
            return "<fetch fault>";
        }
        
        if ((lo & 0x3) != 0x3) {
            if (!config.enable_c_extension) return "ILLEGAL";
            uint16_t c_instr = static_cast<uint16_t>(lo);
            if (config.enable_f_extension &&
                fc_decoder.is_compressed_float(c_instr)) {
                return fc_decoder.decode(c_instr).mnemonic();
            }
            return c_decoder.decode(c_instr).mnemonic();
        }
        
        uint32_t hi;
        try {
            hi = bus.read16(addr + 2);
        } catch (const std::exception&) {
            return "<fetch fault>";
        }
        uint32_t instr = lo | (hi << 16);
        
        if (config.enable_zifencei && zifencei_decoder.is_zifencei_instruction(instr)) {
            return zifencei_decoder.decode(instr).mnemonic();
        }
        if (config.enable_zicsr && csr_decoder.is_csr_instruction(instr)) {
            return csr_decoder.decode(instr).mnemonic();
        }
        if (config.enable_m_extension && m_decoder.is_m_instruction(instr)) {
            return m_decoder.decode(instr).mnemonic();
        }
        if (config.enable_a_extension && a_decoder.is_atomic_instruction(instr)) {
            return a_decoder.decode(instr).mnemonic();
        }
        if (config.enable_f_extension && f_decoder.is_f_instruction(instr)) {
            return f_decoder.decode(instr).mnemonic();
        }
        if (config.enable_zba && zba_decoder.is_zba_instruction(instr)) {
            return zba_decoder.decode(instr).mnemonic();
        }
        if (config.enable_zbb && zbb_decoder.is_zbb_instruction(instr)) {
            return zbb_decoder.decode(instr).mnemonic();
        }
        if (config.enable_zbs && zbs_decoder.is_zbs_instruction(instr)) {
            return zbs_decoder.decode(instr).mnemonic();
        }
        if (config.enable_zicond && zicond_decoder.is_zicond_instruction(instr)) {
            return zicond_decoder.decode(instr).mnemonic();
        }
        return i_decoder.decode(instr).mnemonic();
    }

private:
    // ========================================================================
    // Step prologue shared by both step() overloads.
    // Keeps the executors' cheap flags in sync so that toggling
    // config.allow_misaligned_data (or enable_c_extension for IALIGN)
    // between steps takes effect without a reset. NOTE: changing
    // extension *enables* or anything reflected in CSRs (MISA, FS/SD,
    // mepc mask) still requires reset().
    // Initializes the result record and samples pending interrupts
    // (interrupts are taken before fetch/execution).
    // Returns true if an interrupt was taken and the step is complete.
    // ========================================================================

    bool begin_step(CPUExecResult& result) {
        sync_executor_config();

        result.pc = pc;
        result.next_pc = pc;
        result.instruction = 0;
        result.branch_taken = false;
        result.trap = false;
        result.interrupt = false;
        result.trap_cause = 0;
        result.trap_value = 0;
        result.instr_size = 4;

        if (take_pending_interrupt(result)) {
            pc = result.next_pc;
            bump_cycle();
            // No instruction was retired: minstret is not incremented.
            return true;
        }
        return false;
    }

    // ========================================================================
    // Decode & execute an already-fetched instruction, then finish the step.
    // ========================================================================

    void execute_fetched(uint32_t instr, bool is_32bit, Bus& bus,
                         CPUExecResult& result) {
        if (!is_32bit) {
            result.instr_size = 2;
            if (!config.enable_c_extension) {
                result.mnemonic = "ILLEGAL";
                take_exception(result, exception::ILLEGAL_INSTRUCTION,
                               instr & 0xFFFFu, "Illegal instruction (C disabled)");
                finish_step(result);
                return;
            }
            execute_compressed(static_cast<uint16_t>(instr), bus, result);
        } else {
            execute_32bit(instr, bus, result);
        }

        finish_step(result);
    }

    // ========================================================================
    // Configuration propagation to the sub-executors
    // ========================================================================
    
    void sync_executor_config() {
        i_decoder.s_mode_enabled = config.enable_s_mode;
        i_executor.c_ext_enabled = config.enable_c_extension;
        i_executor.allow_misaligned = config.allow_misaligned_data;
        i_executor.priv = csrs.get_privilege();
        c_executor.allow_misaligned = config.allow_misaligned_data;
        f_executor.allow_misaligned = config.allow_misaligned_data;
        fc_executor.allow_misaligned = config.allow_misaligned_data;
    }
    
    // ========================================================================
    // Compressed dispatch (C and, when F+C, the compressed FP subset)
    // ========================================================================
    
    void execute_compressed(uint16_t c_instr, Bus& bus, CPUExecResult& result) {
        if (config.enable_f_extension &&
            fc_decoder.is_compressed_float(c_instr)) {
            auto decoded = fc_decoder.decode(c_instr);
            result.mnemonic = decoded.mnemonic();
            auto exec = fc_executor.execute(decoded, regs, fregs, bus);
            result.next_pc = pc + 2;
            if (!exec.valid) {
                take_exception(result, exec.trap_cause, exec.trap_value,
                               exec.error);
            }
            return;
        }
        
        auto decoded = c_decoder.decode(c_instr);
        result.mnemonic = decoded.mnemonic();
        auto exec = c_executor.execute(decoded, regs, bus, pc);
        result.next_pc = exec.next_pc;
        result.branch_taken = exec.branch_taken;
        if (exec.trap) {
            take_exception(result, exec.trap_cause, exec.trap_value,
                           exec.trap_info);
        }
    }
    
    // ========================================================================
    // 32-bit dispatch.
    // The decoders are exact (no aliasing between extensions), so the
    // order below only determines which module claims its own opcodes;
    // disabled extensions simply fall through and end up ILLEGAL.
    // ========================================================================
    
    void execute_32bit(uint32_t instr, Bus& bus, CPUExecResult& result) {
        result.next_pc = pc + 4;
        
        // Zifencei (FENCE.I)
        if (config.enable_zifencei &&
            zifencei_decoder.is_zifencei_instruction(instr)) {
            auto decoded = zifencei_decoder.decode(instr);
            result.mnemonic = decoded.mnemonic();
            auto exec = zifencei_executor.execute(decoded);
            if (!exec.valid) {
                take_exception(result, exception::ILLEGAL_INSTRUCTION, instr,
                               exec.error);
            }
            return;
        }
        
        // Zicsr (CSRRW/CSRRS/CSRRC and immediate forms)
        if (config.enable_zicsr && csr_decoder.is_csr_instruction(instr)) {
            auto decoded = csr_decoder.decode(instr);
            // SATP access from S mode is illegal when TVM=1.
            if (decoded.csr == zicsr::csr_addr::SATP &&
                config.enable_s_mode &&
                csrs.get_privilege() == PrivilegeLevel::SUPERVISOR &&
                (csrs.get(zicsr::csr_addr::MSTATUS) & zicsr::CSRFile::MSTATUS_TVM)) {
                take_exception(result, exception::ILLEGAL_INSTRUCTION, instr,
                               "SATP access blocked by TVM");
                return;
            }
            result.mnemonic = decoded.mnemonic();
            auto exec = csr_executor.execute(decoded, regs, csrs);
            if (!exec.valid) {
                take_exception(result, exec.exception_cause, exec.trap_value,
                               exec.error);
            }
            return;
        }
        
        // M extension
        if (config.enable_m_extension && m_decoder.is_m_instruction(instr)) {
            auto decoded = m_decoder.decode(instr);
            result.mnemonic = decoded.mnemonic();
            auto exec = m_executor.execute(decoded, regs);
            if (!exec.valid) {
                take_exception(result, exception::ILLEGAL_INSTRUCTION, instr,
                               exec.error);
            }
            return;
        }
        
        // A extension
        if (config.enable_a_extension && a_decoder.is_atomic_instruction(instr)) {
            auto decoded = a_decoder.decode(instr);
            result.mnemonic = decoded.mnemonic();
            auto exec = a_executor.execute(decoded, regs, bus, reservation);
            if (!exec.valid) {
                take_exception(result, exec.trap_cause, exec.trap_value,
                               exec.error);
            }
            return;
        }
        
        // F extension (FLW/FSW, OP-FP, fused multiply-add)
        if (config.enable_f_extension && f_decoder.is_f_instruction(instr)) {
            auto decoded = f_decoder.decode(instr);
            result.mnemonic = decoded.mnemonic();
            
            // fcsr in the CSR file is the single source of truth: build the
            // executor's view before, write back after.
            rv32f::FCSR fcsr;
            fcsr.write(csrs.get(zicsr::csr_addr::FCSR));
            auto exec = f_executor.execute(decoded, regs, fregs, bus, fcsr);
            csrs.set(zicsr::csr_addr::FCSR, fcsr.read() & 0xFF);
            
            if (!exec.valid) {
                take_exception(result, exec.trap_cause, exec.trap_value,
                               exec.error);
            }
            return;
        }
        
        // Zba / Zbb / Zbs / Zicond
        if (config.enable_zba && zba_decoder.is_zba_instruction(instr)) {
            auto decoded = zba_decoder.decode(instr);
            result.mnemonic = decoded.mnemonic();
            auto exec = zba_executor.execute(decoded, regs);
            if (!exec.valid) {
                take_exception(result, exception::ILLEGAL_INSTRUCTION, instr,
                               exec.error);
            }
            return;
        }
        if (config.enable_zbb && zbb_decoder.is_zbb_instruction(instr)) {
            auto decoded = zbb_decoder.decode(instr);
            result.mnemonic = decoded.mnemonic();
            auto exec = zbb_executor.execute(decoded, regs);
            if (!exec.valid) {
                take_exception(result, exception::ILLEGAL_INSTRUCTION, instr,
                               exec.error);
            }
            return;
        }
        if (config.enable_zbs && zbs_decoder.is_zbs_instruction(instr)) {
            auto decoded = zbs_decoder.decode(instr);
            result.mnemonic = decoded.mnemonic();
            auto exec = zbs_executor.execute(decoded, regs);
            if (!exec.valid) {
                take_exception(result, exception::ILLEGAL_INSTRUCTION, instr,
                               exec.error);
            }
            return;
        }
        if (config.enable_zicond && zicond_decoder.is_zicond_instruction(instr)) {
            auto decoded = zicond_decoder.decode(instr);
            result.mnemonic = decoded.mnemonic();
            auto exec = zicond_executor.execute(decoded, regs);
            if (!exec.valid) {
                take_exception(result, exception::ILLEGAL_INSTRUCTION, instr,
                               exec.error);
            }
            return;
        }
        
        // Base RV32I (incl. ECALL/EBREAK/MRET/SRET/SFENCE.VMA/WFI)
        auto decoded = i_decoder.decode(instr);
        result.mnemonic = decoded.mnemonic();
        auto exec = i_executor.execute(decoded, regs, bus, pc);
        result.next_pc = exec.next_pc;
        result.branch_taken = exec.branch_taken;

        PrivilegeLevel current_priv = csrs.get_privilege();
        uint32_t mstatus = csrs.get(zicsr::csr_addr::MSTATUS);

        if (exec.mret) {
            if (current_priv != PrivilegeLevel::MACHINE) {
                take_exception(result, exception::ILLEGAL_INSTRUCTION, instr,
                               "MRET executed outside M-mode");
            } else {
                do_mret(result);
            }
            return;
        }

        if (exec.sret) {
            if (!config.enable_s_mode ||
                current_priv == PrivilegeLevel::USER ||
                (current_priv == PrivilegeLevel::SUPERVISOR &&
                 (mstatus & zicsr::CSRFile::MSTATUS_TSR))) {
                take_exception(result, exception::ILLEGAL_INSTRUCTION, instr,
                               "SRET not allowed");
            } else {
                do_sret(result);
            }
            return;
        }

        if (exec.sfence_vma) {
            if (current_priv == PrivilegeLevel::USER ||
                (current_priv == PrivilegeLevel::SUPERVISOR &&
                 (mstatus & zicsr::CSRFile::MSTATUS_TVM))) {
                take_exception(result, exception::ILLEGAL_INSTRUCTION, instr,
                               "SFENCE.VMA not allowed");
            }
            // No MMU yet: execute as NOP when allowed.
            return;
        }

        if (exec.wfi) {
            if (current_priv == PrivilegeLevel::USER ||
                (current_priv == PrivilegeLevel::SUPERVISOR &&
                 (mstatus & zicsr::CSRFile::MSTATUS_TW))) {
                take_exception(result, exception::ILLEGAL_INSTRUCTION, instr,
                               "WFI not allowed");
            }
            // Otherwise legal and behaves as NOP in this model.
            return;
        }

        if (exec.trap) {
            take_exception(result, exec.trap_cause, exec.trap_value,
                           exec.trap_info);
        }
    }
    
    // ========================================================================
    // Trap / interrupt machinery
    // ========================================================================
    
    // Record a synchronous exception in the result and update the trap CSRs.
    // next_pc is redirected to the (direct) trap vector base: synchronous
    // exceptions always target BASE, even in vectored mode.
    void take_exception(CPUExecResult& result, uint32_t cause, uint32_t tval,
                        const std::string& info) {
        result.trap = true;
        result.trap_cause = cause;
        result.trap_value = tval;
        result.trap_info = info;

        PrivilegeLevel target = delegate_target(cause, false);
        if (target == PrivilegeLevel::SUPERVISOR) {
            enter_s_trap(cause, tval, pc);
            result.next_pc = csrs.get(zicsr::csr_addr::STVEC) & ~0x3u;
        } else {
            enter_m_trap(cause, tval, pc);
            result.next_pc = csrs.get(zicsr::csr_addr::MTVEC) & ~0x3u;
        }
    }

    // Choose target privilege for an exception/interrupt based on the
    // delegation registers.  U-mode delegation (sedeleg/sideleg) is not
    // implemented.
    PrivilegeLevel delegate_target(uint32_t cause, bool is_interrupt) const {
        if (!config.enable_s_mode) return PrivilegeLevel::MACHINE;
        // Traps taken while in M-mode are never delegated to a less
        // privileged mode (medeleg only applies to traps from S/U).
        // (For interrupts this case is also filtered by
        // interrupt_globally_enabled(), which skips lower-privilege
        // targets while in M-mode.)
        if (!is_interrupt &&
            csrs.get_privilege() == PrivilegeLevel::MACHINE) {
            return PrivilegeLevel::MACHINE;
        }
        uint32_t bit = cause & 0x7FFFFFFFu;
        if (bit >= 32) return PrivilegeLevel::MACHINE;
        uint32_t deleg = csrs.get(is_interrupt ? zicsr::csr_addr::MIDELEG
                                                : zicsr::csr_addr::MEDELEG);
        if ((deleg >> bit) & 1u) return PrivilegeLevel::SUPERVISOR;
        return PrivilegeLevel::MACHINE;
    }

    bool interrupt_globally_enabled(PrivilegeLevel target) const {
        PrivilegeLevel cur = csrs.get_privilege();
        if (cur > target) return false;
        if (cur < target) return true;
        uint32_t mstatus = csrs.get(zicsr::csr_addr::MSTATUS);
        if (target == PrivilegeLevel::MACHINE)
            return (mstatus & zicsr::CSRFile::MSTATUS_MIE) != 0;
        if (target == PrivilegeLevel::SUPERVISOR)
            return (mstatus & zicsr::CSRFile::MSTATUS_SIE) != 0;
        return false;
    }

    // The A extension requires the hart's load reservation to be cleared
    // whenever a trap (exception or interrupt) is taken, so an SC in a
    // trap handler after an LR in trapped code must fail.
    void enter_m_trap(uint32_t cause, uint32_t tval, uint32_t trap_pc) {
        reservation.clear();
        uint32_t mstatus = csrs.get(zicsr::csr_addr::MSTATUS);
        bool old_mie = (mstatus & zicsr::CSRFile::MSTATUS_MIE) != 0;
        mstatus &= ~(zicsr::CSRFile::MSTATUS_MIE |
                     zicsr::CSRFile::MSTATUS_MPIE |
                     zicsr::CSRFile::MSTATUS_MPP);
        if (old_mie) mstatus |= zicsr::CSRFile::MSTATUS_MPIE;
        uint32_t mpp = static_cast<uint32_t>(csrs.get_privilege());
        if (mpp > 3) mpp = 3;
        mstatus |= (mpp << 11);
        csrs.set(zicsr::csr_addr::MSTATUS, mstatus);
        csrs.set_privilege(PrivilegeLevel::MACHINE);
        csrs.write(zicsr::csr_addr::MEPC, trap_pc);
        csrs.set(zicsr::csr_addr::MCAUSE, cause);
        csrs.set(zicsr::csr_addr::MTVAL, tval);
    }

    void enter_s_trap(uint32_t cause, uint32_t tval, uint32_t trap_pc) {
        reservation.clear();
        uint32_t mstatus = csrs.get(zicsr::csr_addr::MSTATUS);
        bool old_sie = (mstatus & zicsr::CSRFile::MSTATUS_SIE) != 0;
        mstatus &= ~(zicsr::CSRFile::MSTATUS_SIE |
                     zicsr::CSRFile::MSTATUS_SPIE |
                     zicsr::CSRFile::MSTATUS_SPP);
        if (old_sie) mstatus |= zicsr::CSRFile::MSTATUS_SPIE;
        uint32_t spp = (csrs.get_privilege() == PrivilegeLevel::SUPERVISOR) ? 1u : 0u;
        mstatus |= (spp << 8);
        csrs.set(zicsr::csr_addr::MSTATUS, mstatus);
        csrs.set_privilege(PrivilegeLevel::SUPERVISOR);
        csrs.write(zicsr::csr_addr::SEPC, trap_pc);
        csrs.set(zicsr::csr_addr::SCAUSE, cause);
        csrs.set(zicsr::csr_addr::STVAL, tval);
    }

    // Check for a pending, enabled interrupt and take it if present.
    // Standard priority order: MEI > MSI > MTI > SEI > SSI > STI.
    bool take_pending_interrupt(CPUExecResult& result) {
        uint32_t mip = csrs.get(zicsr::csr_addr::MIP);
        uint32_t mie = csrs.get(zicsr::csr_addr::MIE);
        uint32_t pending = mip & mie;
        if (!pending) return false;

        // Standard priority order within a target privilege:
        // MEI > MSI > MTI > SEI > SSI > STI. Per the priv spec, interrupts
        // targeting M-mode take priority over any interrupts targeting
        // S-mode, so scan twice: M-target interrupts first, then S-target
        // (this matters only when some M-cause interrupts are delegated).
        static const uint32_t priority[] = {11, 3, 7, 9, 1, 5};
        for (int pass = 0; pass < 2; ++pass) {
        for (uint32_t code : priority) {
            uint32_t bit = 1u << code;
            if (!(pending & bit)) continue;

            uint32_t cause = exception::INTERRUPT_BIT | code;
            PrivilegeLevel target = delegate_target(cause, true);
            if ((pass == 0) != (target == PrivilegeLevel::MACHINE)) continue;
            if (!interrupt_globally_enabled(target)) continue;

            if (target == PrivilegeLevel::SUPERVISOR) {
                enter_s_trap(cause, 0, pc);
                uint32_t stvec = csrs.get(zicsr::csr_addr::STVEC);
                uint32_t base = stvec & ~0x3u;
                result.next_pc = ((stvec & 0x3) == 1) ? (base + 4 * code) : base;
            } else {
                enter_m_trap(cause, 0, pc);
                uint32_t mtvec = csrs.get(zicsr::csr_addr::MTVEC);
                uint32_t base = mtvec & ~0x3u;
                result.next_pc = ((mtvec & 0x3) == 1) ? (base + 4 * code) : base;
            }

            result.interrupt = true;
            result.trap_cause = cause;
            result.trap_value = 0;
            result.trap_info = "Interrupt";
            result.instr_size = 0;
            return true;
        }
        }
        return false;
    }

    // MRET: restore privilege from MPP, MIE <- MPIE, MPIE <- 1, and MPP
    // is set to the least-privileged supported mode (U if U-mode is
    // implemented, else M). Clear MPRV when returning to a mode less
    // privileged than M.
    void do_mret(CPUExecResult& result) {
        uint32_t mstatus = csrs.get(zicsr::csr_addr::MSTATUS);
        uint32_t mpp = (mstatus >> 11) & 3u;
        bool mpie = (mstatus & zicsr::CSRFile::MSTATUS_MPIE) != 0;
        mstatus &= ~zicsr::CSRFile::MSTATUS_MIE;
        if (mpie) mstatus |= zicsr::CSRFile::MSTATUS_MIE;
        mstatus |= zicsr::CSRFile::MSTATUS_MPIE;
        mstatus &= ~zicsr::CSRFile::MSTATUS_MPP;
        uint32_t mpp_reset = csrs.has_u_mode() ? 0u : 3u;
        mstatus |= (mpp_reset << 11);
        if (mpp != 3) mstatus &= ~zicsr::CSRFile::MSTATUS_MPRV;
        csrs.set(zicsr::csr_addr::MSTATUS, mstatus);

        PrivilegeLevel new_priv = (mpp == 0) ? PrivilegeLevel::USER :
                                  (mpp == 1) ? PrivilegeLevel::SUPERVISOR :
                                               PrivilegeLevel::MACHINE;
        csrs.set_privilege(new_priv);
        result.next_pc = csrs.get(zicsr::csr_addr::MEPC);
        result.branch_taken = true;
    }

    // SRET: restore privilege from SPP, SIE <- SPIE, SPIE <- 1, SPP <- U.
    void do_sret(CPUExecResult& result) {
        uint32_t mstatus = csrs.get(zicsr::csr_addr::MSTATUS);
        bool spie = (mstatus & zicsr::CSRFile::MSTATUS_SPIE) != 0;
        bool spp = (mstatus & zicsr::CSRFile::MSTATUS_SPP) != 0;
        mstatus &= ~zicsr::CSRFile::MSTATUS_SIE;
        if (spie) mstatus |= zicsr::CSRFile::MSTATUS_SIE;
        mstatus |= zicsr::CSRFile::MSTATUS_SPIE;
        mstatus &= ~zicsr::CSRFile::MSTATUS_SPP;
        mstatus &= ~zicsr::CSRFile::MSTATUS_MPRV;
        csrs.set(zicsr::csr_addr::MSTATUS, mstatus);

        csrs.set_privilege(spp ? PrivilegeLevel::SUPERVISOR : PrivilegeLevel::USER);
        result.next_pc = csrs.get(zicsr::csr_addr::SEPC);
        result.branch_taken = true;
    }

    void set_mip_bit(uint32_t bit, bool level) {
        uint32_t mip = csrs.get(zicsr::csr_addr::MIP);
        if (level) mip |= bit; else mip &= ~bit;
        csrs.set(zicsr::csr_addr::MIP, mip);
    }
    
    // ========================================================================
    // Counters and PC update
    // ========================================================================
    
    void bump_cycle() {
        uint64_t cycle = (static_cast<uint64_t>(csrs.get(zicsr::csr_addr::MCYCLEH)) << 32) |
                         csrs.get(zicsr::csr_addr::MCYCLE);
        cycle++;
        csrs.set(zicsr::csr_addr::MCYCLE, static_cast<uint32_t>(cycle));
        csrs.set(zicsr::csr_addr::MCYCLEH, static_cast<uint32_t>(cycle >> 32));
        // cycle/cycleh are read-only aliases of mcycle/mcycleh — no
        // separate bookkeeping needed.
    }
    
    void bump_instret() {
        uint64_t instret = (static_cast<uint64_t>(csrs.get(zicsr::csr_addr::MINSTRETH)) << 32) |
                           csrs.get(zicsr::csr_addr::MINSTRET);
        instret++;
        csrs.set(zicsr::csr_addr::MINSTRET, static_cast<uint32_t>(instret));
        csrs.set(zicsr::csr_addr::MINSTRETH, static_cast<uint32_t>(instret >> 32));
    }
    
    void finish_step(CPUExecResult& result) {
        pc = result.next_pc;
        bump_cycle();
        // Instructions that trap do not retire; minstret counts only
        // retired instructions.
        if (!result.trap) {
            bump_instret();
        }
    }
};

// ============================================================================
// Complete System Model (CPU + Memory)
// ============================================================================

class System {
public:
    CPU cpu;
    SimpleMemory memory;
    
    System(size_t mem_size = SimpleMemory::DEFAULT_SIZE, 
           const CPUConfig& cpu_config = CPUConfig())
        : cpu(cpu_config), memory(mem_size) {}
    
    // Step one instruction
    CPUExecResult step() {
        return cpu.step(memory);
    }
    
    // Run for up to max_instructions (see CPU::run)
    uint64_t run(uint64_t max_instructions = UINT64_MAX,
                 bool stop_on_trap = false) {
        return cpu.run(memory, max_instructions, stop_on_trap);
    }
    
    // Load program into memory
    void load_program(uint32_t addr, const uint8_t* data, size_t len) {
        memory.load(addr, data, len);
    }
    
    // Load program from instruction array
    void load_program(uint32_t addr, const uint32_t* instructions, size_t count) {
        for (size_t i = 0; i < count; i++) {
            memory.write32(addr + i * 4, instructions[i]);
        }
    }
    
    // Reset system
    void reset() {
        cpu.reset();
        memory.clear();
    }
    
    // Disassemble at address
    std::string disasm(uint32_t addr) {
        return cpu.disasm(memory, addr);
    }
};

} // namespace riscv

#endif // RISCV_CPU_HPP
