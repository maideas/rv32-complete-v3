# RISC-V S/U-mode review, spec fixes, and test expansion

This session reviewed the uncommitted Supervisor/User-mode implementation in the RV32 reference model, found and fixed two privileged-spec deviations (M-mode exception delegation, medeleg[11] writability) plus MRET's MPP reset behavior, and expanded the test suite from 88 to 98 tests. The changes were committed via a feature branch merged into main. It also added two pi slash commands (/gg for the git graph, /export-md converted from a skill) and cleaned up comment grammar.

## User

> please analyze the files in this directory and have a look at the local changes. evaluate the changes and check them for correctness and completeness. do not commit anything yet.

## Assistant

Now let me check how `i_executor.priv` stays in sync after privilege changes, and build/test:

## Assistant

Let me verify a few spec-sensitive details:

## Assistant

Confirmed two spec deviations. Cleaning up the scratch file:

## Assistant

### Review of local changes

The working tree adds **Supervisor and User mode support** (phases 1, 2, 3, 5 of the plan documented in `riscv_s_and_u_modes_implementation.md`, which is itself the untracked session export). Modified: `riscv_cpu.hpp`, `riscv_rv32i.hpp`, `riscv_zicsr.hpp`, `test_riscv_model.cpp`.

**Build status:** `make test` → **88 tests, 0 failures**, clean under `-Wall -Wextra -Wpedantic -fsanitize=undefined`.

#### What is correct and well done

- **Trap entry/exit** (`enter_m_trap`/`enter_s_trap`/`do_mret`/`do_sret`): MPIE/SPIE/MIE/SIE/MPP/SPP save-restore semantics match the privileged spec; `MPRV` cleared on MRET to non-M and on SRET — correct.
- **Interrupt handling**: `interrupt_globally_enabled` implements the architectural rules (lower-priv interrupts never taken from higher mode; higher-priv always taken; same mode gated by xIE). Priority order MEI > MSI > MTI > SEI > SSI > STI is correct, including vectored `stvec`/`mtvec`.
- **WFI / TW, SRET / TSR, SFENCE.VMA / TVM, SATP / TVM** gating matches the spec (zero-length WFI timeout is a legal implementation choice).
- **CSR work**: `sstatus`/`sie`/`sip` aliasing through `mideleg`, `mcounteren`/`scounteren` gating of `cycle`/`time`/`instret`, `time` aliasing `mcycle`, `mip` write mask (MSIP always, SSIP only with S), `misa.S/U` bits, WARL legalization of `MPP`/`STVEC`/`SATP` — all sound.
- **Privilege propagation**: `sync_executor_config()` runs at the top of every `step()`, so `i_executor.priv` (used for the ECALL cause) is always current — no staleness bug.
- **SFENCE.VMA decode mask** `0xFE007FFF` correctly fixes funct7/funct3/rd/opcode while leaving rs1/rs2 free.
- **Backward compatibility**: defaults keep M-only behavior; existing tests were updated only where behavior legitimately changed (`time` alias, `mip` MSIP-writable).
- Tests are thorough (11 new tests incl. a good `enter_mode` helper).

#### Issues found

**1. Bug — M-mode exceptions must never be delegated** (`riscv_cpu.hpp`, `delegate_target`). Per the privileged spec, `medeleg` only applies to traps originating in S/U mode; traps taken while in M-mode always go to M. I verified with a scratch test: `medeleg[3]=1`, EBREAK executed in M-mode → trap entered **S-mode** (scause=3, pc=stvec). Fix: in `delegate_target`, return MACHINE when `csrs.get_privilege() == PrivilegeLevel::MACHINE`.

**2. Bug — `medeleg[11]` (ECALL-from-M) must be read-only zero** (`riscv_zicsr.hpp`). The write mask `value & 0xFFFFu` permits bit 11. Readback confirmed `medeleg[11] == 1`. Fix: mask with `0xF7FFu`.

**3. Minor — MPP reset value after MRET.** `do_mret` sets MPP to M (3); the spec says xPP is set to the least-privileged supported mode (U when U-mode is implemented). Many implementations do hardwire this, so it's defensible, but with a writable-MPP WARL field it's a deviation worth a comment at minimum.

**4. Cosmetic** (`riscv_zicsr.hpp` `set_f_extension`): `v &= mstatus_legal_mask()` uses flags as configured at that moment; if `set_s_mode`/`set_u_mode` were called after `set_f_extension`, stale legalization could leak. Current call order in `CPU::reset()` is safe, but the coupling is fragile — worth a comment or reordering guard.

#### Completeness assessment

Everything planned for phases 1–3 and 5 is present: privilege CSRs, delegation, S-mode interrupts, xRET, ECALL causes, TVM/TW/TSR, counter enables. MMU/PMP (phase 4/6) intentionally absent — `satp` stores values without translation, which is documented in the export. `sedeleg`/`sideleg` (U-mode delegation) explicitly out of scope. Test coverage is good; the two bugs above are exactly the cases not covered — I'd suggest adding regression tests for "M-mode exception with medeleg set stays in M" and "medeleg[11] read-only zero" when fixing.

Nothing committed, as requested. Want me to fix issues 1 and 2 (plus regression tests)?

## User

> 1. please first create a new branch and commit and push the current local changes to it.then merge back to main branch.

## User

> 1) please first create a new branch and commit and push the current local changes to it. 2) then merge back to main branch. 3) fix all issues based on your analysis. 4) commit and push to main

## Assistant

I'll follow those steps. First, branch, commit, push, merge back:

## Assistant

Now fixing the issues on main. **Fix 1** — M-mode exceptions must never be delegated:

## Assistant

**Fix 3** — MRET sets MPP to the least-privileged supported mode (spec):

## Assistant

**Fix 4** — config-order fragility in `CPU::reset()`:

## Assistant

Now add regression tests:

## Assistant

All 91 tests pass. Committing and pushing to main:

## Assistant

All steps completed:

1. **Branch + commit + push:** Created `feature/s-and-u-modes`, committed the S/U-mode implementation (5 files, incl. the session export), and pushed it (`b7fb028`).
2. **Merge to main:** Merged back with `--no-ff` ("Merge branch 'feature/s-and-u-modes'").
3. **Fixes applied on main:**
   - **Bug 1 — M-mode exception delegation:** `delegate_target()` in `riscv_cpu.hpp` now returns MACHINE for exceptions taken in M-mode; `medeleg` only applies to traps from S/U (interrupts unaffected — already correctly filtered by `interrupt_globally_enabled()`).
   - **Bug 2 — `medeleg[11]` read-only zero:** write mask changed `0xFFFF` → `0xF7FF` in `riscv_zicsr.hpp`.
   - **Issue 3 — MRET MPP reset:** `do_mret()` now sets MPP to the least-privileged supported mode (U when U-mode is implemented, else M), per the privileged spec.
   - **Issue 4 — config ordering:** `set_s_mode`/`set_u_mode` now called before `set_f_extension()` in `CPU::reset()`, with a comment explaining the dependency.
   - **Regression tests added:** M-mode exceptions never delegated, `medeleg[11]` RO-zero, MRET MPP reset value (both S/U config and M-only config).
4. **Commit + push:** All 91 tests pass (UBSan/warning-clean); pushed to `main` (`7917e4e`), which also carried the merge commit to origin.

## User

> please list the last 25 entries of the git graph

## User

> please go on

## User

> please go on

## Assistant

The repository only has 11 commits total, so the full graph fits within 25 entries:

```
* 7917e4e (HEAD -> main, origin/main) Fix trap delegation and MRET spec deviations
*   87da9bd Merge branch 'feature/s-and-u-modes'
|\
| * b7fb028 (feature/s-and-u-modes) Add Supervisor and User mode support (trap delegation, S-mode interrupts, xRET, TVM/TW/TSR)
|/
* 4702010 notes.md: short topic headers, questions as blockquotes, demote answer sections
* 49a5fda Replace notes.html with markdown version (notes.md)
* 6bc5f3d Add notes.html: session notes on RISC-V extensions and dual-issue design
* e33d1da rv32c: test c.addi16sp reserved encoding; add Makefile, AGENTS.md, .gitignore
* d3b1297 removed FENCE.I code from riscv_rv32i_opgen.hpp
* 2ae23c3 fixed C_ADDI16SP decoding case, in which nzimm is 0
* 4f66b00 initial
* f13d6d1 Initial commit
```

The top three entries are from our session: the feature-branch commit `b7fb028`, the merge commit `87da9bd`, and the spec-fix commit `7917e4e` — all pushed to origin.

## User

> does it make sense to add more tests for the latest changes, or is ecerything already covered

## Assistant

Not everything is covered. The core paths are tested, but reviewing the new code against the test suite, I see meaningful gaps — roughly in priority order:

**Interrupt handling (least covered area)**
1. **Priority order with multiple pending interrupts** (MEI > MSI > MTI > SEI > SSI > STI) — only a single SSI case is tested.
2. **M-mode interrupt taken while executing in S/U mode** (higher-privilege interrupts are always taken regardless of xIE) — untested.
3. **S-delegated interrupt *not* taken while in M mode** — untested.
4. **S interrupt in S mode with SIE=0 → not taken** (global-enable gating) — untested.
5. **Non-vectored `stvec` (mode 0)** for both exceptions and interrupts — only vectored mode is tested.
6. **`stvec` WARL legalization** (writing mode 2/3 reads back 0) — untested.

**CSR behavior**
7. **`sstatus` read/write subset** — write SIE/SPIE/SPP/SUM/MXR through sstatus, verify other mstatus bits untouched and FS/SD visible on read. Untested.
8. **`sip` writes** — only `sie` writes are tested; sip should only modify delegated *and* SW-writable bits (SSIP).
9. **SATP access from S with TVM=1 → illegal instruction** (the check in `riscv_cpu.hpp`) — TVM is only tested for SFENCE.VMA, not for the CSR access path.
10. **M-only config: S-mode mstatus bits (SIE/SPIE/SPP/SUM/TVM/TSR/TW) read as zero** — the `mstatus_legal_mask()` path with S disabled is untested.
11. **U-only config (no S)**: MPP=0 legalized to M; U-mode counter access denied when `scounteren` doesn't exist — untested edge configuration.
12. **`sepc` WARL masking** (IALIGN=16 with C, IALIGN=32 without) — untested.

**Instruction legality**
13. **MRET executed in S or U mode → illegal instruction** (the `exec.mret` privilege check) — untested.
14. **MRET/SRET clearing MPRV** when returning to less-privileged mode — untested.
15. **M-mode CSR access from U/S → illegal** (e.g., reading `mstatus` from U) — partially covered by pre-existing tests, but not for the new S-mode setup.

Items 1–4, 7, 9, and 13 are the ones I'd consider genuinely important, since they exercise decision logic in the new code rather than just plumbing. The rest are nice-to-have robustness checks.

Want me to add these tests (or a subset, e.g. the important seven)?

## User

> please add the important ones

## Assistant

Let me check the `step()` flow first to write the interrupt tests correctly:

## Assistant

The standalone case passes, so the test harness's `make_sys` must differ:

## Assistant

The test itself has a bug: `enter_mode` preserves the TVM bit set in the first step, so the "S mode, TVM=0" case actually ran with TVM=1. Fixing the test:

## Assistant

All 98 tests pass. Committing and pushing:

## Assistant

Done. Added the seven important test cases to `test_riscv_model.cpp` (section L):

1. **Interrupt priority order** — MEI > MSI > MTI in M-mode with multiple lines pending, plus SEI > SSI for delegated S-mode interrupts.
2. **M-mode interrupt taken while in S mode** — verifies higher-privilege interrupts fire regardless of `mstatus.MIE`, and MPP records S as the source mode.
3. **S-delegated interrupt not taken in M mode** — pending SSIP with MIE=1 retires a NOP normally.
4. **S interrupt in S mode gated by SIE** — not taken with SIE=0, taken after enabling SIE.
5. **`sstatus` subset behavior** — reads include S bits + hardwired FS/SD; writes only touch SIE/SPIE/SPP/SUM/MXR, leaving MIE/TVM intact.
6. **SATP access from S mode with TVM=1 → illegal** — including the negative cases (M-mode with TVM=1 allowed, S-mode with TVM=0 allowed).
7. **MRET illegal in U and S modes.**

While writing test 6 I initially hit a failure — it turned out to be a bug in my own test (the `enter_mode` helper preserved TVM from the previous step), not in the model. Fixed the test; the implementation behaved correctly.

Result: **98 tests, 0 failures**, committed as `7dc9453` and pushed to `main`.

## User

> please make a slash command named "gg" of "list the last 25 entries of the git graph"

## Assistant

Created the prompt template at `/home/maik/.pi/agent/prompts/gg.md` (global, so it's available in every project).

Usage:
- `/gg` — shows the last 25 entries of the git graph
- `/gg 10` — shows the last 10 entries (optional count argument)

It runs `git log --graph --oneline --decorate --all -<count>` and presents the output as a code block with a short summary of anything noteworthy (unpushed commits, recent merges, etc.).

## User

> Show the last 25 entries of the git graph:
> 
> ```bash
> git log --graph --oneline --decorate --all -25
> ```
> 
> Present the output as a code block, followed by a one-line summary if
> there is anything noteworthy (e.g. unpushed commits, recent merges).

## Assistant

```
* 7dc9453 (HEAD -> main, origin/main, origin/HEAD) Add tests for interrupt prioritization/gating, sstatus subset, SATP TVM, MRET legality
* 7917e4e Fix trap delegation and MRET spec deviations
*   87da9bd Merge branch 'feature/s-and-u-modes'
|\
| * b7fb028 (origin/feature/s-and-u-modes, feature/s-and-u-modes) Add Supervisor and User mode support (trap delegation, S-mode interrupts, xRET, TVM/TW/TSR)
|/
* 4702010 notes.md: short topic headers, questions as blockquotes, demote answer sections
* 49a5fda Replace notes.html with markdown version (notes.md)
* 6bc5f3d Add notes.html: session notes on RISC-V extensions and dual-issue design
* e33d1da rv32c: test c.addi16sp reserved encoding; add Makefile, AGENTS.md, .gitignore
* d3b1297 removed FENCE.I code from riscv_rv32i_opgen.hpp
* 2ae23c3 fixed C_ADDI16SP decoding case, in which nzimm is 0
* 4f66b00 initial
* f13d6d1 Initial commit
```

Nothing noteworthy: `main` is fully pushed and in sync with origin; the repo has 12 commits total, all shown. The `feature/s-and-u-modes` branch is merged and could be deleted if you want to tidy up.

## User

> please check all code comments for correct grammar

## Assistant

Now the remaining headers:

## Assistant

Let me check the remaining comment styles (block comments and file headers):

## Assistant

I reviewed comments across all 12 headers and the test suite. Overall quality is high. I found a handful of minor grammar/wording issues worth fixing:

1. `riscv_rv32i.hpp` — "needed for ECALL encoding" — the privilege is used to select the ECALL *cause*, not encoding.
2. `riscv_zifencei.hpp` — comma splice ("...should all be 0 but implementations must ignore them").
3. `riscv_rv32a.hpp` — AMOADD comment is misleading ("mem[rs1] = rd + rs2" — rd holds the *old* value).
4. `test_riscv_model.cpp` — missing article: "time is alias of mcycle" / "time is now alias".
5. `riscv_rv32i.hpp` — "are decoded already by decode()" → "already decoded".

Fixing them:

## Assistant

Comment review complete. I checked all comments in the 12 header files and the test suite. The overall quality is high — I found and fixed 6 minor grammar/wording issues:

| File | Fix |
|------|-----|
| `riscv_rv32i.hpp` | "needed for ECALL encoding" → "needed to select the ECALL cause" (it selects the trap cause; the encoding is fixed) |
| `riscv_rv32i.hpp` | "are decoded already by decode()" → "are already decoded by decode()" |
| `riscv_zifencei.hpp` | Fixed comma splice: "imm, rs1, and rd should all be 0, but implementations must ignore them" |
| `riscv_rv32a.hpp` | Misleading AMOADD comment: "mem[rs1] = rd + rs2" → "mem[rs1] = mem[rs1] + rs2" (rd holds the *old* value) |
| `test_riscv_model.cpp` | "time is alias of mcycle" → "time is an alias of mcycle" (missing article, 2 places) |

Everything else — including all comments added in the S/U-mode changes — was grammatically correct. Still 98 tests, 0 failures. The fixes are uncommitted in the working tree; let me know if you want them committed.

## User

> yes, please

## Assistant

Committed as `5d5d68b` and pushed to `main`. Working tree is clean, all 98 tests passing.

## User

> Show the last 25 entries of the git graph:
> 
> ```bash
> git log --graph --oneline --decorate --all -25
> ```
> 
> Present the output as a code block, followed by a one-line summary if
> there is anything noteworthy (e.g. unpushed commits, recent merges).

## Assistant

```
* 5d5d68b (HEAD -> main, origin/main, origin/HEAD) Fix minor grammar and wording issues in code comments
* 7dc9453 Add tests for interrupt prioritization/gating, sstatus subset, SATP TVM, MRET legality
* 7917e4e Fix trap delegation and MRET spec deviations
*   87da9bd Merge branch 'feature/s-and-u-modes'
|\
| * b7fb028 (origin/feature/s-and-u-modes, feature/s-and-u-modes) Add Supervisor and User mode support (trap delegation, S-mode interrupts, xRET, TVM/TW/TSR)
|/
* 4702010 notes.md: short topic headers, questions as blockquotes, demote answer sections
* 49a5fda Replace notes.html with markdown version (notes.md)
* 6bc5f3d Add notes.html: session notes on RISC-V extensions and dual-issue design
* e33d1da rv32c: test c.addi16sp reserved encoding; add Makefile, AGENTS.md, .gitignore
* d3b1297 removed FENCE.I code from riscv_rv32i_opgen.hpp
* 2ae23c3 fixed C_ADDI16SP decoding case, in which nzimm is 0
* 4f66b00 initial
* f13d6d1 Initial commit
```

Nothing noteworthy: `main` is fully in sync with origin; all 13 commits shown (that's the whole history). The merged `feature/s-and-u-modes` branch is still around if you want to delete it.

## User

> please transform the export-md skill into a real slash command and afterward remove the skill

## Assistant

Done. The export-md skill is now a real slash command:

1. **Moved the script** to `/home/maik/.pi/agent/scripts/export_md.py` (so it survives the skill removal).
2. **Created the prompt template** `/home/maik/.pi/agent/prompts/export-md.md` with `argument-hint: "<filename.md>"`, containing the same instructions the skill had (derive title + intro, run the script with `--title`/`--intro`, show output path/size afterward, only commit if asked).
3. **Removed the skill** — `/home/maik/.pi/agent/skills/` is now empty.

Usage is unchanged: `/export-md notes.md`. The autocomplete dropdown will show the filename hint and description.

## User

> Export the current pi session conversation (user + assistant messages) to
> the markdown file ``.
> 
> First, derive from the conversation:
> 
> 1. a short title (one line, ≤ 60 chars, no `#` prefix) — e.g.
>    "RISC-V dual-issue design Q&A"
> 2. a short introduction (2–4 sentences) summarizing what the session
>    covered and the key takeaways
> 
> Then run from the current working directory:
> 
> ```bash
> python3 /home/maik/.pi/agent/scripts/export_md.py  \
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

It looks like `/export-md` was invoked without a filename argument. What should the output file be called? For example: `/export-md riscv_s_u_mode_review_and_testing.md`

## User

> please add the requirement of a file name from the export-md slash command and change it to choose a meaningful file name itself

## User

> please go ahead

## Assistant

Updated `/home/maik/.pi/agent/prompts/export-md.md`:

- **Argument is now optional** — `argument-hint: "[filename.md]"`.
- If no filename is given, the command **derives one from the session title**: lowercase, underscores, filesystem-safe, `.md` extension (e.g. `riscv_s_u_mode_review_and_testing.md`).
- **Collision handling**: if the derived file already exists, a suffix like `_2` is appended instead of overwriting.

So `/export-md` now works standalone, and `/export-md my-notes.md` still honors an explicit name. Want me to try it out on this session?

## User

> Export the current pi session conversation (user + assistant messages) to
> the markdown file ``.
> 
> First, derive from the conversation:
> 
> 1. a short title (one line, ≤ 60 chars, no `#` prefix) — e.g.
>    "RISC-V dual-issue design Q&A"
> 2. a short introduction (2–4 sentences) summarizing what the session
>    covered and the key takeaways
> 
> Then run from the current working directory:
> 
> ```bash
> python3 /home/maik/.pi/agent/scripts/export_md.py  \
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
