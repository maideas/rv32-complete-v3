# RV32 model review, spec fixes, and opgen stimulus tooling

A full review of the RV32 reference model followed by an iterative hardening of its test-stimulus infrastructure. The initial code review found and fixed five privileged-spec deviations (JALR decode strictness, LR/SC reservation clearing on traps, mip/sip write masks, cross-mode interrupt priority, U-mode counter gating); subsequent generator-driven testing uncovered and fixed two more model bugs (C.LUI x0 HINT handling, FLW/FSW negative-immediate sign extension). The session then built out the stimulus tooling: a spec-table-derived illegal-opcode generator with trap-contract tests and an exhaustive 16-bit parcel sweep, compressed-HINT generators, opgen coverage audits, and a uniform per-instruction-group enable-mask configuration across all generators, documented with README guides for opgen usage, opcode injection, and HDL lockstep cosimulation.

## User

> hi

## Assistant

Hi! How can I help you with the RV32 reference model today?

## User

> please analyze the files in this directory - implementing a production level RISC-V reference model - for code quality, functional correctness, and completeness.

## Assistant

I'll start by surveying the project structure and files.

## Assistant

Now the core extension modules:

## Assistant

Now the CSR file (largest module) and F/C extensions:

## Assistant

Now the F extension and C extension:

## Assistant

Now the C extension, Zcf, and remaining small extensions:

## Assistant

Now let me build and run the test suite, and get an overview of the tests:

## Assistant

All 104 tests pass. Let me check the test suite structure, an opgen sample, and git history for context:

## Assistant

Let me check the Zbs constants (potential encoding collision with ROL/BINV) and Zicond:

## Assistant

The test suite passes fully. Now let me verify a few suspected spec deviations with targeted experiments:

## Assistant

Three issues confirmed. Let me also check the cross-mode interrupt priority rule and a couple of CSR edge cases:

## Assistant

Let me quickly assess test-suite depth and check a couple of remaining areas (A-extension decode strictness, F test coverage):

## Assistant

My analysis is complete. Let me clean up my scratch probes first:

## Assistant

### Code Review: RV32 Reference Model

I read every header (all 13 model modules + 12 opgen modules), the test suite, README and docs, built and ran the suite (**104/104 tests pass, warning- and UBSan-clean**), and wrote targeted probe programs to verify suspected spec deviations against the actual binary.

#### Overall assessment

This is a well-above-average hobbyist/verification model that is genuinely close to "production reference model" quality: consistent architecture, strict decoding as a stated and mostly-honored goal, precise `mtval`, careful privilege/trap machinery, and unusually good documentation (README, CSR reference, session logs). I found **5 real functional deviations** (verified at runtime, below), none catastrophic, plus a set of minor issues. The biggest gap for a *production-grade golden model* is not code but **validation methodology** (details at the end).

#### Strengths

**Architecture & code quality**
- Clean, uniform Decoder/Executor pattern per extension with exact, non-aliasing decoders, so CPU dispatch order is irrelevant — a good invariant, stated in comments.
- Shared `mem_access::load/store` helpers centralize alignment checks and bus-fault→trap mapping so semantics can't drift between RV32I/C/F/Zcf modules. Same instinct visible in the shared `Bus`/`BusFault` interface and 16-bit-parcel fetch (no over-fetch past the last compressed instruction — a bug many models have).
- `SimpleMemory::check_addr` uses 64-bit arithmetic to prevent wraparound OOB — with an explanatory comment. `RegFile`/`FRegFile` mask indices. This is defensive coding done right.
- Comments consistently cite *why* (spec rules, WARL choices, UB avoidance in rotates, C.JALR read-before-write of ra, FCVT saturation/NV-vs-NX subtleties). The F module documents its RMM≈RNE approximation honestly.
- `HostRoundingGuard` restores host rounding mode; NaN canonicalization is centralized; `fflags`/`frm` are true aliases of one `fcsr` cell; `cycle`/`time`/`instret` are true aliases of the machine counters.

**Correctness highlights verified by reading**: SLTIU sign-extension, divide-by-zero/overflow semantics, JALR target masking and `&~1`, branch/jump target-misaligned-before-side-effects ordering, `mepc` IALIGN masking, `medeleg[11]` read-only-zero, trap-never-delegated-from-M, vectored vs direct `xtvec` (exceptions always to BASE), ECALL cause per privilege, DYN-rm-with-invalid-frm → illegal, FMIN/FMAX ±0 and sNaN handling, saturating FCVT with correct NV/NX split, compressed reserved-encoding strictness (C.ADDI4SPN nzimm=0, C.LWSP rd=0, shamt[5], C.JR rs1=0), correct Zbs/Zicond/Zbb funct7 encodings (BINV=0110100 doesn't collide with ROL).

#### Verified functional issues (confirmed with probe programs)

1. **JALR with funct3 ≠ 0 executes instead of trapping** — `riscv_rv32i.hpp`, `Decoder::decode`: the `opcode::JALR` case sets `JALR` unconditionally; funct3≠0 encodings are reserved. Probe: `0x0000D0E7` (funct3=1) executed as `jalr` with no trap. This contradicts the module's own strict-decoding goal (every other opcode group checks funct3).

2. **LR/SC reservation is not cleared on trap/interrupt entry** — `riscv_cpu.hpp` `enter_m_trap`/`enter_s_trap` never touch `reservation`. The A-extension spec mandates the reservation be cleared when the hart takes a trap. Probe: LR → ECALL → SC succeeded (`rd=0`); spec requires SC to fail (`rd=1`). Fix is one line in each trap-entry helper (or in `take_exception`/`take_pending_interrupt`).

3. **`mip.STIP`/`mip.SEIP` are not writable by M-mode** — `mip_writable_mask()` returns only `MSI|SSI`. Priv spec v1.12: STIP and SEIP *are* writable in `mip` (only MTIP/MEIP are hardwired to the platform lines). Probe: CSRRW of STIP|SEIP left `mip=0`. Related: the `sip` write path ANDs with `mip_writable_mask()`, so with an (unusual) `mideleg` value it lets S-mode write `mip.MSIP` through `sip` — `sip` should expose only SSIP as writable.

4. **Cross-mode interrupt priority inversion** — `take_pending_interrupt` iterates the cause-priority list {MEI, MSI, MTI, SEI, SSI, STI} and takes the first *enabled* one. Per the priv spec, interrupts targeting M-mode take priority over interrupts targeting S-mode regardless of cause order. Probe: MEI delegated to S + MSI not delegated, executing in S with SIE=1 → model took the delegated MEI (cause `0x8000000B`); spec requires MSI to M first. Only observable when M-cause interrupts are delegated, which is an exotic configuration — hence "minor".

5. **U-mode counter access broken when S-mode is absent** — `CSRFile::counter_readable`: for USER privilege it does `if (!s_mode_enabled) return false;`. With U-mode but no S-mode, `scounteren` doesn't exist and `mcounteren` alone should gate access. Probe: `mcounteren.TM=1`, U-mode `csrr a0, time` trapped; spec says it must succeed.

#### Minor / cosmetic

- **AMOs execute as separate bus read + write transactions** — architecturally correct results on a single hart, but a `Bus` implementation (MMIO device, memory-mapped lock, co-simulation peer) observes a non-atomic sequence, and a faulting write leaves the read visible. Worth a documented note or a combined `Bus::amo()` hook.
- Host FP **exception flags leak** into the embedding application (rounding mode is guarded, `fetestexcept` state is not — e.g., FEQ on an sNaN raises host FE_INVALID).
- Dead code: `FRegFile::is_nan_boxed` (RV64 leftover, self-admitted), `#include <unordered_map>` in `riscv_rv32a.hpp`, the odd `(void)fregs.read_bits(...)` lines in FMIN/FMAX.
- Style: the nine near-identical AMO case bodies and the CPU's long dispatch if-chain would table-drive nicely, but both are readable as-is.
- EBREAK `mtval=pc` — matches Spike/QEMU; fine, just noting it's a spec-interpretation choice.

#### Completeness

Documented exclusions (Sv32, PMP, debug mode, hypervisor, N) are reasonable and clearly stated. For a production reference model, consider adding:
- **`mcountinhibit`** (0x320) — commonly implemented; currently traps.
- **`mtime`/`mtimecmp`** as real MMIO rather than `time`≡`mcycle` (documented approximation).
- `mhpmcounter*`/`mhpmevent*` (even as WARL-zero) and `mseccfg`/`mstateen` as WARL-zero for newer toolchain compatibility.
- F-extension `mstatus.FS` state tracking is hardwired Dirty — legal WARL, documented; fine.

#### Test suite — good, but the validation story is the weakest link for "production level"

Strengths: 104 tests with regression tests for every past bug, excellent privileged-architecture coverage (delegation, gating, xRET legality, sstatus subset, TVM/TW/TSR), opcode-injection equivalence checks, and per-extension opgen round trips.

Gaps:
- **No cross-validation against an external golden reference.** The opgen round trips are self-referential — encoder and decoder live in the same project, so a shared misreading of the spec cancels out (issue #1 above survived exactly this way). For a model positioned for RTL verification, lockstep cosimulation against Spike/Sail, or running **riscv-arch-test** (the official RISC-V compatibility suite), is the standard bar — and the `step(bus, opcode)` injection API you already built is precisely the right hook for lockstep diffing.
- No illegal-encoding fuzzing (feed all 2³² / reserved neighborhoods and assert trap behavior) — this would have caught the JALR case.
- FP testing relies on host IEEE behavior; flag/rounding corner cases (esp. RMM) would benefit from a softfloat cross-check or Berkeley TestFloat vectors.

#### Verdict

| Aspect | Rating |
|---|---|
| Code quality / architecture | Strong — consistent, defensive, well-commented |
| Documentation | Strong — README, CSR reference, honest approximation list |
| Functional correctness | Good — 5 verified deviations (1 decode bug, 4 privileged-architecture edge cases), all localized and easy to fix |
| Test suite | Good internally; needs external golden-reference/arch-test validation to justify "production golden model" |

Issues #1–#3 are quick, high-value fixes; #4–#5 are edge cases worth fixing for spec fidelity. Want me to implement the fixes (with regression tests added to the suite)?

## User

> yes, please implement all 5 fixes

## Assistant

I'll implement all 5 fixes plus regression tests. Let me start with the model fixes:

## Assistant

Fix 2 — clear the LR/SC reservation on any trap entry (exception or interrupt, M or S target):

## Assistant

Fix 3 — `mip` STIP/SEIP writable by M-mode; `sip` write restricted to SSIP only:

## Assistant

Now the regression tests. Let me examine the relevant test sections to match style:

## Assistant

There's an existing `mip` writability test at line 1181 that my fix changes the expectations for. Let me review it and the related tests:

## Assistant

The existing `mip` test uses a no-S-mode config, so it still holds. Let me check the S/U interrupt tests that fix 4 might interact with:

## Assistant

Existing tests are compatible with the two-pass fix. Let me check the `enter_mode` helper before writing the new tests:

## Assistant

Now I'll add the five regression tests. First, the JALR decode test:

## Assistant

Reservation-clears-on-trap test (fix 2):

## Assistant

mip/sip writability test (fix 3):

## Assistant

Cross-mode interrupt priority test (fix 4) and U-mode-without-S counter test (fix 5):

## Assistant

All 5 fixes are implemented and verified — the suite now runs **111 tests, 0 failures**, still warning- and UBSan-clean.

### Changes

**Model fixes**

1. **`riscv_rv32i.hpp` — JALR decode strictness**: the `opcode::JALR` case now requires `funct3 == 000`; other funct3 values on that opcode are reserved and trap as illegal (with a CPU-level confirmation that it traps with cause 2 and `mtval` = instruction bits).

2. **`riscv_cpu.hpp` — reservation cleared on trap entry**: `enter_m_trap()` and `enter_s_trap()` now call `reservation.clear()`, per the A-extension requirement that any trap (exception *or* interrupt) clears the hart's load reservation. Covered by two tests: LR → ECALL → SC fails (`rd=1`, no store), and LR → timer interrupt → SC in the handler fails.

3. **`riscv_zicsr.hpp` — `mip`/`sip` write masks**: `mip_writable_mask()` now includes STIP and SEIP when S-mode exists (per priv spec v1.12, only MTIP/MEIP are read-only in `mip`); the `sip` write path now only allows the SSIP bit (and only when delegated), instead of leaking `MSIP` through `sip`. Input lines still drive the same bits via the `set()` backdoor, which the test also checks.

4. **`riscv_cpu.hpp` — cross-mode interrupt priority**: `take_pending_interrupt()` now scans in two passes — M-target interrupts first, then S-target — preserving the MEI > MSI > MTI > SEI > SSI > STI order within each target. New test: delegated MEI + non-delegated MSI both pending in S-mode → MSI taken to M-mode at `mtvec`.

5. **`riscv_zicsr.hpp` — U-mode counter access without S-mode**: `counter_readable()` no longer requires `scounteren` when S-mode isn't implemented; `mcounteren` alone gates U-mode access. Test covers both the `mcounteren.TM=0` (traps) and `=1` (succeeds) cases.

**Tests** — 7 new regression tests (111 total):
- `test_rv32i_decode`: JALR funct3=1/7 illegal, decoder- and CPU-level
- `test_rv32a`: reservation cleared by exception and by interrupt
- `test_traps_interrupts`: mip STIP/SEIP M-writable; sip writes only SSIP
- `test_supervisor_user_mode`: M-target priority over delegated S-target; U-mode counters without S-mode

The pre-existing `mip` writability test (no-S-mode config → MSIP-only) still passes unchanged, confirming the fix is correctly conditional on S-mode presence.

## User

> please review, if the documentation still matches the code

## Assistant

I'll check all documentation against the current code, with focus on anything affected by the 5 fixes. Let me read the doc files:

## Assistant

Now let me check README, notes.md, and the session logs for claims affected by the fixes:

## Assistant

Let me check the README interrupt section and the end of the review session log:

## Assistant

Finally, the errata note in the historical review session log (which asserts the pre-fix behavior as "sound"):

## Assistant

Now verify everything still builds and passes, and do a final consistency sweep:

## Assistant

Documentation review complete — **111 tests, 0 failures** after all edits. Here's what I found and fixed:

### Stale documentation corrected (fallout from the 5 fixes)

**`doc/riscv_csr_reference.md`** (the normative reference):
- **`mip` row** — was "MSIP is always writable; SSIP only when S-mode exists. MTIP/MEIP/STIP/SEIP driven by input lines". Now correctly documents MSIP always writable, SSIP/STIP/SEIP software-writable when S-mode exists, input lines driving the same bits via the backdoor, MTIP/MEIP read-only.
- **Interrupt entry** — added that M-target interrupts take precedence over S-target ones regardless of cause priority; reworded the misleading "interrupts targeting M are never delegated" sentence (mideleg *can* delegate M-cause interrupts to S in this model).
- **Trap section** — added that taking any trap clears the LR/SC reservation.
- **`mcounteren` row** — now states U-mode needs `scounteren` only when S-mode exists, `mcounteren` alone otherwise (matching fix 5).
- **Precise `xtval` table** — added two missing rows: instruction-address-misaligned → faulting target address; EBREAK/C.EBREAK → address of the EBREAK.

**`README.md`**:
- Interrupts bullet now notes the M-target-over-S-target precedence rule.
- A-extension row now mentions the reservation is cleared on taken traps.

**`riscv_cpu.hpp`** file-header comment: same M-target precedence clarification (keeps the code's own docs in sync with `take_pending_interrupt`).

### Pre-existing doc/code mismatches found (not caused by the fixes)

- **`mstatus.TW` row** in the CSR reference said "exists with S or U", but `mstatus_legal_mask()` only makes TW writable when *both* S and U are enabled — corrected the doc to match the (WARL-legal) code behavior.
- **`doc/riscv_s_u_mode_review_fixes_and_tests.md`** is a historical session log that asserts the old `mip` write mask and priority order are "all sound". Rather than rewriting history, I added a dated **Errata** block right after its intro summarizing the five later fixes and pointing to the CSR reference for current behavior.

### Verified already-consistent (no change needed)

- CSR reference's `sip` row ("writable subset restricted to SSIP") and counter-shadow gating text ("`scounteren` ... when S-mode exists") — interestingly, these were *already correct*; the code had drifted from the docs and the fixes realigned it.
- `mideleg`/`medeleg` rows, `sstatus` subset rules, trap-entry state tables, MRET/SRET sequences, reset behavior, `satp`/WARL notes, README's ISA tables, "known approximations", and `AGENTS.md`.
- `doc/notes.md` (hardware design Q&A) and `doc/riscv_s_and_u_modes_implementation.md` (historical plan/log) — unrelated to the fixes; left as historical records.

Documentation and code are now in agreement at the 111-test state.

## User

> Commit all current changes in the repository and push them.
> 
> Procedure:
> 1. Inspect what changed: run `git status --short` and `git diff` (plus
>    `git diff --cached` if something is already staged; use
>    `git log --oneline -5` to match the repo's commit-message style).
> 2. If there is nothing to commit, say so and stop (still push if there
>    are unpushed commits).
> 3. Compose the commit message:
>    - If the user provided a message as argument (``), use it verbatim.
>    - Otherwise write a meaningful message yourself based on the diff:
>      a concise subject line (≤ 72 chars, imperative mood, no trailing
>      period) that names the what and why, followed — only when the change
>      warrants it — by a blank line and a short body with details.
>    - Never use generic messages like "update" or "changes".
> 4. Before staging, review the untracked (`??`) files from `git status`.
>    If any look unintended (scratch files, secrets, editor backups,
>    binaries outside `build/`), ask the user whether to include them.
> 5. Stage everything (`git add -A`), commit with that message, then
>    `git push` (set the upstream with `-u` if the branch has none).
> 6. Show the resulting `git log -1 --stat` output and confirm the push
>    succeeded.

## Assistant

No untracked files, nothing pre-staged. The changes are the 5 spec fixes + tests + doc sync. Committing and pushing:

## Assistant

Committed and pushed successfully.

- **Commit** `d1023ad` — "Fix five privileged-spec deviations found in full-model review" (7 files, +206/−18)
- **Push** — `0ba40b7..d1023ad  main -> main` to `github.com:maideas/rv32-complete-v3.git` ✓

The commit contains the 5 model fixes (JALR decode strictness, reservation clear on trap entry, mip/sip write masks, cross-mode interrupt priority, U-mode counter gating), the 7 regression tests, and the documentation sync (README, CSR reference, review-log errata). Working tree is now clean.

## User

> have you already checked, that the opcode generators create the full range of valid opcode and randomize all opcode fields in the complete valid range ?

## Assistant

Not systematically — I only skimmed the RV32I opgen's encoders during the initial review. Let me do that audit now, checking each generator for (a) full instruction coverage, (b) complete field ranges, and (c) uniform/unbiased randomization:

## Assistant

RV32I opgen is complete (all 40 instructions, full imm ranges; only FENCE's `fm` field is fixed at 0). Now the M, A, and CSR opgens:

## Assistant

A and M opgens are complete. Now the C-extension opgen (largest, most intricate encodings):

## Assistant

Zbb/Zbs/Zicond/Zba opgens look complete. Before concluding, let me check how the test suite uses the opgens, then empirically verify coverage with an audit harness:

## Assistant

The I/B/J immediate "misses" are sampling noise (uniform-by-construction over full ranges, extremes just didn't land). Let me tighten those three checks with targeted sampling to separate construction gaps from luck, and check whether the test suite ever uses the Zifencei random-fields mode:

## Assistant

Now I have — full audit done, both by code inspection (construction) and empirically (200k samples/extension + 2M-sample targeted extreme checks). Here's the verdict:

### Complete generators ✓

| Opgen | Coverage | Field ranges |
|---|---|---|
| **RV32I** | 40/40 types, 0 illegal | imm12/branch/JAL confirmed full incl. extremes (−2048, −4096, −2²⁰ all hit); regs 0–31 |
| **M** | 8/8 | full regs |
| **A** | 11/11 | all 4 aq/rl combos; LR correctly pins rs2=0 |
| **F** | 26/26 | rm ∈ {0,1,2,3,4,7} — all legal modes, reserved 5/6 correctly excluded |
| **Zcf** | 4/4 | full offsets; C.FLWSP hits all 32 f-regs incl. f0 |
| **Zba/Zbb/Zbs/Zicond** | 3/18/8/2 | shamt 0–31 confirmed; unary-op rs2 constants correct |
| **Zifencei** | canonical encoding | (see notes) |

All RNGs are `mt19937` + `uniform_int_distribution` (no modulo bias), seedable, with uniform instruction-type selection.

### Real gaps found (3)

1. **Zicsr opgen — 18 implemented CSRs are never targeted.** The pools predate S/U-mode support: missing `medeleg`, `mideleg`, `mcounteren`, `mip`, `mstatush`, `menvcfg`, `menvcfgh` (writable), all 10 S-mode CSRs (`sstatus`…`satp`), and read-only `time`/`timeh`. The header comment is also stale ("M-mode-only reference model").

2. **RV32C opgen — C.ADDI16SP never generates nzimm = −512** (valid extreme, confirmed empirically: observed range [−496, 496]). Cause: it draws magnitude `range(0,31)*16` ≤ 496 and negates with 50% probability — a construction bug that also slightly skews the distribution.

3. **RV32I opgen — FENCE's `fm` field is hardwired to 0**, so FENCE.TSO (`fm=1000`) and reserved-`fm` encodings are never generated, even though the decoder intentionally accepts them (spec: reserved fm executes as a conservative fence).

### Deliberate scope choices (not bugs)

- **No HINT encodings** are generated (C.ADDI/LI/MV/SLLI with rd=x0, compressed shifts with shamt=0 — confirmed 0 occurrences in 2M samples). These are designated HINTs in the spec and execute as NOPs; restricting stimulus to canonical encodings is defensible, but it's a scope decision worth documenting.
- **Zifencei opgen** *can* randomize the ignored rd/rs1/imm fields (`set_standard_only(false)`), but defaults to canonical — and the test suite never enables that mode, so decoder tolerance of nonzero ignored fields is opgen-untested.
- FENCE rd/rs1 pinned to 0 (reserved-must-be-zero fields) — correct conservative choice.

One meta-observation: the suite's round-trip test only checks "decodes valid" over 5000 samples — no type-coverage or range checks — which is why gaps 1–3 survived. The audit harness I wrote (`build/opgen_audit.cpp`, gitignored) could be adapted into a proper coverage test.

Want me to fix the three gaps (ADDI16SP range, FENCE fm, Zicsr CSR pools + stale comment) and add an opgen coverage test to the suite?

## User

> yes, please fix the gaps and add the proposed opgen coverage test to the suite

## Assistant

I'll implement the three opgen fixes, then add the coverage test. First, the C.ADDI16SP range fix:

## Assistant

Fix 2 — FENCE `fm` randomization (normal and TSO are the two defined values):

## Assistant

Now the coverage test. Let me look at `main()` and the end of the test file:

## Assistant

Now I'll add the coverage test section to the test suite:

## Assistant

Need `<set>`/`<map>` includes:

## Assistant

All 123 tests pass. Let me clean up the scratch audit files (build/ is for generated files only), and sync the two doc spots that describe the test suite:

## Assistant

Let me check the README's opgen paragraph for the new config-dependent CSR pools, then do the final build:

## Assistant

Done — all three gaps fixed and the coverage test added. **123 tests, 0 failures**, warning- and UBSan-clean.

### Opgen fixes

1. **`riscv_rv32c_opgen.hpp` — C.ADDI16SP full range**: now draws the multiplier uniformly from `[-32, 31]` (×16, rejecting the reserved 0), so the valid extreme `nzimm = -512` is reachable and the distribution is uniform over all 63 legal values. Verified by the new test hitting min `-512` / max `496`.

2. **`riscv_rv32i_opgen.hpp` — FENCE `fm` field**: now randomly generates `fm = 0000` (normal) or `fm = 1000` (FENCE.TSO) — the two defined values — with pred/succ still fully randomized and rd/rs1 kept at 0 (reserved fields).

3. **`riscv_zicsr_opgen.hpp` — complete CSR pools**: restructured into `WR_BASE` (19 always-existing M-mode CSRs — now including `mcounteren`, `mip`, `mstatush`, `menvcfg`, `menvcfgh`), `WR_DELEG` (`medeleg`/`mideleg`), `WR_SMODE` (all 10 supervisor CSRs), and an expanded read-only pool (now with `time`/`timeh`). New `set_s_mode()`/`set_u_mode()` on the generator grow the pools to match the CPU configuration, preserving the trap-free guarantee. Stale "M-mode-only" header comment corrected.

### New test section — `M. Opgen coverage` (12 tests)

Adapted from the audit harness into `test_opgen_coverage()`:
- **Type histograms**: every extension must generate all its instruction types (40/27/26/11/8/8/18/8/4/3/2/6) with zero illegal decodes over 200k samples each.
- **Field extremes** (targeted per-type sampling): RV32I imm12 ±2048, branch ±4096, JAL near-±2²⁰; FENCE both fm values; C.J ±2048, C.B ±256, C.ADDI16SP **−512**/496; RORI shamt 0/31; A all four aq/rl combos; F all six legal rm values (and never reserved 5/6); C.FLWSP covering all 32 f-regs incl. f0.
- **Zicsr pool coverage**: M-only config generates exactly the 31 always-implemented CSRs and *never* mode-dependent ones (negative check); S/U config also hits `medeleg`/`mideleg` + all S CSRs, and 3000 S/U-configured samples execute trap-free on an S/U CPU.
- **Zifencei**: canonical-only by default; `set_standard_only(false)` produces >100 distinct encodings, all decoding as FENCE.I.

Docs synced: test-suite header comment, README's test-file row, and a note in the README's opgen paragraph about the config-dependent Zicsr pools and the Zifencei modes.

Changes are unstaged in the working tree — say the word if you want them committed and pushed.

## User

> Commit all current changes in the repository and push them.
> 
> Procedure:
> 1. Inspect what changed: run `git status --short` and `git diff` (plus
>    `git diff --cached` if something is already staged; use
>    `git log --oneline -5` to match the repo's commit-message style).
> 2. If there is nothing to commit, say so and stop (still push if there
>    are unpushed commits).
> 3. Compose the commit message:
>    - If the user provided a message as argument (``), use it verbatim.
>    - Otherwise write a meaningful message yourself based on the diff:
>      a concise subject line (≤ 72 chars, imperative mood, no trailing
>      period) that names the what and why, followed — only when the change
>      warrants it — by a blank line and a short body with details.
>    - Never use generic messages like "update" or "changes".
> 4. Before staging, review the untracked (`??`) files from `git status`.
>    If any look unintended (scratch files, secrets, editor backups,
>    binaries outside `build/`), ask the user whether to include them.
> 5. Stage everything (`git add -A`), commit with that message, then
>    `git push` (set the upstream with `-u` if the branch has none).
> 6. Show the resulting `git log -1 --stat` output and confirm the push
>    succeeded.

## Assistant

No untracked files. Committing and pushing:

## Assistant

Committed and pushed successfully.

- **Commit** `4ab4235` — "Fix opgen coverage gaps and add opgen coverage tests" (5 files, +440/−50)
- **Push** — `d1023ad..4ab4235  main -> main` to `github.com:maideas/rv32-complete-v3.git` ✓

Contains the three opgen fixes (C.ADDI16SP −512 extreme, FENCE `fm` randomization, complete config-aware Zicsr CSR pools), the new 12-test `M. Opgen coverage` section (123 tests total, all passing), and the README/test-header doc sync. Working tree is clean.

## User

> is the opcode generator code written in a way, that all opcode fields get randomized independently, so that - even with a limited number of opcode generate samples - all fields see some randomization ?

## Assistant

Good question — that's about the *construction pattern* of the generators. Let me verify it both by inspecting the draw structure and empirically (field independence + small-sample behavior):

## Assistant

Yes — with a few deliberate, ISA-required exceptions. I verified this both structurally and empirically:

### Construction: every field is an independent, fresh draw

Every opgen follows the same pattern: each logical field gets its own `std::uniform_int_distribution` call against the shared `mt19937` stream, on every single `generate()` call. No field is ever derived from another field's value, there's no cycling/rotation state, and no single random word is bit-sliced into multiple logically independent fields (the one exception is benign — see below). Instruction-type selection is also a single independent uniform draw, so the joint distribution is essentially *uniform type × uniform fields* — any field combination is reachable from the very first sample.

Empirical confirmation:

```
16 ADD samples:   14 distinct rd, 12 distinct rs1 (of 16 draws)
16 ADDI samples:  16 distinct imm12 values
16 AMOADD samples: rd=13 rs1=12 rs2=14 distinct, aq/rl combos=4
corr(rd,rs1)=0.0014  corr(rd,rs2)=-0.0009  corr(rs1,rs2)=-0.0038   (ADD, 500k)
corr(rs1,rs2)=0.0000 corr(rs1,rm)=0.0006 corr(rs2,rm)=0.0026       (FADD, 500k)
```

So even with ~16 samples every field sees wide variation, and pairwise field correlation is statistically zero.

### Deliberate exceptions (all correct by design)

- **Encoding-fixed fields** — LR's rs2=0, FSQRT/FCVT/FMV rs2 constants, Zbb unary-op rs2 constants, SRAI's funct7 bit inside the immediate, C.ADDI16SP rd=sp, FENCE rd/rs1=0 (reserved-must-be-zero). Randomizing these would produce *invalid* opcodes.
- **Format-shared fields** — e.g. C.ADDI's rd/rs1 are literally the same encoding field; correlation is inherent to the format.
- **Canonical-range restrictions** — `reg_nz()` where rd=0 is a HINT, compressed-shift shamt 1–31 (0 is a HINT). Intentional scope choices, previously documented.
- **One designed correlation** — the Zicsr generator forces `rs1=x0` with p=0.3 and picks the CSR pool (read-only CSRs included) *based on that*, which is exactly what keeps the stimulus trap-free. rs1 and csr-address are correlated on purpose here.
- **aq/rl from one draw** — the A opgen draws one uniform 0–3 value and splits it into aq/rl bits; since all four combinations are equiprobable, the bits are still marginally independent Bernoulli(½), so this is fine.

### One theoretical caveat (irrelevant in practice)

Sequential outputs of a single `mt19937` are not independent in the strict cryptographic sense — but mt19937 is 623-dimensionally equidistributed, and the measured correlations (~10⁻³ at 500k samples) confirm that's a non-issue at these dimensions for test stimulus.

Cleaned up the probe; no code changes were needed.

## User

> what would it mean to generate invalid opcodes in a similar way, to allow checking a hardware implementation for correctly recognizing all of them as invalid opcodes ?

## Assistant

That's exactly the right next question for RTL verification — it inverts the opgen problem: instead of sampling the *valid* encoding space, you systematically sample the *invalid* space and check the DUT raises a precise illegal-instruction exception for every case. Here's what that means concretely:

### 1. "Invalid" is a config-dependent, structured set — not just random garbage

An encoding is illegal for several distinct reasons, and a good illegal-opgen covers each category deliberately:

**(a) Constraint violations within a valid instruction family** — the dual of your valid opgen. Every instruction is a template plus field constraints; the illegal generator violates exactly one constraint at a time:

| Family | Valid constraint | Illegal stimulus |
|---|---|---|
| LOAD | funct3 ∈ {000,001,010,100,101} | funct3 ∈ {011,110,111} |
| OP (RV32I) | funct7 ∈ {0000000, 0100000} (+ext) | any other funct7 |
| SLLI/SRLI/SRAI (RV32) | shamt[5]=0, funct7 exact | shamt=32 (`funct7[0]=1`), wrong funct7 |
| JALR | funct3=000 | funct3 = 001…111 |
| LR.W | rs2=0 | rs2 ≠ 0 |
| FP (rm field) | rm ∉ {101,110} | rm = 101/110 |
| C.LWSP / C.JR | rd/rs1 ≠ 0 | rd/rs1 = 0 |
| C.ADDI4SPN / C.LUI / C.ADDI16SP | nzimm ≠ 0 | nzimm = 0 |
| SYSTEM funct3=0 | exact 5–6 encodings | everything else |

This is directly expressible with your existing infrastructure: each `gen_*` function has an implicit constraint set; an `gen_illegal_*` picks one constraint and draws from its complement.

**(b) Reserved major opcodes** — sweep the opcode map: `0001011`, `0101011`, `1011011`, custom/reserved blocks, and the SYSTEM/OP-IMM/OP funct3 holes.

**(c) Canonical illegal patterns** — `0x00000000` (illegal 16-bit), `0xFFFFFFFF`, all-ones parcels.

**(d) Pure random 32-bit words** — >99% of the 2³² space is illegal, so dumb fuzz is cheap broad coverage, with the systematic categories guaranteeing the *interesting* corners.

**(e) Config-matrix cases** — an encoding valid with M enabled is illegal with M disabled. The checker must run per CPU configuration: FP opcodes with F off, FENCE.I without Zifencei, compressed with C off (then *every* 16-bit form is illegal, including C.NOP).

### 2. The two subtle traps in building this

**HINTs must NOT be flagged illegal.** This is the classic mistake. Large parts of the compressed "reserved-looking" space are architecturally *valid NOPs*: `c.addi rd=x0`, `c.li rd=x0`, `c.mv rd=x0`, `c.slli rd=x0` or shamt=0, `c.srli/c.srai shamt=0` (c.srli64/c.srai64 HINTs on RV32/64). A naive "not in the instruction table ⇒ must trap" oracle produces false failures against spec-correct hardware. Your model already executes these as NOPs, so a model-as-oracle approach handles this correctly by construction.

**"Illegal" must not be confused with "traps with another cause."** ECALL/EBREAK trap but are *valid* instructions. `rm=DYN` with `frm∈{5,6,7}` is *state-dependent* illegality (only traps at execution given CSR state). CSR instructions on nonexistent/privileged CSRs are legal encodings that trap for environmental reasons. And encodings with `[1:0]=11, [4:2]=111` are ≥48-bit instruction prefixes — length-decode matters even though a minimal RV32 core traps them. The checker must compare **trap cause = 2 (illegal instruction), `xtval` = instruction bits, `xepc` = faulting pc** — not just "it trapped."

### 3. What "correctly recognizing" should check on the DUT

For each illegal stimulus, the strongest check is precision and *side-effect freeness*:

1. Illegal-instruction exception (cause 2), correct `mtval`/`mepc`, redirect to `mtvec`.
2. **No architectural state change**: no rd write (your JALR-funct3 bug from this session would have failed exactly this check — the DUT executed it), no memory write, no FP flag update, no CSR write, reservation untouched.
3. Determinism: same encoding → same behavior regardless of history.

That last property is why one-constraint-at-a-time generation beats pure fuzz for debugging: a failure maps to a single violated constraint.

### 4. Coverage: "all of them" means equivalence classes, and one space is fully enumerable

You can't enumerate 2³², but the invalid space collapses into finitely many classes: every illegal funct3/funct7 value per family (7-bit funct7 = ≤128 cases × ~10 families), every reserved rm, every rs2≠0 LR, every major-opcode hole. That's a few thousand targeted vectors — cheap to run exhaustively in simulation.

The **compressed space is fully enumerable**: 2¹⁶ = 65,536 parcels × your C/Zcf config — you can check *every single 16-bit encoding* against the model, no sampling at all. That's the gold-standard completeness answer for the C extension.

### 5. Guarding against self-deception

One caution, same as with the valid opgens: if the illegal-set is derived from your *decoder's* logic, a shared spec misreading cancels out (decoder accepts X, generator never calls X illegal). The invalid classes should be derived from the **spec's encoding tables**, and ideally spot-checked against an independent oracle (Spike/Sail, or even GNU `objdump` refusing to disassemble). Your model's strict decoders are in good shape for this after the recent fixes, but the JALR case shows why the oracle itself needs external anchoring for exactly this use case.

### Sketch of the structure

```
for each extension:
    for each instruction template:
        for each field constraint c:
            for each illegal value of c (bounded set):
                emit template with c violated, all other fields random-valid
    sweep reserved funct3/funct7/opcode holes
emit 0x00000000, 0xFFFFFFFF, N random words
per CPU config: repeat with extension disabled
oracle: model.step() → expect cause=2, mtval=instr, no state diff
        (HINT whitelist → expect NOP)
compare DUT vs oracle, lockstep
```

If you want, I can implement this as an `riscv_illegal_opgen.hpp` (constraint-violation generator + full 16-bit compressed sweep) plus a self-consistency test in the suite that asserts the model traps every generated illegal encoding with the right cause/mtval and no side effects — which doubles as a regression suite for decoder strictness and gives you a ready-made stimulus file format for RTL lockstep.

## User

> sounds really good ... please go ahead with it

## Assistant

Excellent — let's build it. While designing the HINT whitelist for the compressed sweep, I actually found **a sixth model bug**: per the C spec, `C.LUI rd=x0` with nzimm≠0 is a HINT (must execute as NOP), but the model's decoder traps it as illegal. The illegal-stimulus work pays off already. Let me fix that first:

## Assistant

Now the illegal-opcode generator header, with spec-table-derived classes:

## Assistant

Now the test suite additions — include, the new section N, and registration:

## Assistant

Now the main illegal-stimulus test section:

## Assistant

Generator bug (not a model bug): `(bits32 & 0x1FFF)` bleeds random bits into the opcode field `[1:0]`, so a "quadrant 0" sample can actually be quadrant 2. Fixing the masking:

## Assistant

All 131 tests pass. Now syncing the docs (README file overview, AGENTS.md layout):

## Assistant

Done — **131 tests, 0 failures**, warning- and UBSan-clean. Summary of what was built:

### New model bug found and fixed first

**`riscv_rv32c.hpp`**: `C.LUI rd=x0` with nzimm≠0 was trapped as illegal, but the C spec designates those code points as **HINTs** (must execute as NOP). The decoder now accepts them (the x0 write is discarded by the register file, giving exactly HINT behavior). Regression-tested both in the `test_rv32c` section and in the HINT whitelist. This is precisely the class of bug the illegal-stimulus approach was meant to catch — and it did, during whitelist design.

### New file: `riscv_illegal_opgen.hpp`

Spec-table-derived invalid-encoding generator (explicitly *not* derived from the project's decoders, so a shared misreading can't cancel out). 38 named classes in four categories:
- **Canonical patterns**: all-zeros, all-ones (≥48-bit prefix space)
- **Reserved major opcodes** from the spec's opcode map (custom-0/1/2/3, OP-32, ≥48-bit prefixes, reserved blocks)
- **Per-family constraint violations** — one violated constraint per sample, all other fields random-valid: LOAD/STORE/BRANCH/JALR bad funct3, SLLI/SRLI-slot violations (shamt[5], unassigned funct7, bad Zbb-unary rs2, ORC.B/REV8 wrong rs2), OP bad funct7/funct3 (via a spec-table legality mask, incl. PACK/Zbkb absent), MISC-MEM, SYSTEM f3=0 unassigned and f3=100 (hypervisor), nonexistent CSR addresses, 11 FP classes (bad funct7, reserved rm, FSQRT/FCVT/FMV/FCMP/FMINMAX/FSGNJ field violations, bad FLW/FSW funct3, bad MADD fmt), AMO bad funct3/funct5, LR rs2≠0
- **Compressed reserved encodings**: quad0/quad2 RV64 funct3s, nzimm=0 forms, C.LWSP rd=x0, C.JR rs1=x0, shamt[5]=1 custom space, RV64 ALU group

HINTs are documented as deliberately excluded (they're valid NOPs) — which is also how I caught and removed my own generator mistake (`bit12=1, rs1=0, rs2≠0` is C.ADD x0, a HINT, not "C.JALR rs1=x0").

### New test section `N. Illegal-opcode stimulus` (7 tests)

1. **Constructed classes** (38 × 300 samples): every encoding traps with cause 2, `mtval` = instruction bits, `mepc` = faulting pc, redirect to `mtvec` — and **zero architectural side effects** (all 31 regs, memory sentinel, `fcsr`, fregs, reservation verified unchanged).
2. **Exhaustive 16-bit sweep**: all 65,536 parcels through full CPU dispatch; every illegal one traps precisely with `mtval` = parcel and no register writes (also a UBSan sweep of the entire compressed decode space).
3. **HINT whitelist**: 11 encodings that must execute as NOPs (incl. the C.LUI x0 fix).
4. **Reserved compressed spot checks** with exact `mtval`.
5. **Config matrix**: valid opgen stimulus from each extension traps as illegal when that extension is disabled (C, F+Zcf, M, A, Zicsr, Zifencei, Zba, Zbb, Zbs, Zicond).
6. **SRET/SFENCE.VMA config-dependence**: illegal without S-mode, legal with it (incl. actual SRET privilege transition).
7. **Robustness**: 20k random words, trap-contract assertions.

Docs synced (README file overview + opgen section, AGENTS.md layout, test-file header). For RTL use, the class table in `riscv_illegal_opgen.hpp` gives you a ready stimulus source for lockstep checking; the model side already produces the expected cause/mtval contract to compare against.

Changes are uncommitted — say the word to commit and push.

## User

> Commit all current changes in the repository and push them.
> 
> Procedure:
> 1. Inspect what changed: run `git status --short` and `git diff` (plus
>    `git diff --cached` if something is already staged; use
>    `git log --oneline -5` to match the repo's commit-message style).
> 2. If there is nothing to commit, say so and stop (still push if there
>    are unpushed commits).
> 3. Compose the commit message:
>    - If the user provided a message as argument (``), use it verbatim.
>    - Otherwise write a meaningful message yourself based on the diff:
>      a concise subject line (≤ 72 chars, imperative mood, no trailing
>      period) that names the what and why, followed — only when the change
>      warrants it — by a blank line and a short body with details.
>    - Never use generic messages like "update" or "changes".
> 4. Before staging, review the untracked (`??`) files from `git status`.
>    If any look unintended (scratch files, secrets, editor backups,
>    binaries outside `build/`), ask the user whether to include them.
> 5. Stage everything (`git add -A`), commit with that message, then
>    `git push` (set the upstream with `-u` if the branch has none).
> 6. Show the resulting `git log -1 --stat` output and confirm the push
>    succeeded.

## Assistant

Committed and pushed successfully.

- **Commit** `6e5c08e` — "Add illegal-opcode stimulus generator and decoder-strictness tests" (5 files, +864/−3, new file `riscv_illegal_opgen.hpp`)
- **Push** — `4ab4235..6e5c08e  main -> main` to `github.com:maideas/rv32-complete-v3.git` ✓

The commit contains the illegal-opcode generator (38 spec-table-derived classes), the C.LUI HINT decoder fix it uncovered, the 7-test `N. Illegal-opcode stimulus` section (131 tests total, all passing), and the README/AGENTS.md doc sync. Working tree is clean.

## User

> are all hints part of the opcode generation ... are they all part of the randomized opcodes ?

## Assistant

Short answer: **no — the compressed HINT space is deliberately not part of the randomized valid-opcode streams**, though it *is* covered by tests. Here's the precise inventory:

### HINT coverage status

**Base RV32I HINTs** (e.g., `addi x0, x0, imm≠0` — the semihosting NOP-slide forms): **yes, covered**. The RV32I opgen draws rd/rs1 from 0–31 (`rng.reg()`), so `rd=x0`/`rs1=x0` combinations occur naturally in the random stream, and the decoder treats them as ordinary instructions discarding results.

**Compressed HINTs**: **not generated**. The C opgen was deliberately restricted to canonical encodings:
- `gen_c_addi`, `gen_c_li`, `gen_c_mv`, `gen_c_add`, `gen_c_slli` all use `reg_nz()` → `rd=x0` HINT forms never occur
- `gen_c_lui` excludes rd ∈ {0, 2} → `c.lui x0` HINT never occurs
- `shamt()` returns 1–31 → the shift-by-zero HINTs (`c.slli64`, `c.srli64`, `c.srai64`) never occur
- only the canonical `c.nop` (0x0001) is generated

So the full compressed HINT space — `c.addi x0`, `c.li x0`, `c.lui x0` (nzimm≠0), `c.mv x0`, `c.add x0`, `c.slli x0`, `c.slli64`, `c.srli64`, `c.srai64` — is absent from the random stimulus.

**But they are tested**, via the section N additions rather than the opgens:
- the **HINT whitelist test** (11 representative encodings asserted to execute as NOPs)
- the **exhaustive 65,536-parcel sweep**, which drives *every* HINT encoding through full CPU dispatch and verifies the trap contract (no HINT may trap)

### Gap assessment

For the model itself, coverage is effectively complete (the sweep is exhaustive). The real gap is for **RTL stimulus reuse**: if you stream opgen output at a hardware decoder, the hardware never sees HINTs from the random stream — and "HINT must decode as NOP, not trap" is exactly the kind of thing hardware gets wrong (as our own model did with `c.lui x0`).

The clean fix is small: add a `generate_hint()` to the RV32C opgen that samples the nine HINT families above (uniformly, with all their fields randomized), without touching the existing canonical generators or type table — so the coverage tests stay valid, and callers can mix HINTs into stimulus (e.g., `generate_mixed(p_hint)`), plus a round-trip test asserting every generated HINT executes trap-free on the CPU.

Want me to add that?

## User

> yes, please.

## Assistant

Adding the HINT generators to the RV32C opgen — kept separate from the canonical type table so existing coverage tests stay valid:

## Assistant

Now the tests — a HINT-family coverage/execution test and a mix-ratio test in section M, plus a local HINT predicate:

## Assistant

All 133 tests pass. Final doc sync (README opgen notes):

## Assistant

Done — **133 tests, 0 failures**, warning- and UBSan-clean.

### What was added

**`riscv_rv32c_opgen.hpp`** — nine HINT generators, one per compressed HINT family, with all their fields fully randomized:
- `c.addi x0` (nzimm≠0), `c.li x0` (any imm), `c.lui x0` (nzimm≠0)
- `c.mv x0` / `c.add x0` (rs2≠0)
- `c.slli x0` (any shamt 0–31), `c.slli64` (rd≠0, shamt=0), `c.srli64` / `c.srai64` (shamt=0)

They're deliberately kept **out of the `InstrType` table** (new `HintType` enum + `generate_hint()`, so canonical type-based generation and the existing coverage tests are unaffected), plus a `generate_mixed(p_hint)` that blends HINTs into the canonical stream at a configurable rate — the drop-in API for RTL stimulus.

**Test suite** (section M, +2 tests):
1. **All 9 families × 200 samples**: each generated encoding decodes non-illegal, executes trap-free through the full CPU, and leaves all 31 integer registers unchanged — the exact NOP-semantics contract hardware must implement (and the property our former `c.lui x0` bug violated).
2. **`generate_mixed(0.5)` over 4000 samples**: everything decodes valid, and both the canonical and HINT populations show up at the expected rates (using an independent `is_c_hint()` predicate in the test).

**Docs**: RV32C opgen header comment and README opgen notes now describe the canonical/HINT split and the new API.

So to answer the original question completely: base-ISA HINTs were already in the random streams, compressed HINTs are now generatable on demand (and mixed into streams), and they're verified both exhaustively (65,536-parcel sweep) and per-family (opgen + execution checks).

Changes are uncommitted — say the word to commit and push.

## User

> Commit all current changes in the repository and push them.
> 
> Procedure:
> 1. Inspect what changed: run `git status --short` and `git diff` (plus
>    `git diff --cached` if something is already staged; use
>    `git log --oneline -5` to match the repo's commit-message style).
> 2. If there is nothing to commit, say so and stop (still push if there
>    are unpushed commits).
> 3. Compose the commit message:
>    - If the user provided a message as argument (``), use it verbatim.
>    - Otherwise write a meaningful message yourself based on the diff:
>      a concise subject line (≤ 72 chars, imperative mood, no trailing
>      period) that names the what and why, followed — only when the change
>      warrants it — by a blank line and a short body with details.
>    - Never use generic messages like "update" or "changes".
> 4. Before staging, review the untracked (`??`) files from `git status`.
>    If any look unintended (scratch files, secrets, editor backups,
>    binaries outside `build/`), ask the user whether to include them.
> 5. Stage everything (`git add -A`), commit with that message, then
>    `git push` (set the upstream with `-u` if the branch has none).
> 6. Show the resulting `git log -1 --stat` output and confirm the push
>    succeeded.

## Assistant

No untracked files. Committing and pushing:

## Assistant

Committed and pushed successfully.

- **Commit** `37245e9` — "Add compressed-HINT generators to the RV32C opgen" (3 files, +162/−4)
- **Push** — `6e5c08e..37245e9  main -> main` to `github.com:maideas/rv32-complete-v3.git` ✓

Contains the nine HINT generators with `generate_hint()`/`generate_mixed()`, the two new tests (HINT families execute as register-preserving NOPs; mixed-stream ratio check), and the doc sync. 133 tests, all passing; working tree is clean.

## User

> can you run the valid opcode generators and check, that the model never traps ... and then the invalid opcode generators and check, that the model always traps ? ... or is this already part of the test suite ?

## Assistant

Good question — the honest answer is: **half of it is covered, half isn't (and can't be stated naively)**.

- **Invalid → always traps**: yes, fully covered (section N: 38 classes × 300 samples with the full cause-2/precise-mtval/no-side-effects contract, config matrix, sweep).
- **Valid → never traps**: only partially covered (Zicsr, F, and HINT streams have trap-free execution tests). And a blanket "valid never traps" is actually **false**: a *valid* `lw` can legitimately trap on a bad address (causes 4/5), `ecall`/`ebreak` trap by design (11/3). The correct universal invariants are:
  1. a valid opcode must **never trap as illegal** (cause 2) — always checkable;
  2. a valid opcode **never traps at all** when the environment is made safe (register-only instructions; or memory instructions with aligned, in-bounds register presets and misalignment enabled).

Let me add exactly that as a comprehensive test:

## Assistant

A failure — let me see the details:

## Assistant

Let me debug which encoding and cause triggers it:

## Assistant

Now it's the F stream failing — let me see which encoding/cause:

## Assistant

**Model bug #7 found** — and it's a good one. The mnemonic prints `flw f9, 3932(s5)`: the immediate field is `0xF5C` (−164 as a 12-bit signed value), but the model computes `+3932`. The `sign_extend()` helper takes the sign **bit position**, and `riscv_rv32f.hpp` calls it with `12` instead of `11` for its 12-bit immediates — so negative FLW/FSW immediates become large positive offsets. Let me check all call sites:

## Assistant

Only the two FLW/FSW sites are wrong (Zbb's 7/15 are correct). Fixing:

## Assistant

Now a regression test for negative FLW/FSW immediates:

## Assistant

Same x0 pattern: `fsw f30, -1700(zero)` now correctly computes address `0xFFFFF95C` and access-faults — the old test was passing *because of the bug*. x0-based access faults are architecturally correct and must be tolerated:

## Assistant

All **135 tests, 0 failures**. Here's the full answer and what came out of it:

### Was this already covered?

- **Invalid → always traps**: yes, already fully covered (section N: 38 classes × 300 samples with the complete cause-2/precise-mtval/no-side-effects contract, config matrix, exhaustive parcel sweep).
- **Valid → never traps**: only partially — and a blanket version is impossible, since valid instructions can *legitimately* trap (misaligned/data access faults, ECALL/EBREAK by design).

### What I added

A comprehensive test (**126: "Valid opgen stimulus never traps as illegal"**) running every extension's valid generator through injected CPU execution (3000 samples each) with two correct invariants:
1. **Never cause 2** — no valid encoding may trap as illegal, ever.
2. **Never any trap** in a data-safe environment (regs anchored to aligned in-bounds addresses, misalignment allowed) — for the register-only streams (M, Zba, Zbb, Zbs, Zicond), A with aligned addresses, and F/Zcf; ECALL/EBREAK (11/3) and x0-based access faults accepted as architecturally correct.

### It immediately caught a real model bug (#7)

The first run failed on `flw f9, 3932(s5)` — the immediate field was `0xF5C`, which is **−164** signed, but the model computed **+3932**. Root cause: `riscv_rv32f.hpp` called `sign_extend(bits, 12)` for its 12-bit FLW/FSW immediates, but the sign bit of a 12-bit field is at position **11**. Every negative FLW/FSW immediate computed `rs1 + large_positive` instead of `rs1 − |imm|` — a wrong-address bug, invisible until now because all prior F tests used non-negative offsets, and the "trap-free F stimulus" test was *passing because of it* (wrong addresses happened to stay in-bounds).

Fixes:
- `riscv_rv32f.hpp`: both call sites corrected to bit 11 (verified Zbb's 7/15 uses are correct; compressed FP offsets are unsigned and unaffected).
- New regression test (**54**): decoder-level sign-extension checks (−4, −8, −2048, +2047) plus execution-level FLW/FSW with negative offsets hitting the exact expected addresses.
- The pre-existing F stimulus test updated to tolerate x0-based access faults (now that addresses are computed correctly, `fsw f30, -1700(zero)` properly faults).

Final state: **135 tests, 0 failures**, UBSan-clean. Changes are uncommitted — say the word to commit and push.

## User

> Commit all current changes in the repository and push them.
> 
> Procedure:
> 1. Inspect what changed: run `git status --short` and `git diff` (plus
>    `git diff --cached` if something is already staged; use
>    `git log --oneline -5` to match the repo's commit-message style).
> 2. If there is nothing to commit, say so and stop (still push if there
>    are unpushed commits).
> 3. Compose the commit message:
>    - If the user provided a message as argument (``), use it verbatim.
>    - Otherwise write a meaningful message yourself based on the diff:
>      a concise subject line (≤ 72 chars, imperative mood, no trailing
>      period) that names the what and why, followed — only when the change
>      warrants it — by a blank line and a short body with details.
>    - Never use generic messages like "update" or "changes".
> 4. Before staging, review the untracked (`??`) files from `git status`.
>    If any look unintended (scratch files, secrets, editor backups,
>    binaries outside `build/`), ask the user whether to include them.
> 5. Stage everything (`git add -A`), commit with that message, then
>    `git push` (set the upstream with `-u` if the branch has none).
> 6. Show the resulting `git log -1 --stat` output and confirm the push
>    succeeded.

## Assistant

No untracked files. Committing and pushing:

## Assistant

Committed and pushed successfully.

- **Commit** `b745256` — "Fix FLW/FSW negative-immediate sign extension; add valid-stimulus trap test" (2 files, +144/−4)
- **Push** — `37245e9..b745256  main -> main` to `github.com:maideas/rv32-complete-v3.git` ✓

Contains the sign-extension fix (model bug #7), its regression test, the comprehensive valid-stimulus trap test, and the F-stimulus tolerance update. 135 tests, all passing; working tree is clean.

## User

> what would it mean to add a configuration to each of the opcode generators to allow a per-instruction-group enable. this would be very helpful during the implementation phase, where not yet all instructions have been implemented, or during debugging to switch off most of the instruction groups to focus on a few.

## Assistant

Great use case — that's essentially "maturity-scaled stimulus": the generator's output space tracks what the DUT actually implements, and you shrink the space to focus debugging. Here's what it means concretely for this codebase:

### Core idea

Every opgen already has a static `generators[]` table indexed by its `InstrType` enum (max 40 entries — RV32I). Since all tables fit in 64 bits, the natural mechanism is a **per-instruction enable mask** in each `OpcodeGenerator`, with **named group constants** layered on top so users don't hand-compose bits:

```cpp
// per-instruction granularity (groups are just predefined masks)
rv32i::opgen::OpcodeGenerator g(seed);
g.set_enabled_mask(rv32i::opgen::groups::LOADS |
                   rv32i::opgen::groups::STORES);     // bring-up: LSU only
g.enable(rv32i::opgen::InstrType::JAL, true);        // ... plus jumps
uint32_t op = g.generate_random();                   // only enabled types
```

Suggested group taxonomy (mask constants) per generator:

| Generator | Groups |
|---|---|
| **RV32I** | `UPPER`, `JUMPS`, `BRANCHES`, `LOADS`, `STORES`, `ALU_IMM`, `SHIFTS`, `ALU_REG`, `FENCE`, `SYSTEM` (ECALL/EBREAK) |
| **M** | `MUL`, `DIV` (bring-up gold: divider not ready → mask it out) |
| **A** | `LR_SC`, `AMO_LOGICAL`, `AMO_ARITH` |
| **F** | `LOAD_STORE`, `ARITH`, `MINMAX`, `FMA`, `CVT_F2I`, `CVT_I2F`, `MV`, `CMP`, `SGNJ`, `CLASS` |
| **C** | quadrants or functional groups (`C_ALU`, `C_MEM`, `C_FLOW`) — plus the HINT table |
| **Zicsr** | the 6 forms (or reg-forms vs imm-forms) |
| **Zba/Zbb/Zbs/Zicond** | single group each (or trivial subgroups) |
| **illegal_opgen** | same mechanism over its 38 classes — see the subtlety below |

### Semantics decisions that matter

1. **`generate_random()` respects the mask; `generate(type)` ignores it.** Explicit requests are honored (caller knows best); random sampling renormalizes *uniformly over enabled types* — not rejection-sampling from the full table, which would silently change type frequencies.
2. **Seed stability by default.** Default mask = all-ones with a fast path that consumes exactly the same RNG draws as today. This is critical: every existing seeded test (round trips, coverage, trap-free stimulus) must produce bit-identical streams, or you invalidate all of them.
3. **Auxiliary selectors must be mask-aware too.** `generate_alu()`, `generate_memory()`, `generate_no_control_flow()`, `generate_hint()`, `generate_mixed()` all carry their own static type lists — each filters by the same mask (e.g., `generate_mixed(p_hint)` draws HINTs only from enabled hint families; if all are masked, falls back to canonical).
4. **Empty mask needs a defined behavior** — cleanest is to treat `mask == 0` as a usage error documented in the API (or clamp to the first type), not silently emit garbage.
5. **C generator**: 27 types + 9 HINT families = 36 < 64, so one mask works with HINT bits above the type bits — no separate mask API needed.

### The interesting subtlety: the illegal opgen and the config matrix

The mask concept has a mirror-image role on the invalid side, tied to your existing config matrix:

- DUT **hasn't implemented extension X yet** → encodings of X *should* trap as illegal → they belong in the **illegal** stimulus (enable those illegal classes / run X's valid opgen as negative stimulus).
- DUT **has implemented X** → X's encodings belong in the **valid** stream, and the illegal classes targeting *internals* of X (e.g., "FSQRT.S rs2≠0") become relevant.

So during bring-up, a class migrates between the two generators as the implementation matures. A shared "implemented extensions" descriptor that configures both sides (valid masks, illegal-class mask, and the CPU model's own `CPUConfig` for lockstep) would make the whole flow coherent: one knob drives generator and golden model alike.

### What it costs / risks

- Mechanical change across 12 generator classes (~15–30 lines each: mask field, filtered sampling, group constants, docs). No model changes.
- Main risk is accidentally altering default RNG consumption — guarded by a regression test: *same seed + default mask ⇒ identical stream as the pre-change generator* (can be checked against the current behavior captured in existing seeded tests, which must all keep passing unchanged).
- New tests: mask filtering (only enabled types appear over N samples, frequencies ~uniform), group-constant correctness (each constant covers exactly its members), empty-mask behavior, hint/mixed interaction.

Net effect for your workflow: during bring-up you run lockstep with `mask = implemented-so-far`, expand it as the RTL grows; during debugging you mask everything except the suspect group and get clean traces without editing stimulus code.

Want me to implement it — starting with the uniform mask mechanism across all generators (seed-stable default), group constants, and the test additions?

## User

> sounds nice. please go ahead with it ...

## Assistant

This is a mechanical but wide change. I'll apply a uniform pattern across all generators: a 64-bit enable mask (seed-stable fast path when full), `enable()`/`set_enabled_mask()` API, named group constants, and mask-aware auxiliary selectors. Starting with RV32I:

## Assistant

Now RV32M and RV32A:

## Assistant

The A opgen's ordering helpers also draw random types — making them mask-aware too:

## Assistant

Now the RV32C opgen (unified mask over 27 types + 9 HINT families):

## Assistant

Simplifying the C fast path and updating its auxiliary selectors:

## Assistant

Now RV32F:

## Assistant

Now RV32FC, Zicsr, and Zifencei:

## Assistant

Now Zba, Zbb, Zbs, Zicond — checking their shared class template first:

## Assistant

Applying the uniform pattern to all four bitmanip/Zicond generators:

## Assistant

Now the illegal opgen — a masked `ClassGenerator` wrapper with group constants:

## Assistant

Now the test suite — a new `test_opgen_masks()` section:

## Assistant

My test expectation was wrong, not the code: C_ALU = 27 types − (4 MEM + 6 FLOW + 1 SYSTEM) = **16**, not 14. Fixing the test:

## Assistant

All 143 tests pass. Final doc sync — README opgen section, the RV32I opgen header (reference pattern), and the test-file header:

## Assistant

Done — **143 tests, 0 failures**, warning- and UBSan-clean. The per-instruction-group enable configuration is implemented uniformly across all 13 generators.

### What was added

**Uniform mask API** in every `OpcodeGenerator` (12 valid + the illegal opgen's new `ClassGenerator`):
```cpp
g.set_enabled_mask(rv32i::opgen::groups::LOADS | rv32i::opgen::groups::STORES);
g.enable(rv32i::opgen::InstrType::JAL, true);   // fine-grained on top
```

**Design points, as discussed:**
- **`generate_random()` samples uniformly over enabled types**; `generate(type)` ignores the mask (explicit request honored).
- **Seed-stable default**: full mask takes a fast path with byte-identical RNG consumption — verified by a test comparing default vs explicit-ALL streams across generators, and all pre-existing seeded tests pass unchanged.
- **Empty mask legalized to all-enabled** (documented, tested).
- **Auxiliary selectors are mask-aware** (`generate_alu()`, `generate_memory()`, `generate_branch()`, `generate_no_control_flow()`, A's ordering variants, F's family selectors, C's `generate_hint()`/`generate_mixed()`) with graceful fallback to the enabled set when their own list is fully masked out.
- **C generator**: unified 64-bit mask — bits 0–26 canonical types, bits 27–35 HINT families (`hint_bit()`, `enable_hint()`).
- **Illegal opgen**: new `ClassGenerator` with mask over its 38 classes + named indices, so invalid stimulus can be scoped the same way during bring-up.

**Group constants** per generator: RV32I (`LOADS`, `STORES`, `BRANCHES`, `JUMPS`, `ALU_IMM`, `SHIFTS`, `ALU_REG`, `UPPER`, `FENCE_OP`, `SYSTEM`), M (`MULTIPLY`/`DIVIDE` — the bring-up classic), A (`LR_SC`, `AMO_LOGICAL`, `AMO_ARITH`), F (`LOAD_STORE`, `ARITH`, `MINMAX`, `FMA`, `CVT`, `MV`, `CMP`, `SGNJ`, `CLASSIFY`), C (`C_MEM`, `C_FLOW`, `C_ALU`, `C_SYSTEM`, `HINTS`), Zcf (`LOADS`/`STORES`), Zicsr (`REG_FORMS`/`IMM_FORMS`), Zbb (6 groups), Zbs (4 groups), illegal opgen (`CANONICAL`, `BASE`, `CSR`, `FP`, `AMO`, `COMPRESSED`).

**Tests** (new section `M2`, +8 tests): mask filtering verified by decode for every extension, single-type masks, C hint/canonical mask interaction, seed stability, empty-mask legalization, aux-selector fallback, group-constant exact membership (popcounts + disjointness), illegal-class filtering. One test-expectation bug of mine fixed along the way (C_ALU is 16 members, not 14).

Docs synced: README opgen paragraph, RV32I opgen header (documents the reference pattern), test-suite header.

Changes are uncommitted — say the word to commit and push.

## User

> Commit all current changes in the repository and push them.
> 
> Procedure:
> 1. Inspect what changed: run `git status --short` and `git diff` (plus
>    `git diff --cached` if something is already staged; use
>    `git log --oneline -5` to match the repo's commit-message style).
> 2. If there is nothing to commit, say so and stop (still push if there
>    are unpushed commits).
> 3. Compose the commit message:
>    - If the user provided a message as argument (``), use it verbatim.
>    - Otherwise write a meaningful message yourself based on the diff:
>      a concise subject line (≤ 72 chars, imperative mood, no trailing
>      period) that names the what and why, followed — only when the change
>      warrants it — by a blank line and a short body with details.
>    - Never use generic messages like "update" or "changes".
> 4. Before staging, review the untracked (`??`) files from `git status`.
>    If any look unintended (scratch files, secrets, editor backups,
>    binaries outside `build/`), ask the user whether to include them.
> 5. Stage everything (`git add -A`), commit with that message, then
>    `git push` (set the upstream with `-u` if the branch has none).
> 6. Show the resulting `git log -1 --stat` output and confirm the push
>    succeeded.

## Assistant

No untracked files. Committing and pushing:

## Assistant

Committed and pushed successfully.

- **Commit** `e28592e` — "Add per-instruction-group enable masks to all opcode generators" (15 files, +908/−85)
- **Push** — `b745256..e28592e  main -> main` to `github.com:maideas/rv32-complete-v3.git` ✓

Contains the uniform enable-mask API across all 13 generators with named group constants, mask-aware auxiliary selectors, seed-stable defaults, the masked `ClassGenerator` for the illegal opgen, the 8-test `M2` section (143 tests total, all passing), and the doc sync. Working tree is clean.

## User

> can you please add to README.md a comprehensive guide to the opcode generator usage (including the groups handling) and reference model stepping with given opcode value. please also explain how a model and HDL lockstep could be set up.

## Assistant

This is a documentation task — let me load the markdown-authoring skill first:

## Assistant

Now the two new top-level sections — the generator guide and the lockstep guide:

## Assistant

Per the skill's verification rule, let me check my link targets and add a cross-reference from the file-overview to the new guide:

## Assistant

All links verified. Let me sanity-check the rendered structure and confirm the anchors resolve to real headings:

## Assistant

Structure is correct. One accuracy bug in my example: `InstrType::MUL` doesn't exist in the RV32I opgen — fixing:

## Assistant

Done — README.md now has comprehensive guides for all three topics (143 tests still pass; docs-only change). What was added:

### 1. `### Opcode-injection stepping` (in "Using the model")
Full contract of `step(Bus&, uint32_t opcode)` with example: no fetch (no instruction access faults/side effects, caller keeps `pc` coherent), interrupts still sampled first (`r.interrupt`, `instr_size == 0`), bus still used for data accesses with precise `mtval`, everything else identical to fetched execution — with a pointer to the state-for-state equivalence test in the suite.

### 2. `## Opcode generators (test stimulus)` — complete usage guide
- **Basic usage**: construction/seeding, `generate_random()`, `generate(type)`, sequences, auxiliary selectors, seed-stability guarantee.
- **Generator-specific selectors table**: C HINT API (`generate_hint`, `generate_mixed`), A ordering helpers, F family selectors, Zicsr config-aware CSR pools, Zifencei field randomization.
- **Enable masks and groups**: semantics (uniform-over-enabled sampling, explicit `generate()` ignores mask, empty-mask legalization, aux-selector fallback), mask composition with `type_bit()`/`hint_bit()`, and a **complete group-constants reference table** for all 12 generators.
- **Illegal-opcode stimulus**: `ClassGenerator` usage, `ClassInfo` fields, class-group constants, the cause-2/mtval/no-side-effects contract, and why HINTs are excluded.

### 3. `## Lockstep cosimulation with an HDL implementation`
- ASCII architecture diagram (one stimulus source → model + DUT → per-instruction compare).
- **Setup checklist**: match `CPUConfig` to RTL parameters, share the memory image, drive identical masked stimulus (with code example), and a comparison table of what to check per instruction (trap/cause/xtval/xepc, next PC, regfiles, memory writes, privilege/CSRs).
- **Recommended 6-phase bring-up flow**: canonical groups one at a time → full valid space → HINT mix → illegal space → config matrix → interrupts/traps — leveraging exactly the mask feature from the last commit.
- **Caveats**: interrupt timing, counter-semantics divergence, WARL legalization matching (linked to the CSR reference), host-FPU bit-exactness incl. the RMM approximation, and delivery mechanisms (Verilator direct-link vs DPI-C vs file/socket batch).

Per the project's markdown conventions, all mentioned `.md` files are links with verified targets (`doc/riscv_csr_reference.md`, plus the two in-page anchors), and a cross-reference from the file-overview to the new guide was added. Also fixed one slip in my own example (referenced a nonexistent `InstrType::MUL` in the RV32I generator).

Change is uncommitted — say the word to commit and push.

## User

> Commit all current changes in the repository and push them.
> 
> Procedure:
> 1. Inspect what changed: run `git status --short` and `git diff` (plus
>    `git diff --cached` if something is already staged; use
>    `git log --oneline -5` to match the repo's commit-message style).
> 2. If there is nothing to commit, say so and stop (still push if there
>    are unpushed commits).
> 3. Compose the commit message:
>    - If the user provided a message as argument (``), use it verbatim.
>    - Otherwise write a meaningful message yourself based on the diff:
>      a concise subject line (≤ 72 chars, imperative mood, no trailing
>      period) that names the what and why, followed — only when the change
>      warrants it — by a blank line and a short body with details.
>    - Never use generic messages like "update" or "changes".
> 4. Before staging, review the untracked (`??`) files from `git status`.
>    If any look unintended (scratch files, secrets, editor backups,
>    binaries outside `build/`), ask the user whether to include them.
> 5. Stage everything (`git add -A`), commit with that message, then
>    `git push` (set the upstream with `-u` if the branch has none).
> 6. Show the resulting `git log -1 --stat` output and confirm the push
>    succeeded.

## Assistant

Only README.md changed, nothing untracked. Committing and pushing:

## Assistant

Committed and pushed successfully.

- **Commit** `d139b7c` — "Document opgen usage, opcode injection, and HDL lockstep setup in README" (1 file, +263/−5)
- **Push** — `e28592e..d139b7c  main -> main` to `github.com:maideas/rv32-complete-v3.git` ✓

The README now contains the three guides: the complete opcode-generator usage reference (selectors, enable masks, group-constants table, illegal opgen), the opcode-injection stepping contract, and the HDL lockstep setup guide with the 6-phase bring-up flow. Working tree is clean.

## User

> Export the current pi session conversation (user + assistant messages) to
> a markdown file.
> 
> First, derive from the conversation:
> 
> 1. a short title (one line, ≤ 60 chars, no `#` prefix) — e.g.
>    "RISC-V dual-issue design Q&A"
> 2. a short introduction (2–4 sentences) summarizing what the session
>    covered and the key takeaways
> 
> Filename: use `` if a non-empty argument was given. Otherwise derive a
> meaningful filename yourself from the title: lowercase, words joined by
> underscores, filesystem-safe characters only, with a `.md` extension
> (e.g. "RISC-V dual-issue design Q&A" → `riscv_dual_issue_design_qa.md`).
> If the derived file already exists, append a short suffix (e.g. `_2`)
> instead of overwriting it.
> 
> Then run from the current working directory:
> 
> ```bash
> python3 /home/maik/.pi/agent/scripts/export_md.py <filename.md> \
>     --title "<short session summary>" \
>     --intro "<introduction paragraph>"
> ```
> 
> The title becomes the markdown main header (`# <title>`) and the intro is
> placed directly below it as a lead-in paragraph. Always pass both.
> 
> The script reads the active session from `$PI_SESSION_FILE` automatically.
> User messages become `## User` blockquote sections, assistant replies
> `## Assistant` sections. Thinking blocks and tool calls are omitted
> (`--with-thinking` / `--with-tools` exist as optional flags).
> 
> After exporting:
> 
> - Show the output path and size.
> - If the user asked to "replace" an existing export (e.g. an `.html`
>   export), remove the old file after the markdown file is written
>   successfully.
> - Only commit/push if the user explicitly asked.

## Assistant

No collision — exporting:
