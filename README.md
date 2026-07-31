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

## File overview

### Core model

| File | Description |
|---|---|
| `riscv_common.hpp` | Shared foundation: `Bus` interface, `BusFault`, `RegFile`, `SimpleMemory` helper, bit-field utilities, address checking, common exceptions. Used by every extension module. |
| `riscv_cpu.hpp` | Top-level CPU integrating all extensions. Handles dispatch, synchronous exceptions and interrupts (M/S/U modes, `medeleg`/`mideleg`, vectored `mtvec`, precise `mtval`), MRET/SRET, misalignment policy, and 16-bit-granular instruction fetch (no over-fetch past the last compressed parcel). |
| `riscv_rv32i.hpp` | RV32I base integer set: decoder/executor for all 40 instructions (incl. ECALL/EBREAK/MRET/SRET/SFENCE.VMA/WFI decoding hooks, FENCE, loads/stores, jumps). |
| `riscv_rv32m.hpp` | M extension: the 8 multiply/divide instructions (MUL[H], DIV[U], REM[U]). |
| `riscv_rv32a.hpp` | A extension: LR.W/SC.W with reservation tracking and all nine AMOs, incl. aq/rl ordering-bit handling and misalignment trapping. |
| `riscv_rv32f.hpp` | F extension: single-precision floating point — FLW/FSW, arithmetic, fused multiply-add, comparisons, conversions, sign injection, classification, and `fflags`/`frm`/`fcsr` interaction. |
| `riscv_rv32fc.hpp` | Zfc: compressed floating-point loads/stores (C.FLW/C.FSW/C.FLWSP/C.FSWSP). |
| `riscv_rv32c.hpp` | C extension: decoder/executor for all 16-bit compressed instructions. |
| `riscv_zicsr.hpp` | Zicsr: CSR instructions plus the full CSR file (privileged spec v1.12) — machine/supervisor/user CSRs, counters, trap state, delegation, `satp`, floating-point CSRs. |
| `riscv_zifencei.hpp` | Zifencei: FENCE.I (tracked for correctness; effectively a no-op in a cache-less software model). |
| `riscv_zba.hpp` | Zba bit-manipulation: shifted-add address-generation instructions (SH1ADD–SH3ADD). |
| `riscv_zbb.hpp` | Zbb bit-manipulation: logic-with-negate, min/max, clz/ctz/cpop, rotations, sign/zero extension, byte swaps, orc.b, rev8. |
| `riscv_zbs.hpp` | Zbs bit-manipulation: single-bit set/clear/invert/extract, register and immediate forms. |
| `riscv_zicond.hpp` | Zicond: conditional-zero instructions (CZERO.EQZ/CZERO.NEZ) for branchless selection. |

### Test stimulus generators (one per extension)

| File | Description |
|---|---|
| `riscv_rv32i_opgen.hpp` | Random *valid* RV32I opcode generator (all fields randomized within legal bounds). |
| `riscv_rv32m_opgen.hpp` | Same for the M extension. |
| `riscv_rv32a_opgen.hpp` | Same for the A extension (LR/SC/AMOs). |
| `riscv_rv32c_opgen.hpp` | Same for the C extension (compressed encodings). |
| `riscv_rv32f_opgen.hpp` | Same for the F extension. |
| `riscv_rv32fc_opgen.hpp` | Same for the compressed FP instructions. |
| `riscv_zicsr_opgen.hpp` | Same for CSR instructions. |
| `riscv_zifencei_opgen.hpp` | Same for FENCE.I. |
| `riscv_zba_opgen.hpp` | Same for Zba. |
| `riscv_zbb_opgen.hpp` | Same for Zbb. |
| `riscv_zbs_opgen.hpp` | Same for Zbs. |
| `riscv_zicond_opgen.hpp` | Same for Zicond. |

### Tests and docs

| File | Description |
|---|---|
| `test_riscv_model.cpp` | The test suite: regression tests for every fixed bug, full-CPU integration tests (dispatch, traps, interrupts, MRET/SRET, misalignment, access faults, interrupt prioritization/gating, `sstatus` subset, SATP/TVM, MRET legality), and opcode-generator round trips for all extensions. Minimal built-in harness (TEST/CHECK macros), no external framework. |
| `Makefile` | Builds the test suite into `build/` and runs it (`make test`). |
| `AGENTS.md` | Project conventions for AI coding agents (layout, build/test rules). |
| `notes.md` | Design Q&A notes on RISC-V hardware implementation: extension priorities for cache-less systems, dual-issue of compressed instructions, and serial execution in debug mode. |
| `LICENSE` | MIT license. |
| `.gitignore` | Keeps `build/` artifacts out of version control. |

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
