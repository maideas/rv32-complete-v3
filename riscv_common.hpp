/*******************************************************************************
 * RISC-V Common Definitions
 * 
 * Shared types, interfaces, and utilities used across all RISC-V models.
 ******************************************************************************/

#ifndef RISCV_COMMON_HPP
#define RISCV_COMMON_HPP

#include <cstdint>
#include <string>
#include <stdexcept>
#include <functional>
#include <algorithm>   // std::copy, std::fill, std::min, std::max
#include <cstdio>      // snprintf

namespace riscv {

// ============================================================================
// Bus Fault
// ============================================================================

/**
 * Exception thrown by Bus implementations on an access error.
 * Executors catch this and convert it into the architectural
 * load/store/instruction access-fault exception with mtval = fault address.
 */
class BusFault : public std::runtime_error {
public:
    uint32_t addr;
    explicit BusFault(uint32_t fault_addr)
        : std::runtime_error("Bus access fault at 0x" + std::to_string(fault_addr)),
          addr(fault_addr) {}
};

// ============================================================================
// Bus Interface
// ============================================================================

/**
 * Abstract bus interface for memory and I/O access.
 * This interface is injected into all models that need memory access.
 * Implementations signal access errors by throwing riscv::BusFault.
 */
class Bus {
public:
    virtual ~Bus() = default;
    
    // Byte access
    virtual uint8_t read8(uint32_t addr) = 0;
    virtual void write8(uint32_t addr, uint8_t data) = 0;
    
    // Half-word access (16-bit)
    virtual uint16_t read16(uint32_t addr) = 0;
    virtual void write16(uint32_t addr, uint16_t data) = 0;
    
    // Word access (32-bit)
    virtual uint32_t read32(uint32_t addr) = 0;
    virtual void write32(uint32_t addr, uint32_t data) = 0;
    
    // Instruction fetch (may have different timing/caching than data access)
    virtual uint32_t fetch32(uint32_t addr) { return read32(addr); }
    virtual uint16_t fetch16(uint32_t addr) { return read16(addr); }
};

// ============================================================================
// Simple Memory Implementation (for testing)
// ============================================================================

class SimpleMemory : public Bus {
public:
    static constexpr size_t DEFAULT_SIZE = 1 << 20;  // 1MB default
    
private:
    uint8_t* mem;
    size_t mem_size;
    uint32_t base_addr;
    bool owns_memory;
    
    // Bounds check using 64-bit arithmetic: with 32-bit arithmetic,
    // (addr - base) + size wraps for addresses near 2^32 and would let
    // out-of-bounds accesses through (guest-reachable host memory
    // corruption).
    void check_addr(uint32_t addr, uint64_t size) const {
        if (mem == nullptr || addr < base_addr ||
            (static_cast<uint64_t>(addr) - base_addr) + size >
                static_cast<uint64_t>(mem_size)) {
            throw BusFault(addr);
        }
    }
    
public:
    // Create memory with default size
    explicit SimpleMemory(size_t size = DEFAULT_SIZE, uint32_t base = 0)
        : mem(new uint8_t[size]()), mem_size(size), base_addr(base), owns_memory(true) {}
    
    // Use external buffer
    SimpleMemory(uint8_t* buffer, size_t size, uint32_t base = 0)
        : mem(buffer), mem_size(size), base_addr(base), owns_memory(false) {}
    
    ~SimpleMemory() override {
        if (owns_memory) delete[] mem;
    }
    
    // Disable copy
    SimpleMemory(const SimpleMemory&) = delete;
    SimpleMemory& operator=(const SimpleMemory&) = delete;
    
    // Allow move (moved-from object becomes an empty, safe-to-destroy shell)
    SimpleMemory(SimpleMemory&& other) noexcept
        : mem(other.mem), mem_size(other.mem_size), 
          base_addr(other.base_addr), owns_memory(other.owns_memory) {
        other.mem = nullptr;
        other.mem_size = 0;
        other.owns_memory = false;
    }
    
    SimpleMemory& operator=(SimpleMemory&& other) noexcept {
        if (this != &other) {
            if (owns_memory) delete[] mem;
            mem = other.mem;
            mem_size = other.mem_size;
            base_addr = other.base_addr;
            owns_memory = other.owns_memory;
            other.mem = nullptr;
            other.mem_size = 0;
            other.owns_memory = false;
        }
        return *this;
    }
    
    // Bus interface implementation
    uint8_t read8(uint32_t addr) override {
        check_addr(addr, 1);
        return mem[addr - base_addr];
    }
    
    void write8(uint32_t addr, uint8_t data) override {
        check_addr(addr, 1);
        mem[addr - base_addr] = data;
    }
    
    uint16_t read16(uint32_t addr) override {
        check_addr(addr, 2);
        uint32_t offset = addr - base_addr;
        return static_cast<uint16_t>(mem[offset]) |
               (static_cast<uint16_t>(mem[offset + 1]) << 8);
    }
    
    void write16(uint32_t addr, uint16_t data) override {
        check_addr(addr, 2);
        uint32_t offset = addr - base_addr;
        mem[offset]     = data & 0xFF;
        mem[offset + 1] = (data >> 8) & 0xFF;
    }
    
    uint32_t read32(uint32_t addr) override {
        check_addr(addr, 4);
        uint32_t offset = addr - base_addr;
        return static_cast<uint32_t>(mem[offset]) |
               (static_cast<uint32_t>(mem[offset + 1]) << 8) |
               (static_cast<uint32_t>(mem[offset + 2]) << 16) |
               (static_cast<uint32_t>(mem[offset + 3]) << 24);
    }
    
    void write32(uint32_t addr, uint32_t data) override {
        check_addr(addr, 4);
        uint32_t offset = addr - base_addr;
        mem[offset]     = data & 0xFF;
        mem[offset + 1] = (data >> 8) & 0xFF;
        mem[offset + 2] = (data >> 16) & 0xFF;
        mem[offset + 3] = (data >> 24) & 0xFF;
    }
    
    // Direct access for initialization/testing
    uint8_t* data() { return mem; }
    const uint8_t* data() const { return mem; }
    size_t size() const { return mem_size; }
    uint32_t base() const { return base_addr; }
    
    // Bulk load
    void load(uint32_t addr, const uint8_t* src, size_t len) {
        check_addr(addr, static_cast<uint64_t>(len));
        std::copy(src, src + len, mem + (addr - base_addr));
    }
    
    // Clear memory
    void clear() {
        std::fill(mem, mem + mem_size, 0);
    }
};

// ============================================================================
// Register File
// ============================================================================

/**
 * RISC-V integer register file.
 * 32 registers, x0 is hardwired to zero.
 */
struct RegFile {
    uint32_t x[32] = {0};
    
    // The index is masked to 5 bits so that out-of-range values from
    // misuse of the public API cannot corrupt memory (consistent with
    // rv32f::FRegFile).
    uint32_t read(uint8_t idx) const {
        idx &= 0x1F;
        return (idx == 0) ? 0 : x[idx];
    }
    
    void write(uint8_t idx, uint32_t val) {
        idx &= 0x1F;
        if (idx != 0) x[idx] = val;
    }
    
    void reset() {
        for (int i = 0; i < 32; i++) x[i] = 0;
    }
};

// ============================================================================
// Privilege Levels
// ============================================================================

enum class PrivilegeLevel : uint8_t {
    USER       = 0,
    SUPERVISOR = 1,
    RESERVED   = 2,
    MACHINE    = 3
};

// ============================================================================
// Exception/Trap Causes
// ============================================================================

namespace exception {
    // Synchronous exceptions
    constexpr uint32_t INSTR_ADDR_MISALIGNED  = 0;
    constexpr uint32_t INSTR_ACCESS_FAULT     = 1;
    constexpr uint32_t ILLEGAL_INSTRUCTION    = 2;
    constexpr uint32_t BREAKPOINT             = 3;
    constexpr uint32_t LOAD_ADDR_MISALIGNED   = 4;
    constexpr uint32_t LOAD_ACCESS_FAULT      = 5;
    constexpr uint32_t STORE_ADDR_MISALIGNED  = 6;
    constexpr uint32_t STORE_ACCESS_FAULT     = 7;
    constexpr uint32_t ECALL_FROM_U           = 8;
    constexpr uint32_t ECALL_FROM_S           = 9;
    constexpr uint32_t ECALL_FROM_M           = 11;
    constexpr uint32_t INSTR_PAGE_FAULT       = 12;
    constexpr uint32_t LOAD_PAGE_FAULT        = 13;
    constexpr uint32_t STORE_PAGE_FAULT       = 15;
    
    // Interrupts (bit 31 set)
    constexpr uint32_t INTERRUPT_BIT          = 0x80000000;
    constexpr uint32_t S_SOFTWARE_INTERRUPT   = INTERRUPT_BIT | 1;
    constexpr uint32_t M_SOFTWARE_INTERRUPT   = INTERRUPT_BIT | 3;
    constexpr uint32_t S_TIMER_INTERRUPT      = INTERRUPT_BIT | 5;
    constexpr uint32_t M_TIMER_INTERRUPT      = INTERRUPT_BIT | 7;
    constexpr uint32_t S_EXTERNAL_INTERRUPT   = INTERRUPT_BIT | 9;
    constexpr uint32_t M_EXTERNAL_INTERRUPT   = INTERRUPT_BIT | 11;
}

// ============================================================================
// Utility Functions
// ============================================================================

// Extract bit range from value (safe for the full-width case hi=31, lo=0)
inline uint32_t bits(uint32_t val, int hi, int lo) {
    int width = hi - lo + 1;
    uint32_t mask = (width >= 32) ? 0xFFFFFFFFu : ((1U << width) - 1);
    return (val >> lo) & mask;
}

// Sign extend from bit position
inline int32_t sign_extend(uint32_t val, int sign_bit) {
    uint32_t mask = 1U << sign_bit;
    return static_cast<int32_t>((val ^ mask) - mask);
}

// Register ABI names
inline const char* reg_abi_name(uint8_t r) {
    static const char* names[] = {
        "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
        "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5",
        "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
        "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"
    };
    return (r < 32) ? names[r] : "x?";
}

// Compressed register mapping (x8-x15)
inline uint8_t creg_to_reg(uint8_t creg) {
    return 8 + (creg & 0x7);
}

// ============================================================================
// Shared Data-Memory Access Helpers
// ============================================================================

/**
 * Common load/store logic used by the executors (rv32i, rv32c, rv32f,
 * rv32fc): natural-alignment checking and mapping of bus errors to
 * access-fault exceptions. Centralized here so the trap semantics cannot
 * drift between modules.
 *
 * On failure, returns false and fills cause (exception::*) and tval
 * (the faulting address). On success, fills val (loads, zero-extended).
 */
namespace mem_access {

inline bool load(Bus& bus, uint32_t addr, uint32_t size, bool allow_misaligned,
                 uint32_t& val, uint32_t& cause, uint32_t& tval) {
    if (!allow_misaligned && size > 1 && (addr % size) != 0) {
        cause = exception::LOAD_ADDR_MISALIGNED;
        tval = addr;
        return false;
    }
    try {
        switch (size) {
            case 1:  val = bus.read8(addr);  break;
            case 2:  val = bus.read16(addr); break;
            default: val = bus.read32(addr); break;
        }
        return true;
    } catch (const std::exception&) {
        cause = exception::LOAD_ACCESS_FAULT;
        tval = addr;
        return false;
    }
}

inline bool store(Bus& bus, uint32_t addr, uint32_t size, uint32_t data,
                  bool allow_misaligned, uint32_t& cause, uint32_t& tval) {
    if (!allow_misaligned && size > 1 && (addr % size) != 0) {
        cause = exception::STORE_ADDR_MISALIGNED;
        tval = addr;
        return false;
    }
    try {
        switch (size) {
            case 1:  bus.write8(addr, data & 0xFF);      break;
            case 2:  bus.write16(addr, data & 0xFFFF);   break;
            default: bus.write32(addr, data);            break;
        }
        return true;
    } catch (const std::exception&) {
        cause = exception::STORE_ACCESS_FAULT;
        tval = addr;
        return false;
    }
}

} // namespace mem_access

} // namespace riscv

#endif // RISCV_COMMON_HPP
