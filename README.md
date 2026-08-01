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
suite for randomized round-trip and execution stimulus:
`riscv_rv32i_opgen.hpp`, `riscv_rv32m_opgen.hpp`, `riscv_rv32a_opgen.hpp`,
`riscv_rv32c_opgen.hpp`, `riscv_rv32f_opgen.hpp`, `riscv_rv32fc_opgen.hpp`,
`riscv_zicsr_opgen.hpp`, `riscv_zifencei_opgen.hpp`, `riscv_zba_opgen.hpp`,
`riscv_zbb_opgen.hpp`, `riscv_zbs_opgen.hpp`, `riscv_zicond_opgen.hpp`.
Notes: the Zicsr generator targets only existing CSRs — its CSR pools
grow when `set_s_mode()`/`set_u_mode()` are enabled to match the CPU
configuration (keeping stimulus trap-free), and the Zifencei generator
emits the canonical FENCE.I unless `set_standard_only(false)` is used to
randomize the spec-ignored rd/rs1/imm fields.

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
  (compressed or 32-bit, selected by the two low bits) instead of
  fetching — useful for cosimulation, trace replay and direct stimulus.
  Interrupts are still sampled first and the bus is still used for data
  accesses, but no fetch occurs (so no instruction access faults and no
  fetch side effects); the caller keeps `pc` coherent with the stream.
- Handled traps redirect to `xTVEC` and execution **continues** in the
  handler; pass `stop_on_trap = true` to `run()` to stop at the first
  trap/interrupt instead.
- `run()` counts *steps* (retired + trapped instructions + interrupt
  deliveries), and its default bound is effectively infinite — pass a
  finite limit when executing untrusted stimulus.
- `mcycle` counts steps; `minstret` counts retired instructions only.

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
