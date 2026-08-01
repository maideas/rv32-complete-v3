/*******************************************************************************
 * Test suite for the RV32IMAFC_Zicsr_Zifencei_Zba_Zbb_Zbs_Zicond
 * reference model.
 *
 * Covers:
 *   - Regressions for every bug fixed in the review
 *     (decode strictness, C.JALR ordering, Zicond encoding, CSR behavior,
 *      F-extension corner cases, counters, traps, ...)
 *   - Full-CPU integration of all extensions (dispatch, traps, interrupts,
 *     MRET/SRET, misalignment policy, access faults)
 *   - Supervisor/User mode support (trap delegation, S-mode interrupts,
 *     interrupt prioritization/gating, sstatus subset, SATP TVM,
 *     MRET/SRET legality)
 *   - Opcode-generator round trips for every extension
 *   - Opcode-injection stepping (step(bus, opcode): no fetch, data access
 *     via bus, traps/interrupts, equivalence with fetched execution)
 *
 * Build:
 *   g++ -std=c++17 -Wall -Wextra -Wpedantic -fsanitize=undefined \
 *       -I. test_riscv_model.cpp -o test_riscv_model && ./test_riscv_model
 ******************************************************************************/

#include "riscv_cpu.hpp"
#include "riscv_rv32i_opgen.hpp"
#include "riscv_rv32m_opgen.hpp"
#include "riscv_rv32a_opgen.hpp"
#include "riscv_rv32c_opgen.hpp"
#include "riscv_rv32f_opgen.hpp"
#include "riscv_rv32fc_opgen.hpp"
#include "riscv_zicsr_opgen.hpp"
#include "riscv_zifencei_opgen.hpp"
#include "riscv_zba_opgen.hpp"
#include "riscv_zbb_opgen.hpp"
#include "riscv_zbs_opgen.hpp"
#include "riscv_zicond_opgen.hpp"

#include <cfenv>
#include <cmath>
#include <cstdio>
#include <cstring>

// ============================================================================
// Minimal test harness
// ============================================================================

static int g_tests = 0;
static int g_failures = 0;
static const char* g_current = "";

#define TEST(name) \
    g_current = name; g_tests++; \
    printf("  [%3d] %-64s", g_tests, name);

#define PASS() printf("PASS\n")

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n        %s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
        g_failures++; \
        return; \
    } \
} while (0)

#define CHECK_EQ(a, b) do { \
    auto va = (a); auto vb = (b); \
    if (!(va == vb)) { \
        printf("FAIL\n        %s:%d: %s == %s  (0x%llx vs 0x%llx)\n", \
               __FILE__, __LINE__, #a, #b, \
               (unsigned long long)va, (unsigned long long)vb); \
        g_failures++; \
        return; \
    } \
} while (0)

using namespace riscv;

// Convenience: fresh system with all extensions on
static System make_sys(size_t mem = 64 * 1024, CPUConfig cfg = CPUConfig()) {
    return System(mem, cfg);
}

static uint32_t f2b(float f) { uint32_t b; std::memcpy(&b, &f, 4); return b; }
static float b2f(uint32_t b) { float f; std::memcpy(&f, &b, 4); return f; }

// ============================================================================
// A. Common layer
// ============================================================================

static void test_common() {
    printf("A. Common layer\n");
    
    {
        TEST("SimpleMemory out-of-bounds read throws BusFault with address");
        SimpleMemory mem(1024);
        bool caught = false;
        uint32_t fault_addr = 0;
        try { mem.read32(2000); }
        catch (const BusFault& bf) { caught = true; fault_addr = bf.addr; }
        CHECK(caught);
        CHECK_EQ(fault_addr, 2000u);
        PASS();
    }
    {
        TEST("SimpleMemory move leaves a safe, empty shell");
        SimpleMemory a(1024);
        a.write32(0, 0xDEADBEEF);
        SimpleMemory b(std::move(a));
        CHECK_EQ(b.read32(0), 0xDEADBEEFu);
        bool caught = false;
        try { a.read32(0); } catch (const BusFault&) { caught = true; }
        CHECK(caught);   // moved-from object faults instead of crashing
        PASS();
    }
    {
        TEST("bits() handles the full-width case");
        CHECK_EQ(bits(0xDEADBEEF, 31, 0), 0xDEADBEEFu);
        CHECK_EQ(bits(0xDEADBEEF, 31, 28), 0xDu);
        PASS();
    }
    {
        TEST("check_addr: no uint32 wrap for addresses near 2^32 (security)");
        SimpleMemory mem(4096);
        bool caught = false;
        try { mem.read32(0xFFFFFFFC); } catch (const BusFault&) { caught = true; }
        CHECK(caught);   // previously: wrapped bounds check -> heap OOB read
        caught = false;
        try { mem.write32(0xFFFFFFFE, 1); } catch (const BusFault&) { caught = true; }
        CHECK(caught);
        caught = false;
        try { mem.read16(0xFFFFFFFF); } catch (const BusFault&) { caught = true; }
        CHECK(caught);
        // A guest lw at 0xFFFFFFFC must be a clean access fault, not a crash
        auto sys = System(4096);
        sys.cpu.regs.write(2, 0xFFFFFFFC);
        sys.memory.write32(0, 0x00012283);               // lw x5, 0(x2)
        auto r = sys.step();
        CHECK(r.trap);
        CHECK_EQ(r.trap_cause, exception::LOAD_ACCESS_FAULT);
        PASS();
    }
    {
        TEST("SimpleMemory::load() rejects lengths beyond the memory size");
        SimpleMemory mem(64);
        static uint8_t src[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        bool caught = false;
        try { mem.load(60, src, 8); } catch (const BusFault&) { caught = true; }
        CHECK(caught);
        mem.load(56, src, 8);        // exactly at the end: OK
        CHECK_EQ(mem.read8(63), 8u);
        PASS();
    }
    {
        TEST("RegFile masks out-of-range indices (no OOB write)");
        RegFile r;
        r.write(40, 0xABCD);         // 40 & 0x1F = 8
        CHECK_EQ(r.read(8), 0xABCDu);
        CHECK_EQ(r.read(40), 0xABCDu);
        r.write(32, 0x999);          // 32 & 0x1F = 0 -> x0 stays 0
        CHECK_EQ(r.read(0), 0u);
        PASS();
    }
}

// ============================================================================
// B. RV32I decode strictness (regressions)
// ============================================================================

static void test_rv32i_decode() {
    printf("B. RV32I decode strictness\n");
    rv32i::Decoder dec;
    
    {
        TEST("MRET and WFI decode; SYSTEM garbage is illegal");
        CHECK(dec.decode(0x30200073).type == rv32i::InstrType::MRET);
        CHECK(dec.decode(0x10500073).type == rv32i::InstrType::WFI);
        CHECK(dec.decode(0x00000073).type == rv32i::InstrType::ECALL);
        CHECK(dec.decode(0x00100073).type == rv32i::InstrType::EBREAK);
        // Previously: everything != ECALL decoded as EBREAK
        CHECK(dec.decode(0x00200073).type == rv32i::InstrType::ILLEGAL);
        CHECK(dec.decode(0x10200073).type == rv32i::InstrType::ILLEGAL);  // SRET
        CHECK(dec.decode(0x7FF00073).type == rv32i::InstrType::ILLEGAL);
        PASS();
    }
    {
        TEST("OP opcode: non-canonical funct7 no longer decodes as ADD");
        // add x1, x2, x3 with funct7 = 0000010 (reserved)
        uint32_t bad_add = (0x02u << 25) | (3 << 20) | (2 << 15) | (0 << 12) | (1 << 7) | 0x33;
        CHECK(dec.decode(bad_add).type == rv32i::InstrType::ILLEGAL);
        // SUB pattern with funct3 = 001 (no such instruction)
        uint32_t bad_sub = (0x20u << 25) | (3 << 20) | (2 << 15) | (1 << 12) | (1 << 7) | 0x33;
        CHECK(dec.decode(bad_sub).type == rv32i::InstrType::ILLEGAL);
        // Canonical ADD/SUB still fine
        uint32_t add = (3 << 20) | (2 << 15) | (1 << 7) | 0x33;
        CHECK(dec.decode(add).type == rv32i::InstrType::ADD);
        uint32_t sub = (0x20u << 25) | (3 << 20) | (2 << 15) | (1 << 7) | 0x33;
        CHECK(dec.decode(sub).type == rv32i::InstrType::SUB);
        PASS();
    }
    {
        TEST("Shift immediates: reserved shamt/funct7 encodings are illegal");
        // slli x1, x2, 33 (imm[5] set)
        uint32_t slli33 = (33u << 20) | (2 << 15) | (1 << 12) | (1 << 7) | 0x13;
        CHECK(dec.decode(slli33).type == rv32i::InstrType::ILLEGAL);
        // srli with funct7 = 0000001
        uint32_t bad_srli = (0x01u << 25) | (4 << 20) | (2 << 15) | (5 << 12) | (1 << 7) | 0x13;
        CHECK(dec.decode(bad_srli).type == rv32i::InstrType::ILLEGAL);
        // canonical slli/srai fine
        uint32_t slli4 = (4u << 20) | (2 << 15) | (1 << 12) | (1 << 7) | 0x13;
        CHECK(dec.decode(slli4).type == rv32i::InstrType::SLLI);
        uint32_t srai4 = (0x20u << 25) | (4 << 20) | (2 << 15) | (5 << 12) | (1 << 7) | 0x13;
        CHECK(dec.decode(srai4).type == rv32i::InstrType::SRAI);
        PASS();
    }
    {
        TEST("MISC-MEM: only FENCE decodes here; other funct3 illegal");
        CHECK(dec.decode(0x0FF0000F).type == rv32i::InstrType::FENCE);
        CHECK(dec.decode(0x0000100F).type == rv32i::InstrType::ILLEGAL); // fence.i -> Zifencei
        CHECK(dec.decode(0x0000200F).type == rv32i::InstrType::ILLEGAL);
        PASS();
    }
    {
        TEST("Golden RV32I encodings still decode");
        auto d = dec.decode(0x00310093);   // addi x1, x2, 3
        CHECK(d.type == rv32i::InstrType::ADDI);
        CHECK_EQ(d.rd, 1); CHECK_EQ(d.rs1, 2); CHECK_EQ((uint32_t)d.imm, 3u);
        d = dec.decode(0x00812283);        // lw x5, 8(x2)
        CHECK(d.type == rv32i::InstrType::LW);
        CHECK_EQ(d.rd, 5); CHECK_EQ((uint32_t)d.imm, 8u);
        PASS();
    }
}

// ============================================================================
// C. RV32I execution: misalignment, faults, IALIGN
// ============================================================================

static void test_rv32i_exec() {
    printf("C. RV32I execution\n");
    
    {
        TEST("Misaligned LW traps with cause 4 and mtval = address");
        auto sys = make_sys();
        sys.cpu.regs.write(2, 0x101);                    // base
        sys.memory.write32(0, 0x00012283);               // lw x5, 0(x2)
        auto r = sys.step();
        CHECK(r.trap);
        CHECK_EQ(r.trap_cause, exception::LOAD_ADDR_MISALIGNED);
        CHECK_EQ(r.trap_value, 0x101u);
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::MTVAL), 0x101u);
        PASS();
    }
    {
        TEST("allow_misaligned_data = true executes misaligned LW");
        CPUConfig cfg; cfg.allow_misaligned_data = true;
        auto sys = make_sys(64 * 1024, cfg);
        sys.memory.write32(0x100, 0xAABBCCDD);
        sys.memory.write32(0x104, 0x11223344);
        sys.cpu.regs.write(2, 0x101);
        sys.memory.write32(0, 0x00012283);               // lw x5, 0(x2)
        auto r = sys.step();
        CHECK(!r.trap);
        CHECK_EQ(sys.cpu.regs.read(5), 0x44AABBCCu);     // little-endian
        PASS();
    }
    {
        TEST("Misaligned SH traps with cause 6");
        auto sys = make_sys();
        sys.cpu.regs.write(2, 0x101);
        sys.memory.write32(0, 0x00511023);               // sh x5, 0(x2)
        auto r = sys.step();
        CHECK(r.trap);
        CHECK_EQ(r.trap_cause, exception::STORE_ADDR_MISALIGNED);
        PASS();
    }
    {
        TEST("Load beyond memory becomes LOAD_ACCESS_FAULT, not a C++ throw");
        auto sys = make_sys(4096);
        sys.cpu.regs.write(2, 0x100000);                 // way out of range
        sys.memory.write32(0, 0x00012283);               // lw x5, 0(x2)
        auto r = sys.step();
        CHECK(r.trap);
        CHECK_EQ(r.trap_cause, exception::LOAD_ACCESS_FAULT);
        CHECK_EQ(r.trap_value, 0x100000u);
        PASS();
    }
    {
        TEST("JALR to 2-misaligned target traps when C disabled, rd unwritten");
        CPUConfig cfg; cfg.enable_c_extension = false;
        auto sys = make_sys(64 * 1024, cfg);
        sys.cpu.regs.write(2, 0x102);                    // target = 0x102
        sys.cpu.regs.write(1, 0x55555555);
        sys.memory.write32(0, 0x000100E7);               // jalr x1, 0(x2)
        auto r = sys.step();
        CHECK(r.trap);
        CHECK_EQ(r.trap_cause, exception::INSTR_ADDR_MISALIGNED);
        CHECK_EQ(r.trap_value, 0x102u);
        CHECK_EQ(sys.cpu.regs.read(1), 0x55555555u);     // link NOT written
        PASS();
    }
    {
        TEST("Same JALR target is fine with C enabled (IALIGN=16)");
        auto sys = make_sys();
        sys.cpu.regs.write(2, 0x102);
        sys.memory.write32(0, 0x000100E7);               // jalr x1, 0(x2)
        auto r = sys.step();
        CHECK(!r.trap);
        CHECK_EQ(sys.cpu.pc, 0x102u);
        CHECK_EQ(sys.cpu.regs.read(1), 4u);
        PASS();
    }
    {
        TEST("EBREAK sets mtval = pc; illegal sets mtval = instruction");
        auto sys = make_sys();
        sys.memory.write32(0, 0x00000013);               // nop
        sys.memory.write32(4, 0x00100073);               // ebreak
        sys.step();
        auto r = sys.step();
        CHECK(r.trap);
        CHECK_EQ(r.trap_cause, exception::BREAKPOINT);
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::MTVAL), 4u);
        
        auto sys2 = make_sys();
        sys2.memory.write32(0, 0xFFFFFFFF);              // illegal
        auto r2 = sys2.step();
        CHECK(r2.trap);
        CHECK_EQ(r2.trap_cause, exception::ILLEGAL_INSTRUCTION);
        CHECK_EQ(sys2.cpu.csrs.read(zicsr::csr_addr::MTVAL), 0xFFFFFFFFu);
        PASS();
    }
    {
        TEST("WFI executes as a NOP");
        auto sys = make_sys();
        sys.memory.write32(0, 0x10500073);               // wfi
        auto r = sys.step();
        CHECK(!r.trap);
        CHECK_EQ(sys.cpu.pc, 4u);
        PASS();
    }
}

// ============================================================================
// D. RV32C
// ============================================================================

static void test_rv32c() {
    printf("D. RV32C\n");
    rv32c::Decoder dec;
    
    {
        TEST("c.jalr ra jumps to the OLD value of ra (regression)");
        auto sys = make_sys();
        sys.cpu.regs.write(1, 0x200);                    // ra = 0x200
        sys.memory.write16(0, 0x9082);                   // c.jalr x1
        auto r = sys.step();
        CHECK(!r.trap);
        CHECK_EQ(sys.cpu.pc, 0x200u);                    // old ra, not pc+2
        CHECK_EQ(sys.cpu.regs.read(1), 2u);              // link = pc + 2
        PASS();
    }
    {
        TEST("Reserved C encodings are illegal");
        CHECK(dec.decode(0x8002).type == rv32c::InstrType::ILLEGAL); // c.jr x0
        CHECK(dec.decode(0x4002).type == rv32c::InstrType::ILLEGAL); // c.lwsp x0
        CHECK(dec.decode(0x6081).type == rv32c::InstrType::ILLEGAL); // c.lui x1, 0
        CHECK(dec.decode(0x6101).type == rv32c::InstrType::ILLEGAL); // c.addi16sp sp, 0
        CHECK(dec.decode(0x6105).type == rv32c::InstrType::C_ADDI16SP); // c.addi16sp sp, 32
        // c.slli x1, 32+ (bit 12 set)
        CHECK(dec.decode(0x1082).type == rv32c::InstrType::ILLEGAL);
        // c.srli bit12: 100 1 00 000 00000 01 = 0x9001
        CHECK(dec.decode(0x9001).type == rv32c::InstrType::ILLEGAL);
        // c.srai bit12: 100 1 01 000 00000 01 = 0x9401
        CHECK(dec.decode(0x9401).type == rv32c::InstrType::ILLEGAL);
        // ...and the valid variants remain valid
        CHECK(dec.decode(0x8082).type == rv32c::InstrType::C_JR);    // c.jr ra
        CHECK(dec.decode(0x4082).type == rv32c::InstrType::C_LWSP);   // c.lwsp x1
        PASS();
    }
    {
        TEST("C.LW misaligned traps");
        auto sys = make_sys();
        sys.cpu.regs.write(8, 0x102);                    // x8 base
        sys.memory.write16(0, 0x4000 | (0 << 7) | (0 << 2)); // c.lw x8, 0(x8)
        auto r = sys.step();
        CHECK(r.trap);
        CHECK_EQ(r.trap_cause, exception::LOAD_ADDR_MISALIGNED);
        PASS();
    }
    {
        TEST("Compressed instruction in the last halfword executes (no throw)");
        auto sys = make_sys(0x1000);
        sys.memory.write16(0x0FFE, 0x0001);              // c.nop at the very end
        sys.cpu.pc = 0x0FFE;
        auto r = sys.step();                              // must not throw
        CHECK(!r.trap);
        CHECK_EQ(sys.cpu.pc, 0x1000u);
        PASS();
    }
    {
        TEST("Instruction fetch out of bounds becomes INSTR_ACCESS_FAULT");
        auto sys = make_sys(0x1000);
        sys.cpu.pc = 0x2000;
        auto r = sys.step();
        CHECK(r.trap);
        CHECK_EQ(r.trap_cause, exception::INSTR_ACCESS_FAULT);
        CHECK_EQ(r.trap_value, 0x2000u);
        PASS();
    }
    {
        TEST("Fault on the SECOND parcel: mtval = pc + 2, mepc = pc");
        auto sys = make_sys(0x1000);
        sys.cpu.csrs.write(zicsr::csr_addr::MTVEC, 0x800);
        // 32-bit instruction starting in the last halfword: lower parcel
        // has bits[1:0] = 11, upper parcel is out of bounds.
        sys.memory.write16(0x0FFE, 0x0073);               // bits[1:0] = 11
        sys.cpu.pc = 0x0FFE;
        auto r = sys.step();
        CHECK(r.trap);
        CHECK_EQ(r.trap_cause, exception::INSTR_ACCESS_FAULT);
        CHECK_EQ(r.trap_value, 0x1000u);                  // faulting portion
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::MEPC), 0x0FFEu);  // start
        PASS();
    }
    {
        TEST("disasm() never throws on unmapped addresses");
        auto sys = make_sys(0x1000);
        CHECK(sys.disasm(0x10000) == "<fetch fault>");
        sys.memory.write16(0x0FFE, 0x0073);               // 32-bit word cut off
        CHECK(sys.disasm(0x0FFE) == "<fetch fault>");
        PASS();
    }
}

// ============================================================================
// E. A extension through the CPU (regression: was never dispatched)
// ============================================================================

static void test_rv32a() {
    printf("E. A extension via CPU\n");
    
    {
        TEST("AMOADD.W executes through CPU::step");
        auto sys = make_sys();
        sys.memory.write32(0x100, 10);
        sys.cpu.regs.write(5, 0x100);
        sys.cpu.regs.write(4, 32);
        sys.memory.write32(0, 0x0042A1AF);               // amoadd.w x3, x4, (x5)
        auto r = sys.step();
        CHECK(!r.trap);
        CHECK_EQ(sys.cpu.regs.read(3), 10u);
        CHECK_EQ(sys.memory.read32(0x100), 42u);
        PASS();
    }
    {
        TEST("LR.W / SC.W pair succeeds; SC without reservation fails");
        auto sys = make_sys();
        sys.memory.write32(0x100, 7);
        sys.cpu.regs.write(5, 0x100);
        sys.cpu.regs.write(4, 99);
        // lr.w x3, (x5): 00010 00 00000 rs1=5 010 rd=3 0101111
        sys.memory.write32(0, 0x1002A1AF);
        // sc.w x6, x4, (x5): 00011 00 rs2=4 rs1=5 010 rd=6 0101111
        sys.memory.write32(4, 0x1842A32F);
        sys.step();
        CHECK_EQ(sys.cpu.regs.read(3), 7u);
        auto r = sys.step();
        CHECK(!r.trap);
        CHECK_EQ(sys.cpu.regs.read(6), 0u);              // success
        CHECK_EQ(sys.memory.read32(0x100), 99u);
        // second SC without a reservation fails
        sys.cpu.pc = 4;
        sys.step();
        CHECK_EQ(sys.cpu.regs.read(6), 1u);
        PASS();
    }
    {
        TEST("Misaligned AMO traps with cause 6, misaligned LR with cause 4");
        auto sys = make_sys();
        sys.cpu.regs.write(5, 0x102);
        sys.memory.write32(0, 0x0042A1AF);               // amoadd.w
        auto r = sys.step();
        CHECK(r.trap);
        CHECK_EQ(r.trap_cause, exception::STORE_ADDR_MISALIGNED);
        CHECK_EQ(r.trap_value, 0x102u);
        
        auto sys2 = make_sys();
        sys2.cpu.regs.write(5, 0x102);
        sys2.memory.write32(0, 0x1002A1AF);              // lr.w
        auto r2 = sys2.step();
        CHECK(r2.trap);
        CHECK_EQ(r2.trap_cause, exception::LOAD_ADDR_MISALIGNED);
        PASS();
    }
    {
        TEST("MISA advertises I, M, A, F, C");
        auto sys = make_sys();
        uint32_t misa = sys.cpu.csrs.read(zicsr::csr_addr::MISA);
        CHECK_EQ(misa, 0x40001125u);   // RV32 | I | M | A | F | C
        PASS();
    }
}

// ============================================================================
// F. Zbb / Zicond encodings and semantics
// ============================================================================

static void test_bitmanip_zicond() {
    printf("F. Zbb / Zicond\n");
    
    {
        TEST("ROR/ROL/RORI by 0 are identity (no UB; run under UBSan)");
        auto sys = make_sys();
        sys.cpu.regs.write(2, 0xDEADBEEF);
        sys.cpu.regs.write(3, 0);                        // shamt 0
        sys.memory.write32(0, zbb::encode::ror(1, 2, 3));
        sys.memory.write32(4, zbb::encode::rol(4, 2, 3));
        sys.memory.write32(8, zbb::encode::rori(5, 2, 0));
        sys.run(3);
        CHECK_EQ(sys.cpu.regs.read(1), 0xDEADBEEFu);
        CHECK_EQ(sys.cpu.regs.read(4), 0xDEADBEEFu);
        CHECK_EQ(sys.cpu.regs.read(5), 0xDEADBEEFu);
        PASS();
    }
    {
        TEST("czero.nez uses the ratified encoding (funct7 0000111/funct3 111)");
        CHECK_EQ(zicond::encode::czero_eqz(1, 2, 3), 0x0E3150B3u);
        CHECK_EQ(zicond::encode::czero_nez(1, 2, 3), 0x0E3170B3u);
        zicond::Decoder zd;
        CHECK(zd.decode(0x0E3170B3).type == zicond::InstrType::CZERO_NEZ);
        // Old (wrong) czero.nez encoding == MINU; must now be Zbb only
        uint32_t minu = (0x05u << 25) | (3 << 20) | (2 << 15) | (5 << 12) | (1 << 7) | 0x33;
        CHECK(!zd.is_zicond_instruction(minu));
        zbb::Decoder bd;
        CHECK(bd.decode(minu).type == zbb::InstrType::MINU);
        PASS();
    }
    {
        TEST("czero semantics through the CPU (incl. MINU disambiguation)");
        auto sys = make_sys();
        sys.cpu.regs.write(2, 1234);
        sys.cpu.regs.write(3, 0);
        sys.memory.write32(0, zicond::encode::czero_eqz(1, 2, 3));  // rs2==0 -> 0
        sys.memory.write32(4, zicond::encode::czero_nez(4, 2, 3));  // rs2==0 -> rs1
        uint32_t minu = (0x05u << 25) | (3 << 20) | (2 << 15) | (5 << 12) | (6 << 7) | 0x33;
        sys.memory.write32(8, minu);                                // minu x6, x2, x3
        sys.run(3);
        CHECK_EQ(sys.cpu.regs.read(1), 0u);
        CHECK_EQ(sys.cpu.regs.read(4), 1234u);
        CHECK_EQ(sys.cpu.regs.read(6), 0u);              // min(1234, 0) unsigned
        PASS();
    }
    {
        TEST("Zba sh1add/sh2add/sh3add through the CPU");
        auto sys = make_sys();
        sys.cpu.regs.write(2, 0x10);
        sys.cpu.regs.write(3, 0x1);
        sys.memory.write32(0, zba::encode::sh1add(1, 2, 3));
        sys.memory.write32(4, zba::encode::sh2add(4, 2, 3));
        sys.memory.write32(8, zba::encode::sh3add(5, 2, 3));
        sys.run(3);
        CHECK_EQ(sys.cpu.regs.read(1), 0x21u);
        CHECK_EQ(sys.cpu.regs.read(4), 0x41u);
        CHECK_EQ(sys.cpu.regs.read(5), 0x81u);
        PASS();
    }
    {
        TEST("Zbs bset/bclr/bext through the CPU");
        auto sys = make_sys();
        sys.cpu.regs.write(2, 0x0F0);
        sys.cpu.regs.write(3, 4);
        sys.memory.write32(0, zbs::encode::bset(1, 2, 3));
        sys.memory.write32(4, zbs::encode::bclr(4, 2, 3));
        sys.memory.write32(8, zbs::encode::bext(5, 2, 3));
        sys.run(3);
        CHECK_EQ(sys.cpu.regs.read(1), 0x0F0u);          // bit 4 already set
        CHECK_EQ(sys.cpu.regs.read(4), 0x0E0u);
        CHECK_EQ(sys.cpu.regs.read(5), 1u);
        PASS();
    }
    {
        TEST("Disabled extensions trap as illegal");
        CPUConfig cfg;
        cfg.enable_zbb = false;
        cfg.enable_zicond = false;
        auto sys = make_sys(64 * 1024, cfg);
        sys.memory.write32(0, zbb::encode::rori(1, 2, 5));
        auto r = sys.step();
        CHECK(r.trap);
        CHECK_EQ(r.trap_cause, exception::ILLEGAL_INSTRUCTION);
        sys.cpu.pc = 4;
        sys.memory.write32(4, zicond::encode::czero_eqz(1, 2, 3));
        r = sys.step();
        CHECK(r.trap);
        PASS();
    }
}

// ============================================================================
// G. F extension
// ============================================================================

static void test_rv32f() {
    printf("G. F extension\n");
    
    {
        TEST("Arithmetic NaN results are canonicalized to 0x7FC00000");
        auto sys = make_sys();
        sys.cpu.fregs.write_bits(1, f2b(INFINITY));
        sys.cpu.fregs.write_bits(2, f2b(-INFINITY));
        sys.memory.write32(0, rv32f::encode::fadd_s(3, 1, 2));  // inf + -inf
        // NaN with payload propagated through fadd:
        sys.cpu.fregs.write_bits(4, 0x7F800001);         // sNaN
        sys.cpu.fregs.write_bits(5, f2b(1.0f));
        sys.memory.write32(4, rv32f::encode::fadd_s(6, 4, 5));
        sys.run(2);
        CHECK_EQ(sys.cpu.fregs.read_bits(3), 0x7FC00000u);
        CHECK_EQ(sys.cpu.fregs.read_bits(6), 0x7FC00000u);
        // NV must be latched in fcsr for both
        CHECK((sys.cpu.csrs.read(zicsr::csr_addr::FFLAGS) & 0x10) != 0);
        PASS();
    }
    {
        TEST("FCVT.W.S of exactly -2^31 is valid (no NV)");
        auto sys = make_sys();
        sys.cpu.fregs.write_bits(1, f2b(-2147483648.0f));
        sys.memory.write32(0, rv32f::encode::fcvt_w_s(5, 1, 1)); // RTZ
        sys.run(1);
        CHECK_EQ(sys.cpu.regs.read(5), 0x80000000u);
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::FFLAGS), 0u);
        PASS();
    }
    {
        TEST("FCVT.W.S of 2^31 saturates to INT32_MAX with NV");
        auto sys = make_sys();
        sys.cpu.fregs.write_bits(1, f2b(2147483648.0f));
        sys.memory.write32(0, rv32f::encode::fcvt_w_s(5, 1, 1));
        sys.run(1);
        CHECK_EQ(sys.cpu.regs.read(5), 0x7FFFFFFFu);
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::FFLAGS), 0x10u);  // NV only
        PASS();
    }
    {
        TEST("FCVT.W.S of 1.5 (RTZ) = 1 and raises NX");
        auto sys = make_sys();
        sys.cpu.fregs.write_bits(1, f2b(1.5f));
        sys.memory.write32(0, rv32f::encode::fcvt_w_s(5, 1, 1));
        sys.run(1);
        CHECK_EQ(sys.cpu.regs.read(5), 1u);
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::FFLAGS), 0x01u);  // NX
        PASS();
    }
    {
        TEST("FCVT.WU.S of -0.5 (RTZ) = 0 with NX only, -1.0 raises NV");
        auto sys = make_sys();
        sys.cpu.fregs.write_bits(1, f2b(-0.5f));
        sys.memory.write32(0, rv32f::encode::fcvt_wu_s(5, 1, 1));
        sys.run(1);
        CHECK_EQ(sys.cpu.regs.read(5), 0u);
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::FFLAGS), 0x01u);  // NX, no NV
        
        auto sys2 = make_sys();
        sys2.cpu.fregs.write_bits(1, f2b(-1.0f));
        sys2.memory.write32(0, rv32f::encode::fcvt_wu_s(5, 1, 1));
        sys2.run(1);
        CHECK_EQ(sys2.cpu.regs.read(5), 0u);
        CHECK_EQ(sys2.cpu.csrs.read(zicsr::csr_addr::FFLAGS), 0x10u); // NV
        PASS();
    }
    {
        TEST("FCVT.W.S of NaN = INT32_MAX with NV");
        auto sys = make_sys();
        sys.cpu.fregs.write_bits(1, 0x7FC00000);
        sys.memory.write32(0, rv32f::encode::fcvt_w_s(5, 1, 1));
        sys.run(1);
        CHECK_EQ(sys.cpu.regs.read(5), 0x7FFFFFFFu);
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::FFLAGS), 0x10u);
        PASS();
    }
    {
        TEST("Reserved static rm (101/110) is an illegal instruction");
        rv32f::Decoder fd;
        CHECK(fd.decode(rv32f::encode::fadd_s(1, 2, 3, 0b101)).type
              == rv32f::InstrType::ILLEGAL);
        CHECK(fd.decode(rv32f::encode::fadd_s(1, 2, 3, 0b110)).type
              == rv32f::InstrType::ILLEGAL);
        // ...but rm is irrelevant for non-rounding ops like FSGNJ (funct3 is
        // the operation selector there)
        CHECK(fd.decode(rv32f::encode::fsgnj_s(1, 2, 3)).type
              == rv32f::InstrType::FSGNJ_S);
        PASS();
    }
    {
        TEST("DYN rounding with invalid frm traps as illegal");
        auto sys = make_sys();
        sys.cpu.csrs.write(zicsr::csr_addr::FRM, 0b111);   // invalid dynamic rm
        sys.cpu.fregs.write_bits(1, f2b(1.0f));
        sys.cpu.fregs.write_bits(2, f2b(2.0f));
        sys.memory.write32(0, rv32f::encode::fadd_s(3, 1, 2, 0b111)); // DYN
        auto r = sys.step();
        CHECK(r.trap);
        CHECK_EQ(r.trap_cause, exception::ILLEGAL_INSTRUCTION);
        PASS();
    }
    {
        TEST("Host FP rounding mode is restored after execution");
        std::fesetround(FE_UPWARD);
        auto sys = make_sys();
        sys.cpu.fregs.write_bits(1, f2b(1.0f));
        sys.cpu.fregs.write_bits(2, f2b(3.0f));
        sys.memory.write32(0, rv32f::encode::fdiv_s(3, 1, 2, 0b001)); // RTZ
        sys.run(1);
        CHECK_EQ(std::fegetround(), FE_UPWARD);
        std::fesetround(FE_TONEAREST);
        PASS();
    }
    {
        TEST("Directed rounding actually applies (fdiv 1/3 RUP vs RDN differ)");
        auto sys = make_sys();
        sys.cpu.fregs.write_bits(1, f2b(1.0f));
        sys.cpu.fregs.write_bits(2, f2b(3.0f));
        sys.memory.write32(0, rv32f::encode::fdiv_s(3, 1, 2, 0b011)); // RUP
        sys.memory.write32(4, rv32f::encode::fdiv_s(4, 1, 2, 0b010)); // RDN
        sys.run(2);
        uint32_t up = sys.cpu.fregs.read_bits(3);
        uint32_t dn = sys.cpu.fregs.read_bits(4);
        CHECK_EQ(up, dn + 1);   // adjacent floats around 1/3
        // NX flag latched
        CHECK((sys.cpu.csrs.read(zicsr::csr_addr::FFLAGS) & 0x01) != 0);
        PASS();
    }
    {
        TEST("FMIN/FMAX: signed zeros and NaN operands per spec");
        auto sys = make_sys();
        sys.cpu.fregs.write_bits(1, f2b(-0.0f));
        sys.cpu.fregs.write_bits(2, f2b(+0.0f));
        sys.cpu.fregs.write_bits(3, 0x7FC00000);          // qNaN
        sys.cpu.fregs.write_bits(4, f2b(5.0f));
        sys.memory.write32(0,  rv32f::encode::fmin_s(5, 2, 1));  // min(+0,-0) = -0
        sys.memory.write32(4,  rv32f::encode::fmax_s(6, 1, 2));  // max(-0,+0) = +0
        sys.memory.write32(8,  rv32f::encode::fmin_s(7, 3, 4));  // min(NaN,5) = 5
        sys.run(3);
        CHECK_EQ(sys.cpu.fregs.read_bits(5), 0x80000000u);
        CHECK_EQ(sys.cpu.fregs.read_bits(6), 0x00000000u);
        CHECK_EQ(sys.cpu.fregs.read_bits(7), f2b(5.0f));
        // qNaN operands do not raise NV for fmin/fmax
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::FFLAGS) & 0x10u, 0u);
        PASS();
    }
    {
        TEST("FLW/FSW through the CPU; misaligned FLW traps");
        auto sys = make_sys();
        sys.memory.write32(0x100, f2b(2.5f));
        sys.cpu.regs.write(2, 0x100);
        sys.memory.write32(0, rv32f::encode::flw(1, 2, 0));      // flw f1, 0(x2)
        sys.memory.write32(4, rv32f::encode::fadd_s(2, 1, 1));   // f2 = 5.0
        sys.memory.write32(8, rv32f::encode::fsw(2, 2, 4));      // fsw f2, 4(x2)
        sys.run(3);
        CHECK_EQ(sys.memory.read32(0x104), f2b(5.0f));
        
        auto sys2 = make_sys();
        sys2.cpu.regs.write(2, 0x101);
        sys2.memory.write32(0, rv32f::encode::flw(1, 2, 0));
        auto r = sys2.step();
        CHECK(r.trap);
        CHECK_EQ(r.trap_cause, exception::LOAD_ADDR_MISALIGNED);
        PASS();
    }
    {
        TEST("FMADD single-rounding and FCLASS through the CPU");
        auto sys = make_sys();
        sys.cpu.fregs.write_bits(1, f2b(3.0f));
        sys.cpu.fregs.write_bits(2, f2b(4.0f));
        sys.cpu.fregs.write_bits(3, f2b(5.0f));
        sys.memory.write32(0, rv32f::encode::fmadd_s(4, 1, 2, 3)); // 3*4+5
        sys.memory.write32(4, rv32f::encode::fclass_s(5, 4));
        sys.run(2);
        CHECK_EQ(b2f(sys.cpu.fregs.read_bits(4)), 17.0f);
        CHECK_EQ(sys.cpu.regs.read(5), 1u << 6);          // positive normal
        PASS();
    }
    {
        TEST("F disabled: FLW and fcsr access trap; MISA lacks F");
        CPUConfig cfg; cfg.enable_f_extension = false;
        auto sys = make_sys(64 * 1024, cfg);
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::MISA) & (1u << 5), 0u);
        sys.memory.write32(0, rv32f::encode::flw(1, 2, 0));
        auto r = sys.step();
        CHECK(r.trap);
        CHECK_EQ(r.trap_cause, exception::ILLEGAL_INSTRUCTION);
        sys.cpu.pc = 4;
        // csrr x1, fcsr
        sys.memory.write32(4, (0x003u << 20) | (0 << 15) | (2 << 12) | (1 << 7) | 0x73);
        r = sys.step();
        CHECK(r.trap);
        PASS();
    }
}

// ============================================================================
// H. FC (compressed floating-point)
// ============================================================================

static void test_rv32fc() {
    printf("H. FC compressed floating-point\n");
    
    {
        TEST("C.FLWSP with rd = f0 is VALID (regression)");
        rv32fc::Decoder dec;
        auto d = dec.decode(0x6002);                      // c.flwsp f0, 0(sp)
        CHECK(d.type == rv32fc::InstrType::C_FLWSP);
        CHECK_EQ(d.rd, 0);
        
        auto sys = make_sys();
        sys.memory.write32(0x100, f2b(1.5f));
        sys.cpu.regs.write(2, 0x100);                     // sp
        sys.memory.write16(0, 0x6002);
        auto r = sys.step();
        CHECK(!r.trap);
        CHECK_EQ(sys.cpu.fregs.read_bits(0), f2b(1.5f));
        PASS();
    }
    {
        TEST("C.FLW / C.FSW / C.FSWSP execute through the CPU");
        auto sys = make_sys();
        sys.memory.write32(0x100, f2b(2.0f));
        sys.cpu.regs.write(8, 0x100);                     // x8 base
        sys.cpu.regs.write(2, 0x200);                     // sp
        // c.flw f8, 0(x8)
        sys.memory.write16(0, rv32fc::encode::c_flw(8, 8, 0));
        // c.fsw f8, 4(x8)
        sys.memory.write16(2, rv32fc::encode::c_fsw(8, 8, 4));
        // c.fswsp f8, 0(sp)
        sys.memory.write16(4, rv32fc::encode::c_fswsp(8, 0));
        sys.run(3);
        CHECK_EQ(sys.memory.read32(0x104), f2b(2.0f));
        CHECK_EQ(sys.memory.read32(0x200), f2b(2.0f));
        PASS();
    }
    {
        TEST("C.FLW misaligned traps; FC round-trips via opgen");
        auto sys = make_sys();
        sys.cpu.regs.write(8, 0x101);
        sys.memory.write16(0, rv32fc::encode::c_flw(8, 8, 0));
        auto r = sys.step();
        CHECK(r.trap);
        CHECK_EQ(r.trap_cause, exception::LOAD_ADDR_MISALIGNED);
        
        rv32fc::Decoder dec;
        rv32fc::opgen::OpcodeGenerator gen(42);
        for (int i = 0; i < 5000; i++) {
            uint16_t op = gen.generate_random();
            CHECK(dec.is_compressed_float(op));
            CHECK(dec.decode(op).type != rv32fc::InstrType::ILLEGAL);
        }
        PASS();
    }
}

// ============================================================================
// I. Zicsr behavior
// ============================================================================

static void test_zicsr() {
    printf("I. Zicsr\n");
    
    {
        TEST("Unimplemented CSRs trap (0x7C0, sscratch); time is an alias of mcycle");
        auto sys = make_sys();
        auto csrr = [](uint16_t csr, uint8_t rd) {
            return (uint32_t(csr) << 20) | (2u << 12) | (uint32_t(rd) << 7) | 0x73u;
        };
        sys.memory.write32(0, csrr(0x7C0, 1));
        auto r = sys.step();
        CHECK(r.trap);
        CHECK_EQ(r.trap_cause, exception::ILLEGAL_INSTRUCTION);
        CHECK_EQ(r.trap_value, csrr(0x7C0, 1));

        sys.cpu.pc = 4;
        sys.memory.write32(4, csrr(0x140, 1));            // sscratch (S-mode)
        CHECK(sys.step().trap);
        sys.cpu.pc = 8;
        sys.memory.write32(8, csrr(0xC01, 1));            // time is now an alias
        auto mcycle_before = sys.cpu.csrs.read(zicsr::csr_addr::MCYCLE);
        CHECK(!sys.step().trap);
        CHECK_EQ(sys.cpu.regs.read(1), mcycle_before);
        PASS();
    }
    {
        TEST("mepc WARL: bit 0 cleared (C on), bits 1:0 cleared (C off)");
        auto sys = make_sys();
        sys.cpu.csrs.write(zicsr::csr_addr::MEPC, 0x1003);
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::MEPC), 0x1002u);
        
        CPUConfig cfg; cfg.enable_c_extension = false;
        auto sys2 = make_sys(4096, cfg);
        sys2.cpu.csrs.write(zicsr::csr_addr::MEPC, 0x1003);
        CHECK_EQ(sys2.cpu.csrs.read(zicsr::csr_addr::MEPC), 0x1000u);
        PASS();
    }
    {
        TEST("misa ignores writes; mtvec legalizes reserved modes");
        auto sys = make_sys();
        uint32_t misa = sys.cpu.csrs.read(zicsr::csr_addr::MISA);
        sys.cpu.csrs.write(zicsr::csr_addr::MISA, 0xDEADBEEF);
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::MISA), misa);
        
        sys.cpu.csrs.write(zicsr::csr_addr::MTVEC, 0x1000 | 3); // reserved mode
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::MTVEC), 0x1000u);
        sys.cpu.csrs.write(zicsr::csr_addr::MTVEC, 0x1000 | 1); // vectored OK
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::MTVEC), 0x1001u);
        PASS();
    }
    {
        TEST("mstatus WARL: MIE/MPIE writable; MPP=11, FS=11, SD=1 hardwired");
        auto sys = make_sys();
        sys.cpu.csrs.write(zicsr::csr_addr::MSTATUS, 0xFFFFFFFF);
        uint32_t ms = sys.cpu.csrs.read(zicsr::csr_addr::MSTATUS);
        CHECK_EQ(ms, (1u << 3) | (1u << 7) | (3u << 11) | (3u << 13) | (1u << 31));
        sys.cpu.csrs.write(zicsr::csr_addr::MSTATUS, 0);
        ms = sys.cpu.csrs.read(zicsr::csr_addr::MSTATUS);
        CHECK_EQ(ms, (3u << 11) | (3u << 13) | (1u << 31));  // hardwired remains
        PASS();
    }
    {
        TEST("fflags/frm are aliases of fcsr");
        auto sys = make_sys();
        sys.cpu.csrs.write(zicsr::csr_addr::FCSR, 0xFF);
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::FFLAGS), 0x1Fu);
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::FRM), 0x7u);
        sys.cpu.csrs.write(zicsr::csr_addr::FRM, 0b010);
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::FCSR), (0b010u << 5) | 0x1F);
        sys.cpu.csrs.write(zicsr::csr_addr::FFLAGS, 0);
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::FCSR), 0b010u << 5);
        PASS();
    }
    {
        TEST("CSRRW with rd = x0 performs no CSR read (side-effect check)");
        zicsr::CSRFile csrs;
        int reads = 0;
        csrs.set_read_callback([&](uint16_t addr) -> uint32_t {
            reads++;
            return csrs.get(addr);
        });
        zicsr::Decoder dec;
        zicsr::Executor ex;
        RegFile regs;
        regs.write(5, 0x123);
        // csrrw x0, mscratch, x5
        uint32_t instr = (0x340u << 20) | (5u << 15) | (1u << 12) | (0u << 7) | 0x73u;
        auto r = ex.execute(dec.decode(instr), regs, csrs);
        CHECK(r.valid);
        CHECK_EQ(reads, 0);
        CHECK(!r.csr_read);
        CHECK_EQ(csrs.get(zicsr::csr_addr::MSCRATCH), 0x123u);
        // csrrw x1, mscratch, x5 DOES read
        instr = (0x340u << 20) | (5u << 15) | (1u << 12) | (1u << 7) | 0x73u;
        r = ex.execute(dec.decode(instr), regs, csrs);
        CHECK_EQ(reads, 1);
        PASS();
    }
    {
        TEST("Read-only CSRs: pure read OK, write attempt traps");
        auto sys = make_sys();
        // csrrs x1, mhartid, x0 (pure read)
        uint32_t rd_ok = (0xF14u << 20) | (0u << 15) | (2u << 12) | (1u << 7) | 0x73u;
        sys.memory.write32(0, rd_ok);
        CHECK(!sys.step().trap);
        // csrrs x1, mhartid, x2 (write attempt)
        uint32_t wr_bad = (0xF14u << 20) | (2u << 15) | (2u << 12) | (1u << 7) | 0x73u;
        sys.memory.write32(4, wr_bad);
        CHECK(sys.step().trap);
        PASS();
    }
    {
        TEST("cycle/instret are read-only aliases of mcycle/minstret");
        auto sys = make_sys();
        for (int i = 0; i < 5; i++) sys.memory.write32(i * 4, 0x00000013);
        sys.run(5);
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::CYCLE),
                 sys.cpu.csrs.read(zicsr::csr_addr::MCYCLE));
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::INSTRET),
                 sys.cpu.csrs.read(zicsr::csr_addr::MINSTRET));
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::MINSTRET), 5u);
        PASS();
    }
    {
        TEST("minstret does not count trapped instructions; mcycle does");
        auto sys = make_sys();
        sys.memory.write32(0, 0x00000013);                // nop (retires)
        sys.memory.write32(4, 0xFFFFFFFF);                // illegal (traps)
        sys.step();
        sys.step();
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::MINSTRET), 1u);
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::MCYCLE), 2u);
        PASS();
    }
    {
        TEST("Priv-1.12 CSRs exist as WARL zero (mstatush/menvcfg/mconfigptr)");
        auto sys = make_sys();
        auto csrr = [](uint16_t csr, uint8_t rd) {
            return (uint32_t(csr) << 20) | (2u << 12) | (uint32_t(rd) << 7) | 0x73u;
        };
        // Reads succeed and return 0
        sys.memory.write32(0, csrr(0x310, 1));            // mstatush
        sys.memory.write32(4, csrr(0x30A, 1));            // menvcfg
        sys.memory.write32(8, csrr(0x31A, 1));            // menvcfgh
        sys.memory.write32(12, csrr(0xF15, 1));           // mconfigptr
        for (int i = 0; i < 4; i++) CHECK(!sys.step().trap);
        // Writes to the RW ones are legal but ignored (WARL zero)
        sys.cpu.csrs.write(zicsr::csr_addr::MSTATUSH, 0xFFFFFFFF);
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::MSTATUSH), 0u);
        sys.cpu.csrs.write(zicsr::csr_addr::MENVCFG, 0xFFFFFFFF);
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::MENVCFG), 0u);
        // mconfigptr is read-only by address: a write attempt traps
        uint32_t wr = (0xF15u << 20) | (2u << 15) | (1u << 12) | (1u << 7) | 0x73u;
        sys.memory.write32(16, wr);
        CHECK(sys.step().trap);
        PASS();
    }
    {
        TEST("Read callback preserves fflags/frm/cycle aliasing");
        zicsr::CSRFile csrs;
        csrs.write(zicsr::csr_addr::FCSR, 0xFF);
        int calls = 0;
        csrs.set_read_callback([&](uint16_t addr) -> uint32_t {
            calls++;
            return csrs.get(addr);
        });
        CHECK_EQ(csrs.read(zicsr::csr_addr::FFLAGS), 0x1Fu);   // was 0 before fix
        CHECK_EQ(csrs.read(zicsr::csr_addr::FRM), 0x7u);
        CHECK_EQ(csrs.read(zicsr::csr_addr::CYCLE),
                 csrs.read(zicsr::csr_addr::MCYCLE));
        CHECK(calls >= 3);   // callback observed the backing registers
        PASS();
    }
    {
        TEST("allow_misaligned_data toggles between steps without reset()");
        auto sys = make_sys();
        sys.memory.write32(0x100, 0xAABBCCDD);
        sys.memory.write32(0x104, 0x11223344);
        sys.cpu.regs.write(2, 0x101);
        sys.memory.write32(0, 0x00012283);                // lw x5, 0(x2)
        CHECK(sys.step().trap);                            // default: traps
        sys.cpu.config.allow_misaligned_data = true;       // toggle live
        sys.cpu.pc = 0;
        auto r = sys.step();
        CHECK(!r.trap);
        CHECK_EQ(sys.cpu.regs.read(5), 0x44AABBCCu);
        PASS();
    }
    {
        TEST("Zicsr opgen produces trap-free stimulus on the model");
        auto sys = make_sys();
        zicsr::opgen::OpcodeGenerator gen(1234);
        for (int i = 0; i < 3000; i++) {
            uint32_t op = gen.generate_random();
            sys.cpu.pc = 0;
            sys.memory.write32(0, op);
            auto r = sys.step();
            if (r.trap) {
                printf("FAIL\n        trap on generated %08X (%s)\n",
                       op, r.mnemonic.c_str());
                g_failures++;
                return;
            }
        }
        PASS();
    }
}

// ============================================================================
// J. Traps, MRET, interrupts
// ============================================================================

static void test_traps_interrupts() {
    printf("J. Traps, MRET, interrupts\n");
    
    {
        TEST("ECALL -> handler -> MRET round trip with MIE save/restore");
        auto sys = make_sys();
        sys.cpu.csrs.write(zicsr::csr_addr::MTVEC, 0x100);
        // enable interrupts so MIE save/restore is observable
        sys.cpu.csrs.write(zicsr::csr_addr::MSTATUS, zicsr::CSRFile::MSTATUS_MIE);
        sys.memory.write32(0, 0x00000013);                // nop
        sys.memory.write32(4, 0x00000073);                // ecall
        sys.memory.write32(8, 0x00100093);                // addi x1, x0, 1
        // handler at 0x100: mepc += 4; mret
        sys.memory.write32(0x100, (0x341u << 20) | (2u << 12) | (5u << 7) | 0x73); // csrrs x5, mepc, x0
        sys.memory.write32(0x104, 0x00428293);            // addi x5, x5, 4
        sys.memory.write32(0x108, (0x341u << 20) | (5u << 15) | (1u << 12) | 0x73); // csrrw x0, mepc, x5
        sys.memory.write32(0x10C, 0x30200073);            // mret
        
        sys.step();                                        // nop
        auto r = sys.step();                               // ecall
        CHECK(r.trap);
        CHECK_EQ(r.trap_cause, exception::ECALL_FROM_M);
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::MEPC), 4u);
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::MTVAL), 0u);
        CHECK_EQ(sys.cpu.pc, 0x100u);
        // MIE cleared, MPIE = old MIE
        uint32_t ms = sys.cpu.csrs.read(zicsr::csr_addr::MSTATUS);
        CHECK_EQ(ms & zicsr::CSRFile::MSTATUS_MIE, 0u);
        CHECK(ms & zicsr::CSRFile::MSTATUS_MPIE);
        
        sys.run(4);                                        // handler + mret
        CHECK_EQ(sys.cpu.pc, 8u);
        ms = sys.cpu.csrs.read(zicsr::csr_addr::MSTATUS);
        CHECK(ms & zicsr::CSRFile::MSTATUS_MIE);           // restored
        sys.step();
        CHECK_EQ(sys.cpu.regs.read(1), 1u);                // resumed correctly
        PASS();
    }
    {
        TEST("Interrupt gating: needs mstatus.MIE and mie bit");
        auto sys = make_sys();
        sys.memory.write32(0, 0x00000013);
        sys.cpu.set_timer_interrupt(true);
        auto r = sys.step();                               // MIE off -> no irq
        CHECK(!r.interrupt);
        
        sys.cpu.pc = 0;
        sys.cpu.csrs.write(zicsr::csr_addr::MSTATUS, zicsr::CSRFile::MSTATUS_MIE);
        r = sys.step();                                    // mie.MTIE off
        CHECK(!r.interrupt);
        
        sys.cpu.pc = 0;
        sys.cpu.csrs.write(zicsr::csr_addr::MIE, zicsr::CSRFile::MI_MTI);
        r = sys.step();
        CHECK(r.interrupt);
        CHECK_EQ(r.trap_cause, 0x80000007u);
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::MEPC), 0u);
        PASS();
    }
    {
        TEST("Vectored mtvec: interrupt to base + 4*code, exception to base");
        auto sys = make_sys();
        sys.cpu.csrs.write(zicsr::csr_addr::MTVEC, 0x200 | 1);   // vectored
        sys.cpu.csrs.write(zicsr::csr_addr::MSTATUS, zicsr::CSRFile::MSTATUS_MIE);
        sys.cpu.csrs.write(zicsr::csr_addr::MIE, zicsr::CSRFile::MI_MTI);
        sys.cpu.set_timer_interrupt(true);
        auto r = sys.step();
        CHECK(r.interrupt);
        CHECK_EQ(sys.cpu.pc, 0x200u + 4 * 7);
        
        sys.cpu.set_timer_interrupt(false);
        sys.cpu.pc = 0;
        sys.memory.write32(0, 0xFFFFFFFF);                 // illegal
        r = sys.step();
        CHECK(r.trap);
        CHECK_EQ(sys.cpu.pc, 0x200u);                      // BASE, not vectored
        PASS();
    }
    {
        TEST("Interrupt priority: MEI > MSI > MTI");
        auto sys = make_sys();
        sys.cpu.csrs.write(zicsr::csr_addr::MSTATUS, zicsr::CSRFile::MSTATUS_MIE);
        sys.cpu.csrs.write(zicsr::csr_addr::MIE, zicsr::CSRFile::MI_MASK);
        sys.cpu.set_timer_interrupt(true);
        sys.cpu.set_software_interrupt(true);
        sys.cpu.set_external_interrupt(true);
        auto r = sys.step();
        CHECK_EQ(r.trap_cause, 0x8000000Bu);               // MEI (11)
        sys.cpu.set_external_interrupt(false);
        sys.cpu.csrs.write(zicsr::csr_addr::MSTATUS, zicsr::CSRFile::MSTATUS_MIE);
        r = sys.step();
        CHECK_EQ(r.trap_cause, 0x80000003u);               // MSI (3)
        PASS();
    }
    {
        TEST("Interrupt does not increment minstret (no retirement)");
        auto sys = make_sys();
        sys.cpu.csrs.write(zicsr::csr_addr::MSTATUS, zicsr::CSRFile::MSTATUS_MIE);
        sys.cpu.csrs.write(zicsr::csr_addr::MIE, zicsr::CSRFile::MI_MTI);
        sys.cpu.set_timer_interrupt(true);
        auto r = sys.step();
        CHECK(r.interrupt);
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::MINSTRET), 0u);
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::MCYCLE), 1u);
        PASS();
    }
    {
        TEST("Writable mip bits (MSIP/SSIP) via CSR writes; external bits read-only");
        auto sys = make_sys();
        sys.cpu.csrs.write(zicsr::csr_addr::MIP, 0xFFFFFFFF);
        // Without S-mode only MSIP is writable.
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::MIP), zicsr::CSRFile::MI_MSI);
        sys.cpu.csrs.write(zicsr::csr_addr::MIP, 0);
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::MIP), 0u);
        sys.cpu.set_software_interrupt(true);
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::MIP), zicsr::CSRFile::MI_MSI);
        sys.cpu.set_software_interrupt(false);
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::MIP), 0u);
        PASS();
    }
}

// ============================================================================
// K. Cross-extension program + opgen round trips
// ============================================================================

static void test_integration() {
    printf("K. Integration\n");
    
    {
        TEST("Mixed-extension program runs to ECALL with expected state");
        auto sys = make_sys();
        sys.cpu.csrs.write(zicsr::csr_addr::MTVEC, 0x800);
        uint32_t p = 0;
        auto emit32 = [&](uint32_t w) { sys.memory.write32(p, w); p += 4; };
        auto emit16 = [&](uint16_t h) { sys.memory.write16(p, h); p += 2; };
        
        emit32(0x00500093);                                // addi x1, x0, 5
        emit32(0x00300113);                                // addi x2, x0, 3
        emit32(0x022081B3);                                // mul x3, x1, x2      (15)
        emit32(zba::encode::sh1add(4, 3, 1));              // x4 = 15*2+5 = 35
        emit32(zbb::encode::rori(5, 4, 1));                // x5 = ror(35,1)
        emit32(zicond::encode::czero_nez(6, 4, 2));        // x2!=0 -> x6 = 0
        emit32(0x10000393);                                // addi x7, x0, 0x100
        // amoadd.w x8, x4, (x7): 00000 00 rs2=4 rs1=7 010 rd=8 0101111
        emit32((0x00u << 27) | (4u << 20) | (7u << 15) | (2u << 12) | (8u << 7) | 0x2F);
        emit32(rv32f::encode::fcvt_s_w(1, 1));             // f1 = (float)x1
        emit32(rv32f::encode::fadd_s(2, 1, 1));            // f2 = 2*f1
        emit32(rv32f::encode::fcvt_w_s(9, 2));             // x9 = int(f2)
        emit16(0x0505);                                    // c.addi x10, 1
        emit16(0x0001);                                    // c.nop
        emit32(0x0000100F);                                // fence.i
        emit32((0x340u << 20) | (9u << 15) | (1u << 12) | 0x73); // csrrw x0, mscratch, x9
        emit32(0x00000073);                                // ecall
        
        sys.memory.write32(0x100, 7);                      // AMO target
        uint64_t executed = sys.run(100, /*stop_on_trap=*/true);
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::MCAUSE),
                 exception::ECALL_FROM_M);
        CHECK(executed <= 20);
        CHECK_EQ(sys.cpu.regs.read(3), 15u);
        CHECK_EQ(sys.cpu.regs.read(4), 35u);
        CHECK_EQ(sys.cpu.regs.read(5), (35u >> 1) | (35u << 31));
        CHECK_EQ(sys.cpu.regs.read(6), 0u);
        CHECK_EQ(sys.cpu.regs.read(8), 7u);                // old AMO value
        CHECK_EQ(sys.memory.read32(0x100), 42u);           // 7 + 35
        CHECK_EQ(sys.cpu.regs.read(9), 10u);               // 2 * 5
        CHECK_EQ(sys.cpu.regs.read(10), 1u);
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::MSCRATCH), 10u);
        PASS();
    }
    {
        TEST("Opgen round trips decode as valid in their own modules");
        {
            rv32i::opgen::OpcodeGenerator g(1); rv32i::Decoder d;
            for (int i = 0; i < 5000; i++) {
                uint32_t op = g.generate_random();
                // FENCE.I now belongs to Zifencei
                zifencei::Decoder zd;
                bool ok = (d.decode(op).type != rv32i::InstrType::ILLEGAL) ||
                          zd.is_zifencei_instruction(op);
                if (!ok) { printf("FAIL\n        i opgen %08X\n", op); g_failures++; return; }
            }
        }
        {
            rv32m::opgen::OpcodeGenerator g(2); rv32m::Decoder d;
            for (int i = 0; i < 5000; i++)
                CHECK(d.decode(g.generate_random()).type != rv32m::InstrType::ILLEGAL);
        }
        {
            rv32a::opgen::OpcodeGenerator g(3); rv32a::Decoder d;
            for (int i = 0; i < 5000; i++)
                CHECK(d.decode(g.generate_random()).type != rv32a::InstrType::ILLEGAL);
        }
        {
            rv32c::opgen::OpcodeGenerator g(4); rv32c::Decoder d;
            for (int i = 0; i < 5000; i++)
                CHECK(d.decode(g.generate_random()).type != rv32c::InstrType::ILLEGAL);
        }
        {
            rv32f::opgen::OpcodeGenerator g(5); rv32f::Decoder d;
            for (int i = 0; i < 5000; i++)
                CHECK(d.decode(g.generate_random()).type != rv32f::InstrType::ILLEGAL);
        }
        {
            zba::opgen::OpcodeGenerator g(6); zba::Decoder d;
            for (int i = 0; i < 5000; i++)
                CHECK(d.decode(g.generate_random()).type != zba::InstrType::ILLEGAL);
        }
        {
            zbb::opgen::OpcodeGenerator g(7); zbb::Decoder d;
            for (int i = 0; i < 5000; i++)
                CHECK(d.decode(g.generate_random()).type != zbb::InstrType::ILLEGAL);
        }
        {
            zbs::opgen::OpcodeGenerator g(8); zbs::Decoder d;
            for (int i = 0; i < 5000; i++)
                CHECK(d.decode(g.generate_random()).type != zbs::InstrType::ILLEGAL);
        }
        {
            zicond::opgen::OpcodeGenerator g(9); zicond::Decoder d;
            for (int i = 0; i < 5000; i++)
                CHECK(d.decode(g.generate_random()).type != zicond::InstrType::ILLEGAL);
        }
        {
            zifencei::opgen::OpcodeGenerator g(10); zifencei::Decoder d;
            for (int i = 0; i < 2000; i++)
                CHECK(d.decode(g.generate_random()).type != zifencei::InstrType::ILLEGAL);
        }
        PASS();
    }
    {
        TEST("c.j / c.jal opgen reaches the full offset range incl. -2048");
        rv32c::opgen::RNG rng(11);
        rv32c::Decoder d;
        int32_t min_seen = 0, max_seen = 0;
        for (int i = 0; i < 20000; i++) {
            uint16_t op = rv32c::opgen::gen_c_j(rng);
            auto dec = d.decode(op);
            CHECK(dec.type == rv32c::InstrType::C_J);
            min_seen = std::min(min_seen, dec.imm);
            max_seen = std::max(max_seen, dec.imm);
        }
        CHECK_EQ(min_seen, -2048);
        CHECK_EQ(max_seen, 2046);
        PASS();
    }
    {
        TEST("Random F opgen stimulus executes trap-free on the CPU");
        auto sys = make_sys();
        rv32f::opgen::OpcodeGenerator gen(77);
        for (int i = 0; i < 3000; i++) {
            uint32_t op = gen.generate_random();
            sys.cpu.pc = 0;
            // Re-anchor base registers each iteration so random offsets stay
            // in range (executed instructions overwrite integer registers)
            for (int r = 1; r < 32; r++) sys.cpu.regs.write(r, 0x8000);
            sys.memory.write32(0, op);
            auto res = sys.cpu.step(sys.memory);
            if (res.trap) {
                // Misaligned FLW/FSW from random offsets are expected and
                // architecturally correct; anything else is a bug.
                bool mem_trap = res.trap_cause == exception::LOAD_ADDR_MISALIGNED ||
                                res.trap_cause == exception::STORE_ADDR_MISALIGNED;
                if (!mem_trap) {
                    printf("FAIL\n        F trap cause %u on %08X (%s)\n",
                           res.trap_cause, op, res.mnemonic.c_str());
                    g_failures++;
                    return;
                }
                sys.cpu.csrs.write(zicsr::csr_addr::MTVEC, 0);  // stay at 0
            }
        }
        PASS();
    }
    {
        TEST("Disassembler covers every extension");
        auto sys = make_sys();
        sys.memory.write32(0, zicond::encode::czero_eqz(1, 2, 3));
        CHECK(sys.disasm(0).find("czero.eqz") != std::string::npos);
        sys.memory.write32(0, rv32f::encode::fmadd_s(1, 2, 3, 4));
        CHECK(sys.disasm(0).find("fmadd.s") != std::string::npos);
        sys.memory.write32(0, 0x30200073);
        CHECK(sys.disasm(0) == "mret");
        sys.memory.write16(0, 0x6002);
        CHECK(sys.disasm(0).find("c.flwsp") != std::string::npos);
        sys.memory.write32(0, 0x0000100F);
        CHECK(sys.disasm(0).find("fence.i") != std::string::npos);
        PASS();
    }
}

// ============================================================================
// L. Supervisor / User mode support
// ============================================================================

static void enter_mode(System& sys, PrivilegeLevel target, uint32_t target_pc) {
    sys.memory.write32(sys.cpu.pc, 0x30200073u);                    // mret
    sys.cpu.csrs.write(zicsr::csr_addr::MEPC, target_pc);
    uint32_t mstatus = sys.cpu.csrs.get(zicsr::csr_addr::MSTATUS);
    mstatus &= ~(zicsr::CSRFile::MSTATUS_MPP | zicsr::CSRFile::MSTATUS_MPRV);
    uint32_t mpp = (target == PrivilegeLevel::USER) ? 0u :
                   (target == PrivilegeLevel::SUPERVISOR) ? 1u : 3u;
    mstatus |= (mpp << 11) | zicsr::CSRFile::MSTATUS_MPIE;
    sys.cpu.csrs.set(zicsr::csr_addr::MSTATUS, mstatus);
    auto r = sys.step();
    CHECK(!r.trap && !r.interrupt);
    CHECK_EQ(sys.cpu.csrs.get_privilege(), target);
    CHECK_EQ(sys.cpu.pc, target_pc);
}

static void test_supervisor_user_mode() {
    printf("L. Supervisor / User mode\n");

    CPUConfig su_cfg;
    su_cfg.enable_s_mode = true;
    su_cfg.enable_u_mode = true;

    {
        TEST("mstatus S/U fields are present and WARL-legalized");
        auto sys = make_sys(4096, su_cfg);
        sys.cpu.csrs.write(zicsr::csr_addr::MSTATUS, 0xFFFFFFFFu);
        uint32_t ms = sys.cpu.csrs.read(zicsr::csr_addr::MSTATUS);
        CHECK(ms & zicsr::CSRFile::MSTATUS_SIE);
        CHECK(ms & zicsr::CSRFile::MSTATUS_SPIE);
        CHECK(ms & zicsr::CSRFile::MSTATUS_SPP);
        CHECK(ms & zicsr::CSRFile::MSTATUS_MPRV);
        CHECK(ms & zicsr::CSRFile::MSTATUS_SUM);
        CHECK(ms & zicsr::CSRFile::MSTATUS_MXR);
        CHECK(ms & zicsr::CSRFile::MSTATUS_TVM);
        CHECK(ms & zicsr::CSRFile::MSTATUS_TW);
        CHECK(ms & zicsr::CSRFile::MSTATUS_TSR);
        CHECK_EQ(ms & zicsr::CSRFile::MSTATUS_MPP, zicsr::CSRFile::MSTATUS_MPP);
        CHECK_EQ(ms & zicsr::CSRFile::MSTATUS_FS, zicsr::CSRFile::MSTATUS_FS);
        PASS();
    }

    {
        TEST("S/U CSR existence and MISA S/U bits");
        auto sys = make_sys(4096, su_cfg);
        CHECK(sys.cpu.csrs.exists(zicsr::csr_addr::SSTATUS));
        CHECK(sys.cpu.csrs.exists(zicsr::csr_addr::SIE));
        CHECK(sys.cpu.csrs.exists(zicsr::csr_addr::STVEC));
        CHECK(sys.cpu.csrs.exists(zicsr::csr_addr::SEPC));
        CHECK(sys.cpu.csrs.exists(zicsr::csr_addr::SCAUSE));
        CHECK(sys.cpu.csrs.exists(zicsr::csr_addr::STVAL));
        CHECK(sys.cpu.csrs.exists(zicsr::csr_addr::SATP));
        CHECK(sys.cpu.csrs.exists(zicsr::csr_addr::MEDELEG));
        CHECK(sys.cpu.csrs.exists(zicsr::csr_addr::MIDELEG));
        CHECK(sys.cpu.csrs.exists(zicsr::csr_addr::MCOUNTEREN));
        CHECK(sys.cpu.csrs.exists(zicsr::csr_addr::SCOUNTEREN));
        uint32_t misa = sys.cpu.csrs.read(zicsr::csr_addr::MISA);
        CHECK(misa & (1u << 18));  // S
        CHECK(misa & (1u << 20));  // U
        PASS();
    }

    {
        TEST("ECALL from U/S/M reports correct cause when not delegated");
        for (auto mode : {PrivilegeLevel::MACHINE,
                          PrivilegeLevel::SUPERVISOR,
                          PrivilegeLevel::USER}) {
            auto sys = make_sys(4096, su_cfg);
            uint32_t handler = 0x800u;
            sys.cpu.csrs.write(zicsr::csr_addr::MTVEC, handler);
            enter_mode(sys, mode, 0x100u);
            sys.memory.write32(0x100u, 0x00000073u);     // ecall
            auto r = sys.step();
            CHECK(r.trap);
            uint32_t expected = (mode == PrivilegeLevel::MACHINE)   ? exception::ECALL_FROM_M :
                                (mode == PrivilegeLevel::SUPERVISOR) ? exception::ECALL_FROM_S :
                                                                          exception::ECALL_FROM_U;
            CHECK_EQ(r.trap_cause, expected);
            // Non-delegated traps land in M mode.
            CHECK_EQ(sys.cpu.csrs.get_privilege(), PrivilegeLevel::MACHINE);
            CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::MCAUSE), expected);
            CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::MEPC), 0x100u);
            uint32_t mpp = (sys.cpu.csrs.read(zicsr::csr_addr::MSTATUS) >> 11) & 3u;
            uint32_t expected_mpp = (mode == PrivilegeLevel::MACHINE) ? 3u :
                                    (mode == PrivilegeLevel::SUPERVISOR) ? 1u : 0u;
            CHECK_EQ(mpp, expected_mpp);
        }
        PASS();
    }

    {
        TEST("Exception delegation: ECALL from U traps to S mode");
        auto sys = make_sys(4096, su_cfg);
        uint32_t m_handler = 0x800u;
        uint32_t s_handler = 0x900u;
        sys.cpu.csrs.write(zicsr::csr_addr::MTVEC, m_handler);
        sys.cpu.csrs.write(zicsr::csr_addr::STVEC, s_handler);
        // Delegate ECALL-from-U (cause 8) to S mode.
        sys.cpu.csrs.write(zicsr::csr_addr::MEDELEG, 1u << 8);
        enter_mode(sys, PrivilegeLevel::USER, 0x100u);
        sys.memory.write32(0x100u, 0x00000073u);     // ecall
        auto r = sys.step();
        CHECK(r.trap);
        CHECK_EQ(r.trap_cause, exception::ECALL_FROM_U);
        CHECK_EQ(sys.cpu.csrs.get_privilege(), PrivilegeLevel::SUPERVISOR);
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::SCAUSE), exception::ECALL_FROM_U);
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::SEPC), 0x100u);
        CHECK_EQ(sys.cpu.pc, s_handler);
        // SPP should be 0 because previous mode was U.
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::MSTATUS) & zicsr::CSRFile::MSTATUS_SPP, 0u);
        PASS();
    }

    {
        TEST("MRET returns to S/U and SRET returns to U");
        auto sys = make_sys(4096, su_cfg);
        // Drop to S mode.
        enter_mode(sys, PrivilegeLevel::SUPERVISOR, 0x100u);
        // Trap from S back to M with ECALL-from-S.
        sys.memory.write32(0x100u, 0x00000073u);     // ecall
        sys.cpu.csrs.write(zicsr::csr_addr::MTVEC, 0x800u);
        auto r = sys.step();
        CHECK(r.trap);
        CHECK_EQ(sys.cpu.csrs.get_privilege(), PrivilegeLevel::MACHINE);
        // The ECALL saved MEPC=0x100.  Move it past the ECALL, then MRET
        // back to S.
        sys.cpu.csrs.write(zicsr::csr_addr::MEPC, 0x104u);
        sys.memory.write32(sys.cpu.pc, 0x30200073u); // mret
        r = sys.step();
        CHECK(!r.trap);
        CHECK_EQ(sys.cpu.csrs.get_privilege(), PrivilegeLevel::SUPERVISOR);
        CHECK_EQ(sys.cpu.pc, 0x104u);
        // SRET back to U.  SPP was 0 because the S->M trap does not save
        // into SPP; the previous SPP was cleared on the M->S transition.
        sys.cpu.csrs.write(zicsr::csr_addr::SEPC, 0x104u);
        sys.memory.write32(sys.cpu.pc, 0x10200073u); // sret
        r = sys.step();
        CHECK(!r.trap);
        CHECK_EQ(sys.cpu.csrs.get_privilege(), PrivilegeLevel::USER);
        CHECK_EQ(sys.cpu.pc, 0x104u);
        PASS();
    }

    {
        TEST("SRET illegal in U mode and when mstatus.TSR=1 in S mode");
        auto sys = make_sys(4096, su_cfg);
        // U mode SRET -> illegal
        enter_mode(sys, PrivilegeLevel::USER, 0x100u);
        sys.memory.write32(0x100u, 0x10200073u);
        auto r = sys.step();
        CHECK(r.trap);
        CHECK_EQ(r.trap_cause, exception::ILLEGAL_INSTRUCTION);

        // S mode with TSR=1 -> illegal
        sys.cpu.reset();
        enter_mode(sys, PrivilegeLevel::SUPERVISOR, 0x100u);
        sys.cpu.csrs.write(zicsr::csr_addr::MSTATUS,
            zicsr::CSRFile::MSTATUS_TSR | zicsr::CSRFile::MSTATUS_SIE);
        sys.memory.write32(0x100u, 0x10200073u);
        r = sys.step();
        CHECK(r.trap);
        CHECK_EQ(r.trap_cause, exception::ILLEGAL_INSTRUCTION);

        // S mode with TSR=0 -> allowed (no MMU effect).
        sys.cpu.reset();
        enter_mode(sys, PrivilegeLevel::SUPERVISOR, 0x100u);
        sys.memory.write32(0x100u, 0x10200073u);
        r = sys.step();
        CHECK(!r.trap);
        PASS();
    }

    {
        TEST("WFI illegal in U mode and when mstatus.TW=1 in S mode");
        auto sys = make_sys(4096, su_cfg);
        // U mode WFI -> illegal
        enter_mode(sys, PrivilegeLevel::USER, 0x100u);
        sys.memory.write32(0x100u, 0x10500073u);
        auto r = sys.step();
        CHECK(r.trap);
        CHECK_EQ(r.trap_cause, exception::ILLEGAL_INSTRUCTION);

        // S mode with TW=1 -> illegal
        sys.cpu.reset();
        enter_mode(sys, PrivilegeLevel::SUPERVISOR, 0x100u);
        sys.cpu.csrs.write(zicsr::csr_addr::MSTATUS, zicsr::CSRFile::MSTATUS_TW);
        sys.memory.write32(0x100u, 0x10500073u);
        r = sys.step();
        CHECK(r.trap);
        CHECK_EQ(r.trap_cause, exception::ILLEGAL_INSTRUCTION);

        // S mode with TW=0 -> allowed.
        sys.cpu.reset();
        enter_mode(sys, PrivilegeLevel::SUPERVISOR, 0x100u);
        sys.memory.write32(0x100u, 0x10500073u);
        r = sys.step();
        CHECK(!r.trap);
        PASS();
    }

    {
        TEST("SFENCE.VMA allowed in M/S; illegal with TVM=1 in S mode");
        auto sys = make_sys(4096, su_cfg);
        // M mode -> allowed
        sys.memory.write32(sys.cpu.pc, 0x12000073u);
        CHECK(!sys.step().trap);

        // S mode, TVM=0 -> allowed
        enter_mode(sys, PrivilegeLevel::SUPERVISOR, 0x100u);
        sys.memory.write32(0x100u, 0x12000073u);
        auto r = sys.step();
        CHECK(!r.trap);

        // S mode, TVM=1 -> illegal
        sys.cpu.csrs.write(zicsr::csr_addr::MSTATUS, zicsr::CSRFile::MSTATUS_TVM);
        sys.memory.write32(sys.cpu.pc, 0x12000073u);
        r = sys.step();
        CHECK(r.trap);
        CHECK_EQ(r.trap_cause, exception::ILLEGAL_INSTRUCTION);
        PASS();
    }

    {
        TEST("S-mode interrupt delegation and stvec vectored delivery");
        auto sys = make_sys(4096, su_cfg);
        uint32_t s_base = 0x900u;
        sys.cpu.csrs.write(zicsr::csr_addr::STVEC, s_base | 1u); // vectored
        // Delegate supervisor software interrupt to S mode.
        sys.cpu.csrs.write(zicsr::csr_addr::MIDELEG, zicsr::CSRFile::MI_SSI);
        // Enter S mode with SIE enabled and SSIP pending.
        sys.cpu.csrs.write(zicsr::csr_addr::MIE, zicsr::CSRFile::MI_SSI);
        sys.cpu.csrs.write(zicsr::csr_addr::MSTATUS, zicsr::CSRFile::MSTATUS_SIE);
        enter_mode(sys, PrivilegeLevel::SUPERVISOR, 0x100u);
        sys.cpu.set_supervisor_software_interrupt(true);
        auto r = sys.step();
        CHECK(r.interrupt);
        CHECK_EQ(sys.cpu.csrs.get_privilege(), PrivilegeLevel::SUPERVISOR);
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::SCAUSE), exception::S_SOFTWARE_INTERRUPT);
        CHECK_EQ(sys.cpu.pc, s_base + 4 * 1);
        // SPP should be 1 because previous mode was S.
        CHECK(sys.cpu.csrs.read(zicsr::csr_addr::MSTATUS) & zicsr::CSRFile::MSTATUS_SPP);
        PASS();
    }

    {
        TEST("sip/sie are subsets visible via mideleg");
        auto sys = make_sys(4096, su_cfg);
        // Delegate SSI and STI to S mode; leave SEI undelegated.
        uint32_t deleg = zicsr::CSRFile::MI_SSI | zicsr::CSRFile::MI_STI;
        sys.cpu.csrs.write(zicsr::csr_addr::MIDELEG, deleg);
        sys.cpu.csrs.write(zicsr::csr_addr::MIE, zicsr::CSRFile::MI_MASK);
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::SIE), deleg);
        // Write to sie should only affect delegated bits.
        sys.cpu.csrs.write(zicsr::csr_addr::SIE, zicsr::CSRFile::MI_MASK);
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::MIE) & zicsr::CSRFile::MI_MASK,
                 zicsr::CSRFile::MI_MASK);
        // But only the delegated bits appear back in sie.
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::SIE), deleg);
        PASS();
    }

    {
        TEST("Counter access from U gated by mcounteren/scounteren");
        uint32_t u_pc = 0x100u;
        uint32_t inst = zicsr::encode::csrr(3, zicsr::csr_addr::CYCLE); // csrr x3, cycle

        // Case 1: mcounteren.cycle = 0 -> trap
        {
            auto sys = make_sys(4096, su_cfg);
            enter_mode(sys, PrivilegeLevel::USER, u_pc);
            sys.memory.write32(u_pc, inst);
            sys.cpu.csrs.write(zicsr::csr_addr::MCOUNTEREN, 0);
            auto r = sys.step();
            CHECK(r.trap);
            CHECK_EQ(r.trap_cause, exception::ILLEGAL_INSTRUCTION);
        }

        // Case 2: mcounteren.cycle = 1 but scounteren.cycle = 0 -> still trap in U
        {
            auto sys = make_sys(4096, su_cfg);
            enter_mode(sys, PrivilegeLevel::USER, u_pc);
            sys.memory.write32(u_pc, inst);
            sys.cpu.csrs.write(zicsr::csr_addr::MCOUNTEREN, 1u);
            sys.cpu.csrs.write(zicsr::csr_addr::SCOUNTEREN, 0);
            auto r = sys.step();
            CHECK(r.trap);
            CHECK_EQ(r.trap_cause, exception::ILLEGAL_INSTRUCTION);
        }

        // Case 3: both enabled -> success
        {
            auto sys = make_sys(4096, su_cfg);
            enter_mode(sys, PrivilegeLevel::USER, u_pc);
            sys.memory.write32(u_pc, inst);
            sys.cpu.csrs.write(zicsr::csr_addr::MCOUNTEREN, 1u);
            sys.cpu.csrs.write(zicsr::csr_addr::SCOUNTEREN, 1u);
            auto r = sys.step();
            CHECK(!r.trap);
        }
        PASS();
    }

    {
        TEST("Exceptions taken in M mode are never delegated to S");
        auto sys = make_sys(4096, su_cfg);
        sys.cpu.csrs.write(zicsr::csr_addr::MTVEC, 0x800u);
        sys.cpu.csrs.write(zicsr::csr_addr::STVEC, 0x900u);
        // Delegate breakpoint (cause 3) to S mode, then take it in M mode.
        sys.cpu.csrs.write(zicsr::csr_addr::MEDELEG, 1u << 3);
        sys.memory.write32(sys.cpu.pc, 0x00100073u);   // ebreak (M mode)
        auto r = sys.step();
        CHECK(r.trap);
        CHECK_EQ(r.trap_cause, exception::BREAKPOINT);
        CHECK_EQ(sys.cpu.csrs.get_privilege(), PrivilegeLevel::MACHINE);
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::MCAUSE), exception::BREAKPOINT);
        CHECK_EQ(sys.cpu.pc, 0x800u);
        // scause must remain untouched.
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::SCAUSE), 0u);
        PASS();
    }

    {
        TEST("medeleg bit 11 (ECALL-from-M) is read-only zero");
        auto sys = make_sys(4096, su_cfg);
        sys.cpu.csrs.write(zicsr::csr_addr::MEDELEG, 0xFFFFu);
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::MEDELEG) & (1u << 11), 0u);
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::MEDELEG), 0xF7FFu);
        PASS();
    }

    {
        TEST("MRET sets MPP to the least-privileged supported mode");
        auto sys = make_sys(4096, su_cfg);   // U-mode supported
        enter_mode(sys, PrivilegeLevel::MACHINE, 0x100u);
        CHECK_EQ((sys.cpu.csrs.read(zicsr::csr_addr::MSTATUS) >> 11) & 3u, 0u); // U

        CPUConfig m_only;                     // M-only: least privileged is M
        auto sys2 = make_sys(4096, m_only);
        sys2.cpu.csrs.write(zicsr::csr_addr::MEPC, 0x100u);
        sys2.cpu.csrs.set(zicsr::csr_addr::MSTATUS, zicsr::CSRFile::MSTATUS_MPIE);
        sys2.memory.write32(sys2.cpu.pc, 0x30200073u);   // mret
        CHECK(!sys2.step().trap);
        CHECK_EQ((sys2.cpu.csrs.read(zicsr::csr_addr::MSTATUS) >> 11) & 3u, 3u); // M
        PASS();
    }

    {
        TEST("Interrupt priority order: MEI > MSI > MTI and SEI > SSI");
        const uint32_t NOP = 0x00000013u;

        // M-mode priorities (nothing delegated): MEI, MSI, MTI all pending.
        {
            auto sys = make_sys(4096, su_cfg);
            sys.cpu.csrs.write(zicsr::csr_addr::MTVEC, 0x800u);
            sys.cpu.csrs.write(zicsr::csr_addr::MIE, zicsr::CSRFile::MI_MASK);
            sys.cpu.csrs.write(zicsr::csr_addr::MSTATUS, zicsr::CSRFile::MSTATUS_MIE);
            sys.cpu.set_external_interrupt(true);
            sys.cpu.set_software_interrupt(true);
            sys.cpu.set_timer_interrupt(true);

            const struct { uint32_t cause; int line; } order[] = {
                { 11u, 0 },  // MEI
                { 3u,  1 },  // MSI
                { 7u,  2 },  // MTI
            };
            for (auto& o : order) {
                auto r = sys.step();
                CHECK(r.interrupt);
                CHECK_EQ(r.trap_cause, exception::INTERRUPT_BIT | o.cause);
                // Trap entry cleared MIE; clear the line and re-enable.
                if (o.line == 0) sys.cpu.set_external_interrupt(false);
                if (o.line == 1) sys.cpu.set_software_interrupt(false);
                if (o.line == 2) sys.cpu.set_timer_interrupt(false);
                uint32_t ms = sys.cpu.csrs.get(zicsr::csr_addr::MSTATUS);
                sys.cpu.csrs.set(zicsr::csr_addr::MSTATUS,
                                 ms | zicsr::CSRFile::MSTATUS_MIE);
            }
        }

        // S-mode priorities: delegate SEI and SSI, pend both -> SEI first.
        {
            auto sys = make_sys(4096, su_cfg);
            sys.cpu.csrs.write(zicsr::csr_addr::STVEC, 0x900u);
            sys.cpu.csrs.write(zicsr::csr_addr::MIDELEG,
                               zicsr::CSRFile::MI_SEI | zicsr::CSRFile::MI_SSI);
            sys.cpu.csrs.write(zicsr::csr_addr::MIE,
                               zicsr::CSRFile::MI_SEI | zicsr::CSRFile::MI_SSI);
            sys.cpu.csrs.write(zicsr::csr_addr::MSTATUS, zicsr::CSRFile::MSTATUS_SIE);
            enter_mode(sys, PrivilegeLevel::SUPERVISOR, 0x100u);
            sys.cpu.set_supervisor_external_interrupt(true);
            sys.cpu.set_supervisor_software_interrupt(true);

            auto r = sys.step();
            CHECK(r.interrupt);
            CHECK_EQ(r.trap_cause, exception::S_EXTERNAL_INTERRUPT);

            sys.cpu.set_supervisor_external_interrupt(false);
            uint32_t ms = sys.cpu.csrs.get(zicsr::csr_addr::MSTATUS);
            sys.cpu.csrs.set(zicsr::csr_addr::MSTATUS,
                             ms | zicsr::CSRFile::MSTATUS_SIE);
            r = sys.step();
            CHECK(r.interrupt);
            CHECK_EQ(r.trap_cause, exception::S_SOFTWARE_INTERRUPT);
        }
        (void)NOP;
        PASS();
    }

    {
        TEST("M-mode interrupt is taken while executing in S mode (MIE=0)");
        auto sys = make_sys(4096, su_cfg);
        sys.cpu.csrs.write(zicsr::csr_addr::MTVEC, 0x800u);
        sys.cpu.csrs.write(zicsr::csr_addr::MIE, zicsr::CSRFile::MI_MTI);
        // Global MIE stays 0: interrupts targeting a more privileged mode
        // are taken regardless of mstatus.MIE.
        enter_mode(sys, PrivilegeLevel::SUPERVISOR, 0x100u);
        sys.cpu.set_timer_interrupt(true);
        auto r = sys.step();
        CHECK(r.interrupt);
        CHECK_EQ(r.trap_cause, exception::M_TIMER_INTERRUPT);
        CHECK_EQ(sys.cpu.csrs.get_privilege(), PrivilegeLevel::MACHINE);
        CHECK_EQ(sys.cpu.pc, 0x800u);
        // MPP records the mode the interrupt was taken from.
        CHECK_EQ((sys.cpu.csrs.read(zicsr::csr_addr::MSTATUS) >> 11) & 3u, 1u);
        PASS();
    }

    {
        TEST("S-delegated interrupt is not taken while in M mode");
        auto sys = make_sys(4096, su_cfg);
        sys.cpu.csrs.write(zicsr::csr_addr::MTVEC, 0x800u);
        sys.cpu.csrs.write(zicsr::csr_addr::MIDELEG, zicsr::CSRFile::MI_SSI);
        sys.cpu.csrs.write(zicsr::csr_addr::MIE, zicsr::CSRFile::MI_SSI);
        sys.cpu.csrs.write(zicsr::csr_addr::MSTATUS, zicsr::CSRFile::MSTATUS_MIE);
        sys.cpu.set_supervisor_software_interrupt(true);
        sys.memory.write32(sys.cpu.pc, 0x00000013u);   // nop
        auto r = sys.step();
        CHECK(!r.interrupt);
        CHECK(!r.trap);
        CHECK_EQ(sys.cpu.pc, 4u);   // nop retired normally
        PASS();
    }

    {
        TEST("S interrupt in S mode gated by mstatus.SIE");
        auto sys = make_sys(4096, su_cfg);
        sys.cpu.csrs.write(zicsr::csr_addr::STVEC, 0x900u);
        sys.cpu.csrs.write(zicsr::csr_addr::MIDELEG, zicsr::CSRFile::MI_SSI);
        sys.cpu.csrs.write(zicsr::csr_addr::MIE, zicsr::CSRFile::MI_SSI);
        enter_mode(sys, PrivilegeLevel::SUPERVISOR, 0x100u);  // SIE = 0
        sys.memory.write32(0x100u, 0x00000013u);   // nop
        sys.cpu.set_supervisor_software_interrupt(true);
        auto r = sys.step();
        CHECK(!r.interrupt);
        CHECK(!r.trap);
        CHECK_EQ(sys.cpu.pc, 0x104u);
        // Enable SIE: the pending interrupt is now taken.
        uint32_t ms = sys.cpu.csrs.get(zicsr::csr_addr::MSTATUS);
        sys.cpu.csrs.set(zicsr::csr_addr::MSTATUS, ms | zicsr::CSRFile::MSTATUS_SIE);
        r = sys.step();
        CHECK(r.interrupt);
        CHECK_EQ(r.trap_cause, exception::S_SOFTWARE_INTERRUPT);
        CHECK_EQ(sys.cpu.pc, 0x900u);
        PASS();
    }

    {
        TEST("sstatus reads/writes only the S-visible mstatus subset");
        auto sys = make_sys(4096, su_cfg);
        uint32_t s_bits = zicsr::CSRFile::MSTATUS_SIE  |
                          zicsr::CSRFile::MSTATUS_SPIE |
                          zicsr::CSRFile::MSTATUS_SPP  |
                          zicsr::CSRFile::MSTATUS_SUM  |
                          zicsr::CSRFile::MSTATUS_MXR;
        sys.cpu.csrs.write(zicsr::csr_addr::MSTATUS,
                           s_bits | zicsr::CSRFile::MSTATUS_MIE |
                           zicsr::CSRFile::MSTATUS_TVM);
        // Read subset: S bits plus hardwired FS/SD (F extension enabled).
        CHECK_EQ(sys.cpu.csrs.read(zicsr::csr_addr::SSTATUS),
                 s_bits | zicsr::CSRFile::MSTATUS_FS | zicsr::CSRFile::MSTATUS_SD);
        // Write subset: only SIE/SPIE/SPP/SUM/MXR are affected.
        sys.cpu.csrs.write(zicsr::csr_addr::SSTATUS, zicsr::CSRFile::MSTATUS_SPP);
        uint32_t ms = sys.cpu.csrs.read(zicsr::csr_addr::MSTATUS);
        CHECK(ms & zicsr::CSRFile::MSTATUS_SPP);
        CHECK(!(ms & zicsr::CSRFile::MSTATUS_SIE));
        CHECK(!(ms & zicsr::CSRFile::MSTATUS_SPIE));
        CHECK(!(ms & zicsr::CSRFile::MSTATUS_SUM));
        CHECK(!(ms & zicsr::CSRFile::MSTATUS_MXR));
        // Non-subset fields are untouched by the sstatus write.
        CHECK(ms & zicsr::CSRFile::MSTATUS_MIE);
        CHECK(ms & zicsr::CSRFile::MSTATUS_TVM);
        PASS();
    }

    {
        TEST("SATP access from S mode traps when mstatus.TVM=1");
        auto sys = make_sys(4096, su_cfg);
        uint32_t csrr_satp = zicsr::encode::csrr(3, zicsr::csr_addr::SATP);

        // M mode with TVM=1: allowed (TVM only affects S mode).
        sys.cpu.csrs.write(zicsr::csr_addr::MSTATUS, zicsr::CSRFile::MSTATUS_TVM);
        sys.memory.write32(sys.cpu.pc, csrr_satp);
        CHECK(!sys.step().trap);

        // S mode with TVM=0: allowed.
        enter_mode(sys, PrivilegeLevel::SUPERVISOR, 0x100u);
        sys.cpu.csrs.write(zicsr::csr_addr::MSTATUS, 0);   // clear TVM from step 1
        sys.memory.write32(0x100u, csrr_satp);
        CHECK(!sys.step().trap);

        // S mode with TVM=1: illegal instruction.
        sys.cpu.csrs.write(zicsr::csr_addr::MSTATUS, zicsr::CSRFile::MSTATUS_TVM);
        sys.memory.write32(sys.cpu.pc, csrr_satp);
        auto r = sys.step();
        CHECK(r.trap);
        CHECK_EQ(r.trap_cause, exception::ILLEGAL_INSTRUCTION);
        PASS();
    }

    {
        TEST("MRET is illegal outside M mode");
        auto sys = make_sys(4096, su_cfg);
        // U mode.
        enter_mode(sys, PrivilegeLevel::USER, 0x100u);
        sys.memory.write32(0x100u, 0x30200073u);   // mret
        auto r = sys.step();
        CHECK(r.trap);
        CHECK_EQ(r.trap_cause, exception::ILLEGAL_INSTRUCTION);

        // S mode.
        sys.cpu.reset();
        enter_mode(sys, PrivilegeLevel::SUPERVISOR, 0x100u);
        sys.memory.write32(0x100u, 0x30200073u);
        r = sys.step();
        CHECK(r.trap);
        CHECK_EQ(r.trap_cause, exception::ILLEGAL_INSTRUCTION);
        PASS();
    }
}

// ============================================================================
// L. Opcode-injection stepping (step(bus, opcode))
// ============================================================================

// Bus that counts fetch accesses, to prove injection performs none.
class FetchCountingBus : public SimpleMemory {
public:
    int fetches = 0;
    using SimpleMemory::SimpleMemory;
    uint16_t fetch16(uint32_t addr) override {
        fetches++;
        return SimpleMemory::fetch16(addr);
    }
    uint32_t fetch32(uint32_t addr) override {
        fetches++;
        return SimpleMemory::fetch32(addr);
    }
};

static void test_step_opcode_injection() {
    printf("L. Opcode-injection stepping\n");

    {
        TEST("Injected 32-bit opcode executes without any bus fetch");
        FetchCountingBus bus(4096);
        CPU cpu;
        auto r = cpu.step(bus, 0x00500093u);   // addi x1, x0, 5
        CHECK(!r.trap && !r.interrupt);
        CHECK_EQ(r.instr_size, 4u);
        CHECK(r.mnemonic == "addi ra, zero, 5");
        CHECK_EQ(cpu.regs.read(1), 5u);
        CHECK_EQ(cpu.pc, 4u);
        CHECK_EQ(bus.fetches, 0);
        PASS();
    }
    {
        TEST("Injected compressed opcode executes with instr_size 2");
        FetchCountingBus bus(4096);
        CPU cpu;
        auto r = cpu.step(bus, 0x0089u);   // c.addi x1, 2  (funct3=000,q1)
        CHECK(!r.trap);
        CHECK_EQ(r.instr_size, 2u);
        CHECK_EQ(cpu.regs.read(1), 2u);
        CHECK_EQ(cpu.pc, 2u);
        CHECK_EQ(bus.fetches, 0);
        PASS();
    }
    {
        TEST("Injected load still uses the bus for data access");
        FetchCountingBus bus(4096);
        CPU cpu;
        cpu.regs.write(1, 0x100);
        bus.write32(0x100, 0xCAFEBABE);
        auto r = cpu.step(bus, 0x0000A103u);   // lw x2, 0(x1)
        CHECK(!r.trap);
        CHECK_EQ(cpu.regs.read(2), 0xCAFEBABEu);
        CHECK_EQ(bus.fetches, 0);
        PASS();
    }
    {
        TEST("Injected ecall traps to mtvec like a fetched one");
        FetchCountingBus bus(4096);
        CPU cpu;
        cpu.csrs.write(zicsr::csr_addr::MTVEC, 0x800u);
        auto r = cpu.step(bus, 0x00000073u);   // ecall
        CHECK(r.trap);
        CHECK_EQ(r.trap_cause, exception::ECALL_FROM_M);
        CHECK_EQ(cpu.pc, 0x800u);
        CHECK_EQ(bus.fetches, 0);
        PASS();
    }
    {
        TEST("Interrupts are still sampled before injected execution");
        FetchCountingBus bus(4096);
        CPU cpu;
        cpu.csrs.write(zicsr::csr_addr::MTVEC, 0x800u);
        cpu.csrs.write(zicsr::csr_addr::MSTATUS, zicsr::CSRFile::MSTATUS_MIE);
        cpu.csrs.write(zicsr::csr_addr::MIE, zicsr::CSRFile::MI_MTI);
        cpu.set_timer_interrupt(true);
        auto r = cpu.step(bus, 0x00500093u);   // addi must NOT execute
        CHECK(r.interrupt);
        CHECK_EQ(r.trap_cause, exception::M_TIMER_INTERRUPT);
        CHECK_EQ(cpu.pc, 0x800u);
        CHECK_EQ(cpu.regs.read(1), 0u);
        CHECK_EQ(bus.fetches, 0);
        PASS();
    }
    {
        TEST("Injected stream matches fetched execution state-for-state");
        const uint32_t prog[] = {
            0x00500093u,   // addi x1, x0, 5
            0x00608113u,   // addi x2, x1, 6
            0x002101B3u,   // add  x3, x2, x2
        };
        System sys(4096);
        sys.load_program(0, prog, 3);
        sys.run(3);

        FetchCountingBus bus(4096);
        CPU cpu;
        for (uint32_t op : prog) cpu.step(bus, op);

        CHECK_EQ(cpu.regs.read(1), sys.cpu.regs.read(1));
        CHECK_EQ(cpu.regs.read(2), sys.cpu.regs.read(2));
        CHECK_EQ(cpu.regs.read(3), sys.cpu.regs.read(3));
        CHECK_EQ(cpu.pc, sys.cpu.pc);
        CHECK_EQ(cpu.csrs.get(zicsr::csr_addr::MINSTRET),
                 sys.cpu.csrs.get(zicsr::csr_addr::MINSTRET));
        CHECK_EQ(bus.fetches, 0);
        PASS();
    }
}

// ============================================================================
// main
// ============================================================================

int main() {
    printf("RISC-V reference model test suite\n");
    printf("==================================\n");
    
    test_common();
    test_rv32i_decode();
    test_rv32i_exec();
    test_rv32c();
    test_rv32a();
    test_bitmanip_zicond();
    test_rv32f();
    test_rv32fc();
    test_zicsr();
    test_traps_interrupts();
    test_supervisor_user_mode();
    test_integration();
    test_step_opcode_injection();

    printf("==================================\n");
    printf("%d tests, %d failures\n", g_tests, g_failures);
    return g_failures == 0 ? 0 : 1;
}
