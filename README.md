# RV32 RISC-V Reference Model

A header-only C++17 reference (golden) model of a 32-bit RISC-V hart,
covering **RV32IMAFC** plus the **Zicsr, Zifencei, Zba, Zbb, Zbs and
Zicond** extensions. It implements the privileged architecture (machine
mode with optional supervisor and user modes, trap delegation, M- and
S-mode interrupts, MRET/SRET) and is intended as a bit-accurate
executable specification for RTL verification, toolchain experiments and
hardware design exploration.

The model is organized as one decoder/executor module per extension,
composed by a top-level `CPU` class. All memory access goes through an
injected `Bus` interface; bus errors become architectural access-fault
exceptions. For each extension there is also a matching random opcode
generator (`*_opgen.hpp`) used as test stimulus, and everything is tied
together by a self-contained test suite (`test_riscv_model.cpp`) that
runs warning- and UBSan-clean.

## Building and testing

```bash
make test     # builds into build/ and runs the test suite
make clean    # removes build/
```

Toolchain: `g++ -std=c++17 -Wall -Wextra -Wpedantic -fsanitize=undefined`.
Being header-only, the model needs no build step of its own — just add
the directory to your include path and `#include "riscv_cpu.hpp"`.

## Implemented ISA

### Unprivileged instruction set

| Extension | Instructions |
|---|---|
| **RV32I** | All 40 base instructions: LUI, AUIPC, JAL, JALR, BEQ/BNE/BLT/BGE/BLTU/BGEU, LB/LH/LW/LBU/LHU, SB/SH/SW, ADDI/SLTI/SLTIU/XORI/ORI/ANDI, SLLI/SRLI/SRAI, ADD/SUB/SLL/SLT/SLTU/XOR/SRL/SRA/OR/AND, FENCE, ECALL, EBREAK. |
| **M** | MUL, MULH, MULHSU, MULHU, DIV, DIVU, REM, REMU. |
| **A** | LR.W, SC.W (with reservation tracking; the reservation is cleared on any taken trap, per the spec) and all nine AMOs: AMOSWAP.W, AMOADD.W, AMOXOR.W, AMOAND.W, AMOOR.W, AMOMIN.W, AMOMAX.W, AMOMINU.W, AMOMAXU.W. aq/rl ordering bits are decoded (no observable effect in a single-hart model). |
| **F** | FLW, FSW, FADD.S, FSUB.S, FMUL.S, FDIV.S, FSQRT.S, FMIN.S, FMAX.S, FMADD.S, FMSUB.S, FNMADD.S, FNMSUB.S, FCVT.W.S, FCVT.WU.S, FCVT.S.W, FCVT.S.WU, FMV.X.W, FMV.W.X, FEQ.S, FLT.S, FLE.S, FSGNJ.S, FSGNJN.S, FSGNJX.S, FCLASS.S, with `fflags`/`frm`/`fcsr` interaction. |
| **C** | All 16-bit compressed instructions (quadrants 0–2): C.ADDI4SPN, C.LW, C.SW, C.NOP, C.ADDI, C.JAL, C.LI, C.ADDI16SP, C.LUI, C.SRLI, C.SRAI, C.ANDI, C.SUB, C.XOR, C.OR, C.AND, C.J, C.BEQZ, C.BNEZ, C.SLLI, C.LWSP, C.JR, C.MV, C.EBREAK, C.JALR, C.ADD, C.SWSP. |
| **Zcf** | C.FLW, C.FSW, C.FLWSP, C.FSWSP (compressed FP loads/stores, require F+C). |
| **Zicsr** | CSRRW, CSRRS, CSRRC, CSRRWI, CSRRSI, CSRRCI plus the full CSR file (privileged spec v1.12). |
| **Zifencei** | FENCE.I (tracked; effectively a no-op in a cache-less software model). |
| **Zba** | SH1ADD, SH2ADD, SH3ADD. |
| **Zbb** | ANDN, ORN, XNOR, CLZ, CTZ, CPOP, MAX, MAXU, MIN, MINU, SEXT.B, SEXT.H, ZEXT.H, ROL, ROR, RORI, ORC.B, REV8. |
| **Zbs** | BSET, BSETI, BCLR, BCLRI, BINV, BINVI, BEXT, BEXTI. |
| **Zicond** | CZERO.EQZ, CZERO.NEZ. |

### Privileged architecture

- **Privilege modes**: Machine mode always; Supervisor and User modes
  optional (`CPUConfig::enable_s_mode` / `enable_u_mode`).
- **Privileged instructions**: MRET, SRET, SFENCE.VMA, WFI — with the
  architectural legality checks (MRET requires M-mode; SRET blocked in
  U-mode and by `mstatus.TSR`; WFI blocked in U-mode and by `mstatus.TW`;
  SFENCE.VMA and `satp` access blocked in U-mode and by `mstatus.TVM`).
- **Exceptions**: synchronous exceptions update `xepc`/`xcause`/`xtval`/
  `xstatus` and redirect to `mtvec`/`stvec` (always the BASE address, even
  in vectored mode); execution continues in the handler. `mtval` is
  precise (faulting address or faulting instruction bits; partial-instruction
  fetch faults report the faulting parcel address).
- **Trap delegation**: `medeleg`/`mideleg` route traps from S/U to S-mode;
  traps taken in M-mode are never delegated. `medeleg[11]` is read-only
  zero per the spec.
- **Interrupts**: MEI/MSI/MTI (M-mode) and SEI/SSI/STI (S-mode), level-
  sensitive input lines driving `mip`, gated by `mie`/`mstatus.xIE` with
  the architectural global-enable rules, standard priority order
  MEI > MSI > MTI > SEI > SSI > STI within a target privilege (interrupts
  targeting M-mode take precedence over any targeting S-mode), and
  vectored `mtvec`/`stvec` support.
- **CSR file**: machine/supervisor/user CSRs per privileged spec v1.12 —
  trap setup and handling, counters (`mcycle`/`minstret` with read-only
  `cycle`/`instret` shadows; `time` aliases `mcycle`), delegation and
  counter enables (`mcounteren`/`scounteren`), `sstatus`/`sie`/`sip` as
  subsets of their M-mode counterparts, `satp` (stored; no translation),
  `misa` reflecting the configured extensions, and the floating-point CSRs
  (`fflags`/`frm` as aliases into a single `fcsr` cell). WARL fields are
  legalized on write; reserved/illegal encodings trap.

### Deliberately not implemented

- **Sv32 virtual memory / MMU** — `satp` values are stored but do not
  affect address translation; all accesses are physical.
- **PMP** — no `pmpcfg*`/`pmpaddr*` CSRs.
- **U-mode trap delegation** (`sedeleg`/`sideleg`) — traps from U-mode go
  to M or S.
- **Hypervisor extension**, **Debug mode**, **N extension** (removed from
  the ratified spec).

### Known approximations

- **RMM rounding** (round-to-nearest, ties to max magnitude) is
  approximated with RNE for FP arithmetic, because host FPUs lack the
  mode; the two differ only on exact ties. Integer conversions implement
  all five rounding modes exactly. All NaN results are canonicalized to
  `0x7FC00000`.
- **`mstatus.FS`** is hardwired to Dirty (and `SD` set) when the F
  extension is enabled — a legal WARL choice.
- **`mstatush`/`menvcfg`/`menvcfgh`** are implemented as WARL all-zero
  (little-endian, no environment-config features).

## File overview

### Core model

| File | Description |
|---|---|
| `riscv_common.hpp` | Shared foundation: `Bus` interface, `BusFault`, `RegFile`, `SimpleMemory` helper, bit-field utilities, address checking, common exceptions. Used by every extension module. |
| `riscv_cpu.hpp` | Top-level `CPU` integrating all extensions, plus a `System` convenience wrapper (CPU + memory). Handles dispatch, synchronous exceptions and interrupts (M/S/U modes, `medeleg`/`mideleg`, vectored `mtvec`, precise `mtval`), MRET/SRET, misalignment policy, and 16-bit-granular instruction fetch (no over-fetch past the last compressed parcel). |
| `riscv_rv32i.hpp` | RV32I base integer set: decoder/executor for all 40 instructions (incl. ECALL/EBREAK/MRET/SRET/SFENCE.VMA/WFI decoding hooks, FENCE, loads/stores, jumps). |
| `riscv_rv32m.hpp` | M extension: the 8 multiply/divide instructions. |
| `riscv_rv32a.hpp` | A extension: LR.W/SC.W with reservation tracking and all nine AMOs, incl. aq/rl handling and misalignment trapping. |
| `riscv_rv32f.hpp` | F extension: single-precision floating point — loads/stores, arithmetic, fused multiply-add, comparisons, conversions, sign injection, classification, and `fflags`/`frm`/`fcsr` interaction. |
| `riscv_rv32fc.hpp` | Zcf: compressed floating-point loads/stores (C.FLW/C.FSW/C.FLWSP/C.FSWSP). |
| `riscv_rv32c.hpp` | C extension: decoder/executor for all 16-bit compressed instructions. |
| `riscv_zicsr.hpp` | Zicsr: CSR instructions plus the full CSR file (privileged spec v1.12) — machine/supervisor/user CSRs, counters, trap state, delegation, `satp`, floating-point CSRs. |
| `riscv_zifencei.hpp` | Zifencei: FENCE.I (tracked for correctness; effectively a no-op in a cache-less software model). |
| `riscv_zba.hpp` | Zba bit-manipulation: shifted-add address-generation instructions (SH1ADD–SH3ADD). |
| `riscv_zbb.hpp` | Zbb bit-manipulation: logic-with-negate, min/max, clz/ctz/cpop, rotations, sign/zero extension, byte swaps, orc.b, rev8. |
| `riscv_zbs.hpp` | Zbs bit-manipulation: single-bit set/clear/invert/extract, register and immediate forms. |
| `riscv_zicond.hpp` | Zicond: conditional-zero instructions (CZERO.EQZ/CZERO.NEZ) for branchless selection. |

### Test stimulus generators (one per extension)

Each `riscv_<ext>_opgen.hpp` generates random *valid* opcodes for its
extension (all fields randomized within legal bounds), used by the test
suite for randomized round-trip and execution stimulus. Every generator
supports a **per-instruction-type enable mask** (`set_enabled_mask()`,
`enable()`, with named `groups::` constants like `groups::LOADS` or
`groups::FMA`) so stimulus can be restricted during bring-up (generate
only what the implementation already supports) or debugging (focus on a
few instruction groups). The default (all-enabled) is seed-stable, an
empty mask is legalized back to all-enabled, and auxiliary selectors
(`generate_alu()`, `generate_memory()`, ...) honor the mask with
fallback to the enabled set:
`riscv_rv32i_opgen.hpp`, `riscv_rv32m_opgen.hpp`, `riscv_rv32a_opgen.hpp`,
`riscv_rv32c_opgen.hpp`, `riscv_rv32f_opgen.hpp`, `riscv_rv32fc_opgen.hpp`,
`riscv_zicsr_opgen.hpp`, `riscv_zifencei_opgen.hpp`, `riscv_zba_opgen.hpp`,
`riscv_zbb_opgen.hpp`, `riscv_zbs_opgen.hpp`, `riscv_zicond_opgen.hpp`.
Notes: the Zicsr generator targets only existing CSRs — its CSR pools
grow when `set_s_mode()`/`set_u_mode()` are enabled to match the CPU
configuration (keeping stimulus trap-free); the Zifencei generator
emits the canonical FENCE.I unless `set_standard_only(false)` is used to
randomize the spec-ignored rd/rs1/imm fields; and the RV32C generator
emits only canonical (non-HINT) encodings from its type table — the
valid NOP-semantics HINT space (rd = x0 forms, shift-by-zero forms) is
available separately via `generate_hint()` / `generate_mixed()`.
See [Opcode generators (test stimulus)](#opcode-generators-test-stimulus)
for a full usage guide, including enable masks and groups.

Complementing the valid-opcode generators, `riscv_illegal_opgen.hpp`
generates *invalid* encodings derived from the specification's encoding
tables (canonical illegal patterns, reserved major opcodes, per-family
constraint violations, compressed reserved encodings) for checking that
an implementation rejects every one of them with a precise
illegal-instruction exception and no side effects. HINT encodings are
deliberately excluded — they are valid NOPs.

### Tests and docs

| File | Description |
|---|---|
| `test_riscv_model.cpp` | The test suite: regression tests for every fixed bug, full-CPU integration tests (dispatch, traps, interrupts, MRET/SRET, misalignment, access faults, interrupt prioritization/gating, `sstatus` subset, SATP/TVM, MRET legality), opcode-generator round trips for all extensions, opgen coverage tests (every instruction type generated, operand fields spanning their full valid ranges, CSR address pools), and illegal-opcode stimulus (all constructed invalid encodings trap precisely and side-effect-free, exhaustive 16-bit parcel sweep with HINT whitelist, disabled-extension config matrix). Minimal built-in harness (TEST/CHECK macros), no external framework. |
| `Makefile` | Builds the test suite into `build/` and runs it (`make test`). |
| [AGENTS.md](AGENTS.md) | Project conventions for AI coding agents (layout, build/test rules). |
| [notes.md](doc/notes.md) | Design Q&A notes on RISC-V hardware implementation: extension priorities for cache-less systems, dual-issue of compressed instructions, and serial execution in debug mode. |
| [riscv_csr_reference.md](doc/riscv_csr_reference.md) | CSR reference: every implemented CSR, its purpose and read/write behavior, `mstatus` field semantics, and the exact CSR state changes on exceptions, interrupts, MRET/SRET and reset. |
| [riscv_s_and_u_modes_implementation.md](doc/riscv_s_and_u_modes_implementation.md) | Session log: plan and implementation of the S/U-mode support. |
| [riscv_s_u_mode_review_fixes_and_tests.md](doc/riscv_s_u_mode_review_fixes_and_tests.md) | Session log: review, spec fixes and test expansion for the S/U-mode work. |
| [rv32_model_review_spec_fixes_and_opgen_tooling.md](doc/rv32_model_review_spec_fixes_and_opgen_tooling.md) | Session log: full-model review (seven spec fixes found and fixed), and the stimulus-tooling build-out — illegal-opcode generator, HINT generators, opgen coverage audits, and per-group enable masks. |
| `LICENSE` | MIT license. |
| `.gitignore` | Keeps `build/` artifacts out of version control. |

## Using the model

### Quick start with `System`

`System` bundles a `CPU` with a `SimpleMemory` (1 MiB at address 0 by
default) and is the easiest way to run code:

```cpp
#include "riscv_cpu.hpp"
using namespace riscv;

CPUConfig cfg;
cfg.enable_s_mode = true;     // optional Supervisor mode
cfg.enable_u_mode = true;     // optional User mode
cfg.reset_vector  = 0x0000;   // initial PC (default 0)
cfg.mtvec_reset   = 0x0000;   // initial mtvec

System sys(64 * 1024, cfg);   // 64 KiB of memory

uint32_t program[] = { 0x00100093, /* addi ra, zero, 1 */ ... };
sys.load_program(0, program, sizeof(program) / sizeof(program[0]));

sys.run(1000);                // run up to 1000 steps
```

`load_program` also accepts a byte buffer, e.g. an ELF-extracted segment.

### Stepping and inspecting state

```cpp
CPUExecResult r = sys.step();
// r.pc, r.next_pc, r.instruction, r.instr_size (2/4, 0 on interrupt),
// r.branch_taken, r.trap, r.interrupt, r.trap_cause, r.trap_value,
// r.trap_info, r.mnemonic

uint32_t x1 = sys.cpu.regs.read(1);               // integer registers
float    f0 = sys.cpu.fregs.read(0);              // FP registers
uint32_t mstatus = sys.cpu.csrs.get(zicsr::csr_addr::MSTATUS);
```

- `step(Bus&, uint32_t opcode)` executes a caller-supplied opcode
  instead of fetching — see [Opcode-injection
  stepping](#opcode-injection-stepping).
- Handled traps redirect to `xTVEC` and execution **continues** in the
  handler; pass `stop_on_trap = true` to `run()` to stop at the first
  trap/interrupt instead.
- `run()` counts *steps* (retired + trapped instructions + interrupt
  deliveries), and its default bound is effectively infinite — pass a
  finite limit when executing untrusted stimulus.
- `mcycle` counts steps; `minstret` counts retired instructions only.

### Opcode-injection stepping

`step(Bus&, uint32_t opcode)` executes a caller-supplied opcode —
compressed or 32-bit, selected by the two low bits as usual — instead
of fetching from the bus. This is the primary interface for
cosimulation, trace replay and directed stimulus:

```cpp
CPU cpu(cfg);
SimpleMemory mem(64 * 1024);
cpu.pc = 0x40;

CPUExecResult r = cpu.step(mem, 0x003100B3);   // add x1, x2, x3
// r.pc == 0x40, r.next_pc == 0x44, r.instr_size == 4, r.mnemonic == "add ..."

r = cpu.step(mem, 0x9001);                     // 16-bit parcel -> compressed
// r.instr_size == 2
```

The contract:

- **No fetch happens**: no instruction access faults and no fetch side
  effects; the caller keeps `pc` coherent with the injected stream
  (the model still advances `pc` from `next_pc` as usual).
- **Interrupts are still sampled first** (an interrupt delivery
  executes *no* instruction: `r.interrupt`, `r.instr_size == 0`).
- **The bus is still used for data loads/stores**, so misaligned and
  access-fault traps occur exactly as for fetched execution, with
  precise `mtval`.
- Everything else — decode, execution, exceptions with precise
  `xepc`/`xcause`/`xtval`, delegation, xRET, counters — is identical
  to fetched execution; see `L. Opcode-injection stepping` in
  `test_riscv_model.cpp` for a state-for-state equivalence test.

### Custom memory and peripherals: the `Bus` interface

All loads, stores and fetches go through an injected `Bus`. Implement it
to attach your own memory map; signal errors by throwing
`riscv::BusFault` (or any `std::exception`) — the CPU converts it into the
correct access-fault exception with precise `mtval`:

```cpp
class MyBus : public riscv::Bus {
public:
    uint8_t  read8 (uint32_t addr) override { ... }
    void     write8(uint32_t addr, uint8_t data) override { ... }
    uint16_t read16(uint32_t addr) override { ... }   // also read32/write32
    ...
    // Optionally override fetch16/fetch32 if fetch differs from load.
};

MyBus bus;
CPU cpu(cfg);
uint64_t steps = cpu.run(bus, 1'000'000);
```

### Driving interrupts

Interrupt lines are level-sensitive and drive `mip` directly:

```cpp
cpu.set_timer_interrupt(true);                  // MTI
cpu.set_external_interrupt(true);               // MEI
cpu.set_software_interrupt(true);               // MSI
cpu.set_supervisor_timer_interrupt(true);       // STI (needs S-mode)
// ... also set_supervisor_external_interrupt / set_supervisor_software_interrupt
```

### Configuration reference (`CPUConfig`)

| Option | Default | Effect |
|---|---|---|
| `enable_m/a/f/c_extension` | `true` | Enable the corresponding ISA extension |
| `enable_zicsr` / `enable_zifencei` | `true` | CSR instructions / FENCE.I |
| `enable_zba` / `enable_zbb` / `enable_zbs` / `enable_zicond` | `true` | Bit-manipulation / conditional-zero extensions |
| `enable_s_mode` / `enable_u_mode` | `false` | Add Supervisor / User privilege modes (sets `misa.S`/`misa.U`) |
| `allow_misaligned_data` | `false` | `false`: misaligned data accesses trap |
| `reset_vector` | `0` | Initial PC |
| `mtvec_reset` | `0` | Initial `mtvec` |

Extension enables and anything reflected in CSRs (`misa`, FS/SD, `mepc`
mask) take effect at `reset()`; `allow_misaligned_data` can be toggled
between `step()` calls.

### Disassembly

```cpp
std::string s = sys.disasm(0x40);   // never throws; "<fetch fault>" if unreadable
```

## Opcode generators (test stimulus)

One generator per extension (`riscv_<ext>_opgen.hpp`, namespaces
`riscv::<ext>::opgen`) produces random *valid* opcodes with every field
drawn independently and uniformly within its legal range; a spec-table-
derived *illegal* generator (`riscv_illegal_opgen.hpp`) produces
encodings that must be rejected. All generators share the same API
shape and the enable-mask configuration.

### Basic usage

```cpp
#include "riscv_rv32i_opgen.hpp"
using namespace riscv;

rv32i::opgen::OpcodeGenerator gen(42);        // seed for reproducibility
gen.seed(1234);                               // reseed any time

uint32_t op  = gen.generate_random();         // random type, random fields
uint32_t add = gen.generate(rv32i::opgen::InstrType::ADD);
auto seq     = gen.generate_sequence(1000);   // vector of 1000 opcodes

// Auxiliary selectors (type-class shortcuts)
gen.generate_alu();              // any ALU instruction
gen.generate_memory();           // any load/store
gen.generate_branch();           // any conditional branch
gen.generate_no_control_flow();  // no branches/jumps/ecall/ebreak
gen.generate_linear_sequence(100);
```

Seeds make streams fully reproducible; the default configuration
(all types enabled) is **seed-stable** — it produces exactly the same
stream regardless of any mask-related API calls you don't make.

### Generator-specific selectors

| Generator | Extra API |
|---|---|
| RV32C | `generate_hint()` / `generate_hint(HintType)` — compressed HINTs (valid NOPs: `c.addi x0`, `c.lui x0`, shift-by-zero forms, ...); `generate_mixed(p_hint)` — blend HINTs into the canonical stream at a given rate |
| RV32A | `generate_lr()` / `generate_sc()` / `generate_amo()` / `generate_amo_arithmetic()` / `generate_amo_logical()` / `generate_acquire()` / `generate_release()` / `generate_seq_cst()` / `generate_with_ordering(type, aq, rl)` |
| RV32F | `generate_load_store()` / `generate_arithmetic()` / `generate_fma()` / `generate_conversion()` / `generate_compare()` / `generate_sign_inject()` / `generate_no_memory()` |
| Zicsr | `set_s_mode(b)` / `set_u_mode(b)` — grow the CSR address pools (`medeleg`/`mideleg`, supervisor CSRs) to match the CPU configuration so stimulus stays trap-free |
| Zifencei | `set_standard_only(false)` — also randomize the spec-ignored rd/rs1/imm fields of FENCE.I (default: canonical encoding only) |
| RV32FC, M, Zba, Zbb, Zbs, Zicond | the common API only (type tables are small) |

### Restricting stimulus: enable masks and groups

Every generator carries a 64-bit **enable mask** with one bit per
instruction type (for RV32C, bits 0–26 are the canonical types and
bits 27–35 the HINT families). Use it during bring-up (generate only
what the implementation already supports) or debugging (focus on a few
groups):

```cpp
rv32i::opgen::OpcodeGenerator gen(42);

gen.set_enabled_mask(rv32i::opgen::groups::LOADS |
                     rv32i::opgen::groups::STORES);   // LSU bring-up
gen.enable(rv32i::opgen::InstrType::JAL, true);       // plus one type
gen.enable(rv32i::opgen::InstrType::LB, false);       // minus one type

gen.generate_random();        // only enabled types, uniformly
gen.generate(rv32i::opgen::InstrType::SUB);  // explicit: ignores the mask
```

Semantics:

- `generate_random()` samples **uniformly over the enabled types**;
  `generate(type)` always honors an explicit request.
- Auxiliary selectors (`generate_alu()`, `generate_memory()`, ...)
  honor the mask and fall back to the enabled set when their own type
  list is fully masked out.
- An **empty mask is legalized to all-enabled** — no empty streams.
- Mask constants compose with `|`; `type_bit(type)` builds single-type
  masks; `get_enabled_mask()` reads the current mask back.

Named group constants per generator:

| Generator | `groups::` constants |
|---|---|
| RV32I | `UPPER`, `JUMPS`, `BRANCHES`, `LOADS`, `STORES`, `ALU_IMM`, `SHIFTS`, `ALU_REG`, `FENCE_OP`, `SYSTEM`, `ALL` |
| M | `MULTIPLY`, `DIVIDE`, `ALL` |
| A | `LR_SC`, `AMO_LOGICAL`, `AMO_ARITH`, `ALL` |
| C | `C_MEM`, `C_FLOW`, `C_ALU`, `C_SYSTEM`, `C_TYPES`, `HINTS`, `ALL` (plus `hint_bit(HintType)`) |
| F | `LOAD_STORE`, `ARITH`, `MINMAX`, `FMA`, `CVT`, `MV`, `CMP`, `SGNJ`, `CLASSIFY`, `ALL` |
| Zcf | `LOADS`, `STORES`, `ALL` |
| Zicsr | `REG_FORMS`, `IMM_FORMS`, `ALL` |
| Zba | `SHADD`, `ALL` |
| Zbb | `LOGIC_NEG`, `COUNT_OPS`, `MINMAX`, `EXTEND`, `ROTATE`, `BYTE_OPS`, `ALL` |
| Zbs | `SET`, `CLEAR`, `INVERT`, `EXTRACT`, `ALL` |
| Zicond, Zifencei | `ALL` (per-type `enable()` still works) |

### Illegal-opcode stimulus

`riscv_illegal_opgen.hpp` (namespace `riscv::illegal_opgen`) emits
encodings that are invalid per the specification's encoding tables —
derived independently of the model's decoders, so a shared spec
misreading cannot cancel out. Every generated encoding must trap with
cause 2 (`illegal instruction`), `xtval` = instruction bits, and no
architectural side effects. HINT encodings are deliberately *not*
generated (they are valid NOPs).

```cpp
illegal_opgen::ClassGenerator gen(42);
const illegal_opgen::ClassInfo* info = nullptr;

gen.set_enabled_mask(illegal_opgen::groups::COMPRESSED);
uint32_t op = gen.generate_random(info);
// info->name (e.g. "C.LWSP rd=x0"), info->compressed (mtval = parcel)

// Or iterate the full class table directly:
size_t n;
const illegal_opgen::ClassInfo* table = illegal_opgen::classes(n);
```

Group constants: `CANONICAL`, `BASE`, `CSR`, `FP`, `AMO`, `COMPRESSED`,
`ALL`; stable per-class indices live in `illegal_opgen::class_idx`.

## Lockstep cosimulation with an HDL implementation

The model is designed for lockstep verification against an RTL core:
header-only C++ (drop it into a Verilator/Verible C++ harness or a
DPI-C shared library), opcode injection (no fetch coupling), precise
trap reporting, and maskable stimulus that tracks the implementation's
maturity.

### Architecture

```
              +---------------------------+
              | stimulus (opgen + masks)  |
              +-------------+-------------+
                            | same opcode stream, same memory image
            +---------------v+    +-------v--------+
            | reference model |    | DUT (RTL)     |
            | step(bus, op)   |    | testbench     |
            +-------+---------+    +-------+-------+
                    | compare after every instruction
        +-----------v------------+
        | pc, x-regs, f-regs,    |
        | memory writes, trap    |
        | cause/xtval/xepc, priv |
        +------------------------+
```

### Setup checklist

1. **Match configurations.** Set `CPUConfig` to the RTL's parameter set:
   extension enables, `allow_misaligned_data`, `reset_vector`,
   `mtvec_reset`, S/U modes. Remember extension enables take effect at
   `reset()`.
2. **Share the memory image.** Load the same bytes into the model's
   memory (`load_program`) and the RTL's memory. For data accesses,
   either give both sides identical memories and compare writes, or
   route the model's `Bus` to the *same* backing store as the DUT.
3. **Drive identical stimulus.** Generate with the opgens,
   masked to the instructions the RTL already implements:

   ```cpp
   rv32i::opgen::OpcodeGenerator stim(seed);
   stim.set_enabled_mask(rv32i::opgen::groups::ALU_IMM |
                         rv32i::opgen::groups::ALU_REG |
                         rv32i::opgen::groups::SHIFTS);

   CPU model(cfg);
   for (;;) {
       uint32_t op = stim.generate_random();
       CPUExecResult r = model.step(bus, op);   // golden
       drive_dut_with(op);                       // via your harness
       compare(r, dut_state);
   }
   ```

4. **Compare after every instruction.** The minimal golden set from
   `CPUExecResult` plus model state:

   | What | Model source | Notes |
   |---|---|---|
   | Instruction retired vs trapped | `r.trap` / `r.interrupt` | must match exactly |
   | Trap cause / xtval / xepc | `r.trap_cause`, `r.trap_value`, `csrs.get(MEPC)` | precise on both sides |
   | Next PC | `r.next_pc` | check RTL's pc after retirement |
   | x-register file | `model.regs.read(i)` | compare all 31 each step (cheapest full check) |
   | f-register file / fflags | `model.fregs.read_bits(i)`, `fcsr` | see FP caveat below |
   | Memory writes | watch `Bus::write*` calls or compare images | AMOs are one logical read+write |
   | Privilege / key CSRs | `csrs.get_privilege()`, `mstatus`, `mie`... | mind WARL implementation choices |

### Recommended bring-up flow

1. **Canonical groups, one at a time** — start with `ALU_IMM`/`ALU_REG`,
   add `LOADS`/`STORES`, then control flow. A mismatch maps to a single
   group.
2. **Full valid space** — all groups enabled; add the other extensions'
   generators.
3. **HINT mix** — `generate_mixed(0.1)`: the RTL must treat every HINT
   as a NOP (a classic RTL bug).
4. **Illegal space** — `illegal_opgen::ClassGenerator`: the RTL must
   trap every class precisely (cause 2, xtval, no side effects).
5. **Config matrix** — disable extensions on both sides (RTL parameter
   ↔ `CPUConfig` + masks); formerly valid encodings must now trap.
6. **Interrupts and traps** — drive the same level-sensitive lines into
   both (`set_timer_interrupt()` etc. ↔ RTL pins) and compare
   delegation, vectoring and xRET behavior.

### Caveats

- **Interrupts**: inject them only after phase 6 — they make every
  step's outcome timing-dependent. `r.interrupt` marks steps where the
  model delivered an interrupt and no instruction retired.
- **Counters**: `mcycle`/`minstret` semantics (steps vs retired) may
  differ from your RTL's; either align the RTL or mask these CSRs out
  of the comparison.
- **WARL fields** (`misa`, `mtvec` mode, `mepc` mask, `mstatus.FS/SD`):
  the model's legalizations are documented in
  [riscv_csr_reference.md](doc/riscv_csr_reference.md) — match the RTL
  to them or exclude these fields from comparison.
- **Floating point**: arithmetic is bit-exact via the host FPU
  (RNE/RTZ/RDN/RUP, fused FMA), except RMM which is approximated with
  RNE (differs only on exact ties). If your RTL FPU is not yet
  IEEE-exact, compare integer results and fflags selectively rather
  than raw f-register bits.
- **Delivery mechanisms**: with Verilator, link the headers directly
  into the C++ harness. With event-driven simulators, wrap the model in
  a DPI-C shared library, or stream `{opcode, expected state}` records
  over a file/socket for batch comparison.

## Design highlights

- **Bus-injected memory**: all loads/stores/fetches go through the `Bus`
  interface; `BusFault` (or any `std::exception`) is converted into the
  correct architectural access-fault exception with precise `mtval`.
- **16-bit fetch granularity**: fetches are parcel-based, so a compressed
  instruction at the end of memory executes without over-fetching.
- **Configurable ISA**: extensions and privilege modes (S/U) are toggled
  via the CPU config; `misa` reflects the enabled set.
- **Strict decoding**: reserved/illegal encodings trap instead of being
  silently accepted, keeping the model faithful to the ratified specs.

## License

MIT — see `LICENSE`.
