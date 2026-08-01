/*******************************************************************************
 * RISC-V Zicsr Extension Model
 * 
 * Control and Status Register (CSR) instructions and CSR file implementation.
 * Based on RISC-V Privileged Architecture v1.12 (ratified).
 * 
 * NOTE: N extension (user-level interrupts) has been removed as it was
 * deprecated and removed from the ratified specification.
 ******************************************************************************/

#ifndef RISCV_ZICSR_HPP
#define RISCV_ZICSR_HPP

#include "riscv_common.hpp"
#include <unordered_map>

namespace riscv {
namespace zicsr {

// ============================================================================
// CSR Addresses
// ============================================================================

namespace csr_addr {
    // =========================================================================
    // User-level Floating-Point CSRs (F extension)
    // =========================================================================
    constexpr uint16_t FFLAGS      = 0x001;  // URW - Floating-point accrued exceptions
    constexpr uint16_t FRM         = 0x002;  // URW - Floating-point dynamic rounding mode
    constexpr uint16_t FCSR        = 0x003;  // URW - Floating-point control and status
    
    // =========================================================================
    // User Counter/Timers (read-only shadows of machine counters)
    // =========================================================================
    constexpr uint16_t CYCLE       = 0xC00;  // URO - Cycle counter
    constexpr uint16_t TIME        = 0xC01;  // URO - Timer
    constexpr uint16_t INSTRET     = 0xC02;  // URO - Instructions retired
    constexpr uint16_t CYCLEH      = 0xC80;  // URO - Upper 32 bits of cycle
    constexpr uint16_t TIMEH       = 0xC81;  // URO - Upper 32 bits of time
    constexpr uint16_t INSTRETH    = 0xC82;  // URO - Upper 32 bits of instret
    
    // =========================================================================
    // Supervisor-level CSRs
    // =========================================================================
    
    // Supervisor Trap Setup
    constexpr uint16_t SSTATUS     = 0x100;  // SRW - Supervisor status
    constexpr uint16_t SIE         = 0x104;  // SRW - Supervisor interrupt enable
    constexpr uint16_t STVEC       = 0x105;  // SRW - Supervisor trap vector
    constexpr uint16_t SCOUNTEREN  = 0x106;  // SRW - Supervisor counter enable
    
    // Supervisor Trap Handling
    constexpr uint16_t SSCRATCH    = 0x140;  // SRW - Supervisor scratch
    constexpr uint16_t SEPC        = 0x141;  // SRW - Supervisor exception PC
    constexpr uint16_t SCAUSE      = 0x142;  // SRW - Supervisor cause
    constexpr uint16_t STVAL       = 0x143;  // SRW - Supervisor trap value
    constexpr uint16_t SIP         = 0x144;  // SRW - Supervisor interrupt pending
    
    // Supervisor Protection and Translation
    constexpr uint16_t SATP        = 0x180;  // SRW - Supervisor address translation
    
    // =========================================================================
    // Machine-level CSRs
    // =========================================================================
    
    // Machine Information Registers (read-only)
    constexpr uint16_t MVENDORID   = 0xF11;  // MRO - Vendor ID
    constexpr uint16_t MARCHID     = 0xF12;  // MRO - Architecture ID
    constexpr uint16_t MIMPID      = 0xF13;  // MRO - Implementation ID
    constexpr uint16_t MHARTID     = 0xF14;  // MRO - Hardware thread ID
    constexpr uint16_t MCONFIGPTR  = 0xF15;  // MRO - Config data structure ptr
    
    // Machine Trap Setup
    constexpr uint16_t MSTATUS     = 0x300;  // MRW - Machine status
    constexpr uint16_t MISA        = 0x301;  // MRW - Machine ISA
    constexpr uint16_t MEDELEG     = 0x302;  // MRW - Machine exception delegation
    constexpr uint16_t MIDELEG     = 0x303;  // MRW - Machine interrupt delegation
    constexpr uint16_t MIE         = 0x304;  // MRW - Machine interrupt enable
    constexpr uint16_t MTVEC       = 0x305;  // MRW - Machine trap vector
    constexpr uint16_t MCOUNTEREN  = 0x306;  // MRW - Machine counter enable
    constexpr uint16_t MSTATUSH    = 0x310;  // MRW - Upper machine status (RV32)
    constexpr uint16_t MENVCFG     = 0x30A;  // MRW - Machine environment config
    constexpr uint16_t MENVCFGH    = 0x31A;  // MRW - Upper menvcfg (RV32)
    
    // Machine Trap Handling
    constexpr uint16_t MSCRATCH    = 0x340;  // MRW - Machine scratch
    constexpr uint16_t MEPC        = 0x341;  // MRW - Machine exception PC
    constexpr uint16_t MCAUSE      = 0x342;  // MRW - Machine cause
    constexpr uint16_t MTVAL       = 0x343;  // MRW - Machine trap value
    constexpr uint16_t MIP         = 0x344;  // MRW - Machine interrupt pending
    
    // Machine Counter/Timers
    constexpr uint16_t MCYCLE      = 0xB00;  // MRW - Machine cycle counter
    constexpr uint16_t MINSTRET    = 0xB02;  // MRW - Machine instructions retired
    constexpr uint16_t MCYCLEH     = 0xB80;  // MRW - Upper 32 bits of mcycle
    constexpr uint16_t MINSTRETH   = 0xB82;  // MRW - Upper 32 bits of minstret
}

// ============================================================================
// CSR Access Permissions
// ============================================================================

struct CSRPermissions {
    bool readable;
    bool writable;
    PrivilegeLevel min_privilege;
    
    static CSRPermissions from_address(uint16_t addr) {
        CSRPermissions perm;
        // Bits [11:10] encode read/write access
        // 00, 01, 10 = read/write, 11 = read-only
        uint8_t rw_bits = (addr >> 10) & 0x3;
        perm.writable = (rw_bits != 0x3);
        perm.readable = true;
        
        // Bits [9:8] encode minimum privilege level
        perm.min_privilege = static_cast<PrivilegeLevel>((addr >> 8) & 0x3);
        
        return perm;
    }
};

// ============================================================================
// CSR File
// ============================================================================

/**
 * CSR file for an RV32 hart with optional Supervisor and User modes.
 *
 * - Only the CSRs of the implemented configuration exist; accessing any
 *   other CSR address must raise an illegal-instruction exception (the
 *   executor checks exists()).
 * - WARL fields are legalized on write (mstatus, mtvec, mepc, misa, mie,
 *   mip, fcsr). set()/get() are raw backdoors for initialization and
 *   state load/save that bypass legalization.
 * - fflags and frm are architectural aliases into fcsr: a single fcsr
 *   storage cell backs all three addresses.
 * - cycle/cycleh/instret/instreth are read-only shadows aliased onto
 *   mcycle/mcycleh/minstret/minstreth.
 * - S-mode CSRs, medeleg/mideleg and mcounteren/scounteren exist when the
 *   corresponding modes are enabled; time/timeh are read-only aliases of
 *   mcycle/mcycleh.
 */
class CSRFile {
public:
    using ReadCallback = std::function<uint32_t(uint16_t)>;
    using WriteCallback = std::function<void(uint16_t, uint32_t)>;
    
    // mstatus bit positions
    static constexpr uint32_t MSTATUS_SIE  = 1u << 1;
    static constexpr uint32_t MSTATUS_MIE  = 1u << 3;
    static constexpr uint32_t MSTATUS_SPIE = 1u << 5;
    static constexpr uint32_t MSTATUS_MPIE = 1u << 7;
    static constexpr uint32_t MSTATUS_SPP  = 1u << 8;
    static constexpr uint32_t MSTATUS_MPP  = 3u << 11;
    static constexpr uint32_t MSTATUS_FS   = 3u << 13;   // hardwired to Dirty (11) when F
    static constexpr uint32_t MSTATUS_MPRV = 1u << 17;
    static constexpr uint32_t MSTATUS_SUM  = 1u << 18;
    static constexpr uint32_t MSTATUS_MXR  = 1u << 19;
    static constexpr uint32_t MSTATUS_TVM  = 1u << 20;
    static constexpr uint32_t MSTATUS_TW   = 1u << 21;
    static constexpr uint32_t MSTATUS_TSR  = 1u << 22;
    static constexpr uint32_t MSTATUS_SD   = 1u << 31;   // reflects FS == 11

    // mie/mip bit positions
    static constexpr uint32_t MI_SSI = 1u << 1;
    static constexpr uint32_t MI_MSI = 1u << 3;
    static constexpr uint32_t MI_STI = 1u << 5;
    static constexpr uint32_t MI_MTI = 1u << 7;
    static constexpr uint32_t MI_SEI = 1u << 9;
    static constexpr uint32_t MI_MEI = 1u << 11;

    static constexpr uint32_t MI_MASK = MI_SSI | MI_MSI | MI_STI | MI_MTI | MI_SEI | MI_MEI;

    uint32_t mip_writable_mask() const {
        // MSIP is writable in every implementation; SSIP only when S-mode exists.
        return s_mode_enabled ? (MI_MSI | MI_SSI) : MI_MSI;
    }

private:
    std::unordered_map<uint16_t, uint32_t> csrs;
    PrivilegeLevel current_privilege = PrivilegeLevel::MACHINE;
    ReadCallback read_callback;
    WriteCallback write_callback;
    bool f_extension = true;        // fflags/frm/fcsr exist and FS/SD are set
    bool s_mode_enabled = false;      // true if Supervisor mode is implemented
    bool u_mode_enabled = false;      // true if User mode is implemented
    uint32_t mepc_mask = ~1u;       // ~1 with C (IALIGN=16), ~3 without

    // Bits that are visible in sstatus (read) and writable through sstatus.
    static constexpr uint32_t SSTATUS_READ_MASK =
        MSTATUS_SIE | MSTATUS_SPIE | MSTATUS_SPP |
        MSTATUS_MXR | MSTATUS_SUM | MSTATUS_FS | MSTATUS_SD;
    static constexpr uint32_t SSTATUS_WRITE_MASK =
        MSTATUS_SIE | MSTATUS_SPIE | MSTATUS_SPP |
        MSTATUS_MXR | MSTATUS_SUM;

    uint32_t mstatus_hardwired() const {
        // FS is hardwired to Dirty (11) and SD to 1 when the F extension is
        // present (a legal WARL choice for an implementation that does not
        // track FP context state).
        uint32_t hw = 0;
        if (f_extension) hw |= MSTATUS_FS | MSTATUS_SD;
        return hw;
    }

    uint32_t mstatus_legal_mask() const {
        uint32_t mask = MSTATUS_SIE | MSTATUS_SPIE | MSTATUS_SPP |
                        MSTATUS_MIE | MSTATUS_MPIE | MSTATUS_MPP |
                        MSTATUS_MPRV | MSTATUS_SUM | MSTATUS_MXR |
                        MSTATUS_TVM | MSTATUS_TW | MSTATUS_TSR;
        if (!s_mode_enabled) {
            mask &= ~(MSTATUS_SIE | MSTATUS_SPIE | MSTATUS_SPP |
                      MSTATUS_SUM | MSTATUS_TVM | MSTATUS_TSR);
            mask &= ~MSTATUS_TW;  // no S/U mode for WFI timeout to affect
        }
        if (!u_mode_enabled) {
            mask &= ~(MSTATUS_MPRV | MSTATUS_SUM | MSTATUS_MXR | MSTATUS_TW);
        }
        return mask;
    }

    uint32_t legalize_mpp(uint32_t mpp) const {
        if (mpp == 2) mpp = 3;              // reserved -> Machine
        if (!u_mode_enabled && mpp == 0) mpp = 3;
        if (!s_mode_enabled && mpp == 1) mpp = 3;
        return mpp;
    }
    
    void init_storage() {
        csrs.clear();
        
        // Floating-point state (single fcsr cell backs fflags/frm/fcsr)
        csrs[csr_addr::FCSR] = 0;
        
        // Machine information registers (read-only by address encoding)
        csrs[csr_addr::MISA] = 0x40000100;      // set by the CPU via set_misa()
        csrs[csr_addr::MVENDORID] = 0;
        csrs[csr_addr::MARCHID] = 0;
        csrs[csr_addr::MIMPID] = 0;
        csrs[csr_addr::MHARTID] = 0;
        csrs[csr_addr::MCONFIGPTR] = 0;         // priv >= 1.12, read-only zero
        
        // Priv-spec >= 1.12 mandatory RV32 CSRs, implemented as WARL
        // all-zero (little-endian only, no environment-config features).
        csrs[csr_addr::MSTATUSH] = 0;
        csrs[csr_addr::MENVCFG] = 0;
        csrs[csr_addr::MENVCFGH] = 0;
        
        // Machine trap setup / handling
        csrs[csr_addr::MSTATUS] = mstatus_hardwired() | MSTATUS_MPP; // MPP = M
        csrs[csr_addr::MTVEC] = 0;
        csrs[csr_addr::MIE] = 0;
        csrs[csr_addr::MEPC] = 0;
        csrs[csr_addr::MCAUSE] = 0;
        csrs[csr_addr::MTVAL] = 0;
        csrs[csr_addr::MSCRATCH] = 0;
        csrs[csr_addr::MIP] = 0;

        // Machine delegation and counter enables
        if (s_mode_enabled || u_mode_enabled) {
            csrs[csr_addr::MEDELEG] = 0;
            csrs[csr_addr::MIDELEG] = 0;
        }
        csrs[csr_addr::MCOUNTEREN] = 0;

        // Supervisor CSRs
        if (s_mode_enabled) {
            csrs[csr_addr::SSTATUS] = 0;
            csrs[csr_addr::SIE] = 0;
            csrs[csr_addr::STVEC] = 0;
            csrs[csr_addr::SCOUNTEREN] = 0;
            csrs[csr_addr::SSCRATCH] = 0;
            csrs[csr_addr::SEPC] = 0;
            csrs[csr_addr::SCAUSE] = 0;
            csrs[csr_addr::STVAL] = 0;
            csrs[csr_addr::SIP] = 0;
            csrs[csr_addr::SATP] = 0;
        }

        // Machine counters
        csrs[csr_addr::MCYCLE] = 0;
        csrs[csr_addr::MCYCLEH] = 0;
        csrs[csr_addr::MINSTRET] = 0;
        csrs[csr_addr::MINSTRETH] = 0;
    }
    
public:
    CSRFile() {
        init_storage();
    }
    
    // Configuration hooks used by the integrating CPU --------------------
    
    void set_f_extension(bool enabled) {
        f_extension = enabled;
        // Re-legalize mstatus hardwired fields
        uint32_t v = csrs[csr_addr::MSTATUS];
        v &= mstatus_legal_mask();
        csrs[csr_addr::MSTATUS] = v | mstatus_hardwired();
    }

    bool has_f_extension() const { return f_extension; }

    void set_s_mode(bool enabled) { s_mode_enabled = enabled; }
    bool has_s_mode() const { return s_mode_enabled; }

    void set_u_mode(bool enabled) { u_mode_enabled = enabled; }
    bool has_u_mode() const { return u_mode_enabled; }
    
    // IALIGN=16 (C enabled) -> mask ~1; IALIGN=32 -> mask ~3
    void set_mepc_mask(uint32_t mask) { mepc_mask = mask; }
    
    // Raw MISA update (misa itself is read-only through CSR instructions)
    void set_misa(uint32_t value) { csrs[csr_addr::MISA] = value; }
    
    void set_privilege(PrivilegeLevel priv) { current_privilege = priv; }
    PrivilegeLevel get_privilege() const { return current_privilege; }
    
    void set_read_callback(ReadCallback cb) { read_callback = cb; }
    void set_write_callback(WriteCallback cb) { write_callback = cb; }
    
    bool exists(uint16_t addr) const {
        switch (addr) {
            // fflags/frm alias into fcsr; all three exist only with F
            case csr_addr::FFLAGS:
            case csr_addr::FRM:
            case csr_addr::FCSR:
                return f_extension;
            // Read-only counter shadows alias the machine counters
            case csr_addr::CYCLE:
            case csr_addr::CYCLEH:
            case csr_addr::TIME:
            case csr_addr::TIMEH:
            case csr_addr::INSTRET:
            case csr_addr::INSTRETH:
                return true;
            default:
                return csrs.find(addr) != csrs.end();
        }
    }

    bool can_read(uint16_t addr) const {
        auto perm = CSRPermissions::from_address(addr);
        if (static_cast<uint8_t>(current_privilege) <
            static_cast<uint8_t>(perm.min_privilege)) {
            return false;
        }
        if (is_counter_csr(addr)) return counter_readable(addr);
        return true;
    }

    bool can_write(uint16_t addr) const {
        auto perm = CSRPermissions::from_address(addr);
        if (!perm.writable) return false;
        return static_cast<uint8_t>(current_privilege) >=
               static_cast<uint8_t>(perm.min_privilege);
    }

    // Counter CSR access helpers -----------------------------------------
    static bool is_counter_csr(uint16_t addr) {
        return (addr == csr_addr::CYCLE)   || (addr == csr_addr::CYCLEH) ||
               (addr == csr_addr::TIME)    || (addr == csr_addr::TIMEH)  ||
               (addr == csr_addr::INSTRET) || (addr == csr_addr::INSTRETH);
    }

    static uint8_t counter_index(uint16_t addr) { return static_cast<uint8_t>(addr & 0x1F); }

    bool counter_readable(uint16_t addr) const {
        if (current_privilege == PrivilegeLevel::MACHINE) return true;

        auto it = csrs.find(csr_addr::MCOUNTEREN);
        uint32_t mcen = (it != csrs.end()) ? it->second : 0;
        uint8_t idx = counter_index(addr);
        if (((mcen >> idx) & 1u) == 0u) return false;

        if (current_privilege == PrivilegeLevel::SUPERVISOR) return true;

        // USER: scounteren bit must also be set if S-mode is implemented.
        if (!s_mode_enabled) return false;
        auto its = csrs.find(csr_addr::SCOUNTEREN);
        uint32_t scen = (its != csrs.end()) ? its->second : 0;
        return ((scen >> idx) & 1u) != 0u;
    }
    
    /**
     * Architectural CSR read (aliasing applied). The caller must have
     * verified exists() and can_read(); reading a non-existent CSR
     * returns 0 here but MUST be turned into an illegal-instruction
     * exception by the executor.
     *
     * If a read callback is installed, it observes/overrides the read of
     * the BACKING register (aliases are resolved first, so fflags/frm
     * invoke the callback with FCSR, cycle with MCYCLE, etc.). Aliasing
     * therefore keeps working with a callback installed.
     */
    uint32_t read(uint16_t addr) const {
        switch (addr) {
            case csr_addr::FFLAGS:  return read_raw(csr_addr::FCSR) & 0x1F;
            case csr_addr::FRM:     return (read_raw(csr_addr::FCSR) >> 5) & 0x7;
            case csr_addr::CYCLE:   return read_raw(csr_addr::MCYCLE);
            case csr_addr::CYCLEH:  return read_raw(csr_addr::MCYCLEH);
            case csr_addr::TIME:    return read_raw(csr_addr::MCYCLE);
            case csr_addr::TIMEH:   return read_raw(csr_addr::MCYCLEH);
            case csr_addr::INSTRET: return read_raw(csr_addr::MINSTRET);
            case csr_addr::INSTRETH:return read_raw(csr_addr::MINSTRETH);
            case csr_addr::SSTATUS: return read_raw(csr_addr::MSTATUS) & SSTATUS_READ_MASK;
            case csr_addr::SIE:     return read_raw(csr_addr::MIE) & get(csr_addr::MIDELEG);
            case csr_addr::SIP:     return read_raw(csr_addr::MIP) & get(csr_addr::MIDELEG);
            default:                return read_raw(addr);
        }
    }
    
private:
    // Backing-register read: consults the read callback (if any), else
    // the storage map.
    uint32_t read_raw(uint16_t addr) const {
        if (read_callback) return read_callback(addr);
        return get(addr);
    }
    
public:
    /**
     * Architectural CSR write with WARL legalization and aliasing.
     * The optional write callback is an OBSERVER of the architectural
     * write request: it receives the originally addressed CSR and the
     * pre-legalization value (useful for tracing exactly what software
     * wrote); the stored value is the legalized one.
     */
    void write(uint16_t addr, uint32_t value) {
        if (write_callback) write_callback(addr, value);
        switch (addr) {
            case csr_addr::FFLAGS: {
                uint32_t fcsr = get(csr_addr::FCSR);
                csrs[csr_addr::FCSR] = (fcsr & ~0x1Fu) | (value & 0x1F);
                break;
            }
            case csr_addr::FRM: {
                uint32_t fcsr = get(csr_addr::FCSR);
                csrs[csr_addr::FCSR] = (fcsr & ~0xE0u) | ((value & 0x7) << 5);
                break;
            }
            case csr_addr::FCSR:
                csrs[csr_addr::FCSR] = value & 0xFF;
                break;
            
            case csr_addr::MSTATUS: {
                uint32_t v = value & mstatus_legal_mask();
                uint32_t mpp = (v >> 11) & 3u;
                mpp = legalize_mpp(mpp);
                v &= ~MSTATUS_MPP;
                v |= (mpp << 11);
                csrs[csr_addr::MSTATUS] = v | mstatus_hardwired();
                break;
            }
            case csr_addr::MTVEC: {
                // WARL mode field: only Direct (0) and Vectored (1) are
                // supported; reserved modes are legalized to Direct.
                uint32_t mode = value & 0x3;
                if (mode > 1) mode = 0;
                csrs[csr_addr::MTVEC] = (value & ~0x3u) | mode;
                break;
            }
            case csr_addr::MEPC:
                csrs[csr_addr::MEPC] = value & mepc_mask;
                break;
            case csr_addr::MISA:
                // WARL: this implementation ignores writes to misa.
                break;
            case csr_addr::MSTATUSH:
            case csr_addr::MENVCFG:
            case csr_addr::MENVCFGH:
                // WARL all-zero (little-endian, no envcfg features).
                break;
            case csr_addr::MIE:
                csrs[csr_addr::MIE] = value & MI_MASK;
                break;
            case csr_addr::MIP: {
                uint32_t mip = get(csr_addr::MIP);
                uint32_t wmask = mip_writable_mask();
                csrs[csr_addr::MIP] = (mip & ~wmask) | (value & wmask);
                break;
            }
            case csr_addr::MEDELEG:
                // Bit 11 (ECALL-from-M) is read-only zero: M-mode traps
                // can never be delegated.
                csrs[csr_addr::MEDELEG] = value & 0xF7FFu;
                break;
            case csr_addr::MIDELEG:
                csrs[csr_addr::MIDELEG] = value & MI_MASK;
                break;
            case csr_addr::MCOUNTEREN:
                csrs[csr_addr::MCOUNTEREN] = value;
                break;

            // Supervisor aliases and CSRs
            case csr_addr::SSTATUS: {
                uint32_t mstatus = get(csr_addr::MSTATUS);
                uint32_t v = (mstatus & ~SSTATUS_WRITE_MASK) | (value & SSTATUS_WRITE_MASK);
                v &= ~MSTATUS_SD;
                csrs[csr_addr::MSTATUS] = (v & mstatus_legal_mask()) | mstatus_hardwired();
                break;
            }
            case csr_addr::SIE: {
                if (!s_mode_enabled) break;
                uint32_t deleg = get(csr_addr::MIDELEG);
                uint32_t mie = get(csr_addr::MIE);
                csrs[csr_addr::MIE] = (mie & ~deleg) | (value & deleg & MI_MASK);
                break;
            }
            case csr_addr::SIP: {
                if (!s_mode_enabled) break;
                uint32_t deleg = get(csr_addr::MIDELEG);
                uint32_t mip = get(csr_addr::MIP);
                uint32_t wmask = mip_writable_mask();
                csrs[csr_addr::MIP] = (mip & ~deleg) | (value & deleg & wmask);
                break;
            }
            case csr_addr::STVEC: {
                uint32_t mode = value & 0x3;
                if (mode > 1) mode = 0;
                csrs[csr_addr::STVEC] = (value & ~0x3u) | mode;
                break;
            }
            case csr_addr::SEPC:
                csrs[csr_addr::SEPC] = value & mepc_mask;
                break;
            case csr_addr::SCAUSE:
                csrs[csr_addr::SCAUSE] = value;
                break;
            case csr_addr::STVAL:
                csrs[csr_addr::STVAL] = value;
                break;
            case csr_addr::SSCRATCH:
                csrs[csr_addr::SSCRATCH] = value;
                break;
            case csr_addr::SATP: {
                uint32_t mode = value >> 31;
                if (mode > 1) mode = 0;     // Bare or Sv32 only
                csrs[csr_addr::SATP] = (value & 0x7FFFFFFFu) | (mode << 31);
                break;
            }
            case csr_addr::SCOUNTEREN:
                csrs[csr_addr::SCOUNTEREN] = value;
                break;


            default:
                csrs[addr] = value;
                break;
        }
    }
    
    // Raw backdoor access for initialization and state load/save.
    // Bypasses legalization and aliasing (except that fflags/frm have no
    // storage of their own).
    void set(uint16_t addr, uint32_t value) { csrs[addr] = value; }
    uint32_t get(uint16_t addr) const {
        auto it = csrs.find(addr);
        return (it != csrs.end()) ? it->second : 0;
    }
    
    void reset() {
        init_storage();
        current_privilege = PrivilegeLevel::MACHINE;
    }
};

// ============================================================================
// CSR Instruction Types
// ============================================================================

enum class CSRInstrType {
    CSRRW,   // CSR Read/Write
    CSRRS,   // CSR Read and Set Bits
    CSRRC,   // CSR Read and Clear Bits
    CSRRWI,  // CSR Read/Write Immediate
    CSRRSI,  // CSR Read and Set Bits Immediate
    CSRRCI,  // CSR Read and Clear Bits Immediate
    ILLEGAL
};

// ============================================================================
// Decoded CSR Instruction
// ============================================================================

struct DecodedCSRInstr {
    CSRInstrType type;
    uint8_t rd;
    uint8_t rs1;
    uint16_t csr;
    uint8_t funct3;
    uint32_t raw;
    
    uint8_t uimm() const { return rs1; }
    std::string mnemonic() const;
};

// ============================================================================
// CSR Execution Result
// ============================================================================

struct CSRExecResult {
    bool valid;
    uint32_t old_csr_value;
    uint32_t new_csr_value;
    bool csr_read;
    bool csr_written;
    bool exception;
    uint32_t exception_cause;
    uint32_t trap_value;    // Value for mtval (the faulting instruction)
    std::string error;
};

// ============================================================================
// Opcodes and Function Codes
// ============================================================================

namespace opcode {
    constexpr uint8_t SYSTEM = 0b1110011;
}

namespace funct3 {
    constexpr uint8_t CSRRW  = 0b001;
    constexpr uint8_t CSRRS  = 0b010;
    constexpr uint8_t CSRRC  = 0b011;
    constexpr uint8_t CSRRWI = 0b101;
    constexpr uint8_t CSRRSI = 0b110;
    constexpr uint8_t CSRRCI = 0b111;
}

// ============================================================================
// CSR Decoder
// ============================================================================

class Decoder {
public:
    DecodedCSRInstr decode(uint32_t instr) const {
        DecodedCSRInstr d;
        d.raw = instr;
        d.rd = bits(instr, 11, 7);
        d.rs1 = bits(instr, 19, 15);
        d.csr = bits(instr, 31, 20);
        d.funct3 = bits(instr, 14, 12);
        d.type = CSRInstrType::ILLEGAL;
        
        uint8_t op = bits(instr, 6, 0);
        if (op != opcode::SYSTEM) return d;
        
        switch (d.funct3) {
            case funct3::CSRRW:  d.type = CSRInstrType::CSRRW;  break;
            case funct3::CSRRS:  d.type = CSRInstrType::CSRRS;  break;
            case funct3::CSRRC:  d.type = CSRInstrType::CSRRC;  break;
            case funct3::CSRRWI: d.type = CSRInstrType::CSRRWI; break;
            case funct3::CSRRSI: d.type = CSRInstrType::CSRRSI; break;
            case funct3::CSRRCI: d.type = CSRInstrType::CSRRCI; break;
            default: break;
        }
        return d;
    }
    
    bool is_csr_instruction(uint32_t instr) const {
        uint8_t op = bits(instr, 6, 0);
        uint8_t f3 = bits(instr, 14, 12);
        return (op == opcode::SYSTEM) && (f3 >= 1) && (f3 != 4);
    }
};

// ============================================================================
// CSR Executor
// ============================================================================

class Executor {
public:
    CSRExecResult execute(const DecodedCSRInstr& instr, RegFile& regs, CSRFile& csrs) const {
        CSRExecResult result = {};
        result.valid = true;
        
        if (instr.type == CSRInstrType::ILLEGAL) {
            result.valid = false;
            result.exception = true;
            result.exception_cause = exception::ILLEGAL_INSTRUCTION;
            result.trap_value = instr.raw;
            result.error = "Illegal instruction";
            return result;
        }
        
        // Accessing a CSR that does not exist in this implementation
        // raises an illegal-instruction exception.
        if (!csrs.exists(instr.csr)) {
            result.valid = false;
            result.exception = true;
            result.exception_cause = exception::ILLEGAL_INSTRUCTION;
            result.trap_value = instr.raw;
            result.error = "CSR does not exist";
            return result;
        }
        
        if (!csrs.can_read(instr.csr)) {
            result.valid = false;
            result.exception = true;
            result.exception_cause = exception::ILLEGAL_INSTRUCTION;
            result.trap_value = instr.raw;
            result.error = "CSR read permission denied";
            return result;
        }
        
        // Determine if a read / write will architecturally occur.
        // CSRRW/CSRRWI with rd = x0 must NOT read the CSR (no read side
        // effects); CSRRS/CSRRC(I) with rs1/uimm = 0 must NOT write it.
        bool will_write = false;
        bool will_read = true;
        switch (instr.type) {
            case CSRInstrType::CSRRW:
            case CSRInstrType::CSRRWI:
                will_write = true;
                will_read = (instr.rd != 0);
                break;
            case CSRInstrType::CSRRS:
            case CSRInstrType::CSRRC:
                will_write = (instr.rs1 != 0);
                break;
            case CSRInstrType::CSRRSI:
            case CSRInstrType::CSRRCI:
                will_write = (instr.uimm() != 0);
                break;
            default:
                break;
        }
        
        if (will_write && !csrs.can_write(instr.csr)) {
            result.valid = false;
            result.exception = true;
            result.exception_cause = exception::ILLEGAL_INSTRUCTION;
            result.trap_value = instr.raw;
            result.error = "CSR write permission denied";
            return result;
        }
        
        // Read current value (skipped for CSRRW/CSRRWI with rd = x0)
        uint32_t csr_val = 0;
        if (will_read) {
            csr_val = csrs.read(instr.csr);
        }
        result.old_csr_value = csr_val;
        result.csr_read = will_read;
        
        // Get source value
        uint32_t src_val;
        switch (instr.type) {
            case CSRInstrType::CSRRW:
            case CSRInstrType::CSRRS:
            case CSRInstrType::CSRRC:
                src_val = regs.read(instr.rs1);
                break;
            default:
                src_val = instr.uimm();
                break;
        }
        
        // Compute new value
        uint32_t new_val = csr_val;
        switch (instr.type) {
            case CSRInstrType::CSRRW:
            case CSRInstrType::CSRRWI:
                new_val = src_val;
                break;
            case CSRInstrType::CSRRS:
            case CSRInstrType::CSRRSI:
                if (will_write) new_val = csr_val | src_val;
                break;
            case CSRInstrType::CSRRC:
            case CSRInstrType::CSRRCI:
                if (will_write) new_val = csr_val & ~src_val;
                break;
            default:
                break;
        }
        
        // Write CSR
        if (will_write) {
            csrs.write(instr.csr, new_val);
            result.csr_written = true;
        }
        result.new_csr_value = new_val;
        
        // Write rd
        regs.write(instr.rd, csr_val);
        
        return result;
    }
};

// ============================================================================
// CSR Name Lookup
// ============================================================================

inline std::string csr_name(uint16_t addr) {
    switch (addr) {
        // Floating-point CSRs
        case csr_addr::FFLAGS:     return "fflags";
        case csr_addr::FRM:        return "frm";
        case csr_addr::FCSR:       return "fcsr";
        
        // User counters
        case csr_addr::CYCLE:      return "cycle";
        case csr_addr::TIME:       return "time";
        case csr_addr::INSTRET:    return "instret";
        case csr_addr::CYCLEH:     return "cycleh";
        case csr_addr::TIMEH:      return "timeh";
        case csr_addr::INSTRETH:   return "instreth";
        
        // Supervisor CSRs
        case csr_addr::SSTATUS:    return "sstatus";
        case csr_addr::SIE:        return "sie";
        case csr_addr::STVEC:      return "stvec";
        case csr_addr::SCOUNTEREN: return "scounteren";
        case csr_addr::SSCRATCH:   return "sscratch";
        case csr_addr::SEPC:       return "sepc";
        case csr_addr::SCAUSE:     return "scause";
        case csr_addr::STVAL:      return "stval";
        case csr_addr::SIP:        return "sip";
        case csr_addr::SATP:       return "satp";
        
        // Machine CSRs
        case csr_addr::MSTATUS:    return "mstatus";
        case csr_addr::MISA:       return "misa";
        case csr_addr::MEDELEG:    return "medeleg";
        case csr_addr::MIDELEG:    return "mideleg";
        case csr_addr::MIE:        return "mie";
        case csr_addr::MTVEC:      return "mtvec";
        case csr_addr::MCOUNTEREN: return "mcounteren";
        case csr_addr::MSCRATCH:   return "mscratch";
        case csr_addr::MEPC:       return "mepc";
        case csr_addr::MCAUSE:     return "mcause";
        case csr_addr::MTVAL:      return "mtval";
        case csr_addr::MIP:        return "mip";
        case csr_addr::MVENDORID:  return "mvendorid";
        case csr_addr::MARCHID:    return "marchid";
        case csr_addr::MIMPID:     return "mimpid";
        case csr_addr::MHARTID:    return "mhartid";
        case csr_addr::MCYCLE:     return "mcycle";
        case csr_addr::MINSTRET:   return "minstret";
        case csr_addr::MCYCLEH:    return "mcycleh";
        case csr_addr::MINSTRETH:  return "minstreth";
        
        default: {
            char buf[16];
            snprintf(buf, sizeof(buf), "0x%03X", addr);
            return buf;
        }
    }
}

// ============================================================================
// Mnemonic Generation
// ============================================================================

inline std::string DecodedCSRInstr::mnemonic() const {
    auto rd_s = reg_abi_name(rd);
    auto rs1_s = reg_abi_name(rs1);
    auto csr_s = csr_name(csr);
    
    switch (type) {
        case CSRInstrType::CSRRW:
            return "csrrw " + std::string(rd_s) + ", " + csr_s + ", " + rs1_s;
        case CSRInstrType::CSRRS:
            return "csrrs " + std::string(rd_s) + ", " + csr_s + ", " + rs1_s;
        case CSRInstrType::CSRRC:
            return "csrrc " + std::string(rd_s) + ", " + csr_s + ", " + rs1_s;
        case CSRInstrType::CSRRWI:
            return "csrrwi " + std::string(rd_s) + ", " + csr_s + ", " + std::to_string(uimm());
        case CSRInstrType::CSRRSI:
            return "csrrsi " + std::string(rd_s) + ", " + csr_s + ", " + std::to_string(uimm());
        case CSRInstrType::CSRRCI:
            return "csrrci " + std::string(rd_s) + ", " + csr_s + ", " + std::to_string(uimm());
        case CSRInstrType::ILLEGAL:
            return "ILLEGAL";
    }
    return "UNKNOWN";
}

// ============================================================================
// Instruction Encoding Helpers
// ============================================================================

namespace encode {
    inline uint32_t csr_instr(uint8_t funct3, uint8_t rd, uint16_t csr, uint8_t rs1_uimm) {
        return (static_cast<uint32_t>(csr & 0xFFF) << 20) |
               (static_cast<uint32_t>(rs1_uimm & 0x1F) << 15) |
               (static_cast<uint32_t>(funct3) << 12) |
               (static_cast<uint32_t>(rd & 0x1F) << 7) |
               opcode::SYSTEM;
    }
    
    inline uint32_t csrrw(uint8_t rd, uint16_t csr, uint8_t rs1) {
        return csr_instr(funct3::CSRRW, rd, csr, rs1);
    }
    inline uint32_t csrrs(uint8_t rd, uint16_t csr, uint8_t rs1) {
        return csr_instr(funct3::CSRRS, rd, csr, rs1);
    }
    inline uint32_t csrrc(uint8_t rd, uint16_t csr, uint8_t rs1) {
        return csr_instr(funct3::CSRRC, rd, csr, rs1);
    }
    inline uint32_t csrrwi(uint8_t rd, uint16_t csr, uint8_t uimm) {
        return csr_instr(funct3::CSRRWI, rd, csr, uimm);
    }
    inline uint32_t csrrsi(uint8_t rd, uint16_t csr, uint8_t uimm) {
        return csr_instr(funct3::CSRRSI, rd, csr, uimm);
    }
    inline uint32_t csrrci(uint8_t rd, uint16_t csr, uint8_t uimm) {
        return csr_instr(funct3::CSRRCI, rd, csr, uimm);
    }
    
    // Pseudo-instructions
    inline uint32_t csrr(uint8_t rd, uint16_t csr) { return csrrs(rd, csr, 0); }
    inline uint32_t csrw(uint16_t csr, uint8_t rs1) { return csrrw(0, csr, rs1); }
    inline uint32_t csrs(uint16_t csr, uint8_t rs1) { return csrrs(0, csr, rs1); }
    inline uint32_t csrc(uint16_t csr, uint8_t rs1) { return csrrc(0, csr, rs1); }
}

} // namespace zicsr
} // namespace riscv

#endif // RISCV_ZICSR_HPP
