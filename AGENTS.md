# AGENTS.md — RV32 reference model

Project-specific facts. General build/convention rules live in the
global `~/.pi/agent/AGENTS.md` and apply here as well (artifacts in
`build/`, always build and test via the Makefile).

## Building and running the test suite

```bash
make test     # builds into build/ and runs the test suite
make clean    # removes build/
```

The build uses `-std=c++17 -Wall -Wextra -Wpedantic -fsanitize=undefined`;
keep the suite warning- and UBSan-clean.

## Project layout

- `riscv_common.hpp` — shared types (Bus, RegFile, memory helpers, exceptions)
- `riscv_cpu.hpp` — full CPU integrating all extensions (M-mode hart with
  optional S/U modes: trap delegation, S-mode interrupts, MRET/SRET)
- `riscv_<ext>.hpp` — decoder/executor per extension
  (rv32i/m/a/c/f/fc, zicsr, zifencei, zba, zbb, zbs, zicond)
- `riscv_<ext>_opgen.hpp` — random opcode generators per extension (test stimulus)
- `riscv_illegal_opgen.hpp` — spec-table-derived invalid-encoding generator
  (decoder-strictness stimulus; HINT encodings are valid, never emitted)
- `test_riscv_model.cpp` — the test suite
