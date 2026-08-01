# CSR Reference — RV32 Reference Model

This document describes the Control and Status Registers (CSRs)
implemented by the model (`riscv_zicsr.hpp`, integrated by
`riscv_cpu.hpp`): what each CSR is for, how it behaves on reads and
writes, and how the CSR state changes on privilege-mode transitions,
exceptions and interrupts.

The implementation follows the RISC-V Privileged Architecture v1.12
(ratified). Machine mode is always present; Supervisor and User modes
are optional (`CPUConfig::enable_s_mode` / `enable_u_mode`).

## General access rules

- **Existence.** Only the CSRs of the configured implementation exist.
  Accessing any other CSR address raises an *illegal-instruction*
  exception with `xtval` = the faulting instruction bits.
- **Privilege.** Address bits `[9:8]` encode the minimum privilege
  (0=U, 1=S, 3=M); accessing a CSR from a lower privilege raises an
  *illegal-instruction* exception.
- **Read-only CSRs.** Address bits `[11:10] == 11` mark read-only CSRs
  (counters, machine information). A write attempt raises an
  *illegal-instruction* exception.
- **WARL fields** are legalized on write: reserved or unsupported
  values are converted to a legal one rather than trapping (details per
  CSR below).
- **Read/write suppression.** Per the spec, `CSRRW(I)` with `rd = x0`
  performs no CSR read, and `CSRRS/C(I)` with `rs1`/`uimm = 0`
  performs no CSR write — permissions are checked accordingly.
- **Backdoors.** `CSRFile::set()`/`get()` bypass legalization and
  aliasing and are meant for initialization and state load/save;
  `read()`/`write()` are the architectural paths used by CSR
  instructions. Optional read/write callbacks observe (reads may
  override) the *backing* register — aliases are resolved first, so
  `fflags`/`frm` invoke the callback with `fcsr`, `cycle` with
  `mcycle`, etc.

## CSR map

Legend: **MRW/MRO** = machine read-write/read-only, **SRW** =
supervisor, **URW/URO** = user. "Exists" notes the configuration
condition beyond the default.

### Floating-point CSRs (F extension)

| Address | Name | Access | Description |
|---|---|---|---|
| 0x001 | `fflags` | URW | Accrued FP exception flags (NV/DZ/OF/UF/NX, bits [4:0]). Alias into `fcsr`. |
| 0x002 | `frm` | URW | Dynamic FP rounding mode (bits [7:5] of `fcsr`). Alias into `fcsr`. |
| 0x003 | `fcsr` | URW | FP control/status: `frm` ∥ `fflags`. Single storage cell backs all three addresses. |

All three exist only when the F extension is enabled. Writes are
masked to the implemented bits (`fflags`: 0x1F, `frm`: 0x7,
`fcsr`: 0xFF). The F executor reads `fcsr` before and writes it back
after every FP instruction, so accrued flags and rounding mode are
architecturally visible.

### User counter shadows (read-only)

| Address | Name | Access | Description |
|---|---|---|---|
| 0xC00 | `cycle` | URO | Read-only alias of `mcycle` |
| 0xC01 | `time` | URO | Read-only alias of `mcycle` (no separate RTC) |
| 0xC02 | `instret` | URO | Read-only alias of `minstret` |
| 0xC80–0xC82 | `cycleh`, `timeh`, `instreth` | URO | Upper halves of the above |

Reads from S/U mode are gated by `mcounteren` (and additionally
`scounteren` for U-mode when S-mode exists), bit index = CSR
address & 0x1F; a cleared bit raises an *illegal-instruction*
exception. M-mode reads are always allowed.

### Machine information (read-only)

| Address | Name | Value |
|---|---|---|
| 0xF11 | `mvendorid` | 0 (non-commercial implementation) |
| 0xF12 | `marchid` | 0 |
| 0xF13 | `mimpid` | 0 |
| 0xF14 | `mhartid` | 0 |
| 0xF15 | `mconfigptr` | 0 (no config data structure) |

### Machine trap setup

| Address | Name | Description |
|---|---|---|
| 0x300 | `mstatus` | Global machine status (field details below) |
| 0x301 | `misa` | ISA description; WARL — writes are ignored. Set by the CPU at `reset()` from the enabled extensions (MXL=1, I, plus A/C/F/M/S/U bits as configured) |
| 0x302 | `medeleg` | Exception delegation to S-mode. Exists when S or U mode is enabled. Bit 11 (ecall-from-M) is read-only zero: M-mode traps can never be delegated |
| 0x303 | `mideleg` | Interrupt delegation to S-mode. Same existence rule; only the six standard interrupt bits (SSIP/MSIP/STIP/MTIP/SEIP/MEIP) are writable |
| 0x304 | `mie` | Interrupt enables; writes masked to the six standard bits |
| 0x305 | `mtvec` | M-mode trap vector. BASE in bits [31:2]; MODE in [1:0]: 0=Direct, 1=Vectored — reserved modes (2, 3) are legalized to Direct (WARL) |
| 0x306 | `mcounteren` | Enables `cycle`/`time`/`instret` reads from S and U (U-mode reads additionally need the `scounteren` bit when S-mode exists; without S-mode `mcounteren` alone gates U). Reset 0 = blocked |
| 0x310 | `mstatush` | WARL all-zero (little-endian hart) |
| 0x30A | `menvcfg` | WARL all-zero (no environment-config features) |
| 0x31A | `menvcfgh` | WARL all-zero |

### Machine trap handling

| Address | Name | Description |
|---|---|---|
| 0x340 | `mscratch` | Scratch register for M-mode trap handlers |
| 0x341 | `mepc` | Exception PC. Writes are masked: `~1` with the C extension (IALIGN=16), `~3` without (WARL) |
| 0x342 | `mcause` | Trap cause: Interrupt bit [31] + exception code in [30:0] |
| 0x343 | `mtval` | Trap-specific value (precise; see below) |
| 0x344 | `mip` | Interrupt pending. MSIP is always software-writable; SSIP/STIP/SEIP are also software-writable when S-mode exists. The CPU's level-sensitive interrupt input lines drive the same bits via the `set()` backdoor (both options are spec-sanctioned). MTIP/MEIP are read-only (platform-driven) |

### Machine counters

| Address | Name | Description |
|---|---|---|
| 0xB00/0xB80 | `mcycle`/`mcycleh` | 64-bit cycle counter; incremented once per step, including trapped instructions and interrupt deliveries |
| 0xB02/0xB82 | `minstret`/`minstreth` | 64-bit retired-instruction counter; not incremented for trapped instructions or interrupt deliveries |

### Supervisor CSRs (exist when S-mode is enabled)

| Address | Name | Description |
|---|---|---|
| 0x100 | `sstatus` | Restricted view of `mstatus` (details below) |
| 0x104 | `sie` | `mie` restricted to the bits delegated in `mideleg` |
| 0x105 | `stvec` | S-mode trap vector; same WARL mode legalization as `mtvec` |
| 0x106 | `scounteren` | Enables counter reads from U-mode |
| 0x140 | `sscratch` | Scratch register for S-mode trap handlers |
| 0x141 | `sepc` | S-mode exception PC; same IALIGN write mask as `mepc` |
| 0x142 | `scause` | S-mode trap cause |
| 0x143 | `stval` | S-mode trap value |
| 0x144 | `sip` | `mip` restricted to the bits delegated in `mideleg`; writable subset further restricted to SSIP |
| 0x180 | `satp` | Address-translation register. MODE (bit 31): 0=Bare, 1=Sv32 — other values legalized to Bare (WARL). Values are **stored but unused**: the model has no MMU, all accesses are physical |

## `mstatus` field reference

| Bits | Field | Behavior |
|---|---|---|
| 1 | SIE | S-mode global interrupt enable (exists with S-mode) |
| 3 | MIE | M-mode global interrupt enable |
| 5 | SPIE | SIE saved on S-trap entry |
| 7 | MPIE | MIE saved on M-trap entry |
| 8 | SPP | Privilege before S-trap (0=U, 1=S) |
| 12:11 | MPP | Privilege before M-trap. WARL: encoding 2 legalizes to M; U/S legalize to M when the mode is not implemented |
| 14:13 | FS | **Hardwired to Dirty (11)** when F is enabled; `SD` mirrors it (a legal WARL choice — the model does not track FP context state) |
| 17 | MPRV | Modify privilege for loads/stores; exists with U-mode, cleared by MRET/SRET when the new mode is less privileged than M |
| 18 | SUM | Permit S-mode access to U-mode pages (exists with S and U) |
| 19 | MXR | Make executable readable (exists with U) |
| 20 | TVM | Trap S-mode `satp` accesses and `SFENCE.VMA` (exists with S) |
| 21 | TW | Timeout wait: trap WFI in S/U (writable when S and U are both enabled; WARL zero otherwise) |
| 22 | TSR | Trap SRET executed in S-mode (exists with S) |
| 31 | SD | Read-only summary: set because FS=Dirty with F |

Fields that require a mode/feature not configured are WARL zero and
ignore writes.

## `sstatus`, `sie`, `sip` subset rules

- `sstatus` reads expose SIE, SPIE, SPP, SUM, MXR plus the hardwired
  FS/SD; writes through `sstatus` affect only SIE, SPIE, SPP, SUM, MXR.
- `sie`/`sip` read and write `mie`/`mip` masked by `mideleg`: only
  delegated interrupt bits are visible/modifiable from S-mode.
  `sip` writes are additionally limited to the SSIP bit.

## What happens on a trap

### Synchronous exception entry

The CPU determines the target mode from `medeleg` (traps taken in
M-mode are never delegated; without S-mode everything goes to M), then:

| Action | M-mode target | S-mode target |
|---|---|---|
| Privilege | ← M | ← S |
| `xepc` | `mepc` ← PC of faulting instruction | `sepc` ← PC of faulting instruction |
| `xcause` | `mcause` ← cause (Interrupt=0) | `scause` ← cause |
| `xtval` | `mtval` ← precise trap value | `stval` ← same value |
| `xstatus` | MPIE ← MIE, MIE ← 0, MPP ← old privilege | SPIE ← SIE, SIE ← 0, SPP ← old privilege (U=0/S=1) |
| PC | ← `mtvec` BASE (even in vectored mode) | ← `stvec` BASE |

Execution **continues** in the handler (the model does not halt).
Trapped instructions do not retire: `minstret` is not incremented,
`mcycle` is. Taking any trap (exception or interrupt, to M or S) also
clears the LR/SC load reservation, as the A extension requires — an
SC in the handler after an LR in the trapped code fails.

**Precise `xtval` values:**

| Exception | `xtval` |
|---|---|
| Instruction address misaligned (jump/branch target) | Faulting target address |
| Instruction/load/store access fault, misaligned load/store | Faulting address |
| Breakpoint (EBREAK, C.EBREAK) | Address of the EBREAK instruction |
| Partial-instruction fetch fault (second parcel) | Address of the faulting parcel (`pc + 2`); `xepc` still points at the instruction start |
| Illegal instruction | The faulting instruction bits |
| ECALL | 0 (unspecified fields are written zero) |

### Interrupt entry

Interrupts are sampled before each instruction fetch. A pending,
enabled interrupt (`mip & mie`, gated by the global-enable rules: an
interrupt targeting mode X is enabled when the current privilege is
below X, or equal to X with `xIE` set; M-mode interrupts are never
taken from M-mode with MIE=0, and lower-privilege targets are skipped
while in M-mode) performs the same state update as an exception, with:

- `xcause` = Interrupt bit set | cause code (priority order
  **MEI > MSI > MTI > SEI > SSI > STI** within a target privilege;
  interrupts targeting M-mode take precedence over any interrupts
  targeting S-mode, regardless of cause priority);
- `xtval` = 0; `xepc` = PC of the *next* instruction;
- PC ← vectored dispatch: in vectored mode `BASE + 4 × cause`, in
  direct mode `BASE` (both `mtvec` and `stvec` support MODE=1);
- no instruction retires (`minstret` unchanged, `mcycle` incremented,
  `CPUExecResult::instr_size = 0`).

`mideleg` routes delegated interrupts to S-mode (they are then taken
only in S/U, subject to the SIE gating rules); traps taken in M-mode
are never delegated.

### MRET

Legal only in M-mode (otherwise *illegal-instruction*). It:

1. Sets privilege ← MPP;
2. MIE ← MPIE, MPIE ← 1;
3. MPP ← least privileged supported mode (U if U-mode exists, else M);
4. Clears MPRV when the new privilege is below M;
5. PC ← `mepc`.

### SRET

Illegal in U-mode, in S-mode when `mstatus.TSR=1`, and entirely when
S-mode is not implemented. It:

1. Sets privilege ← SPP ? S : U;
2. SIE ← SPIE, SPIE ← 1, SPP ← 0 (U);
3. Clears MPRV;
4. PC ← `sepc`.

### Reset

`CPU::reset()` (and `CSRFile::reset()`) restore: privilege = M;
`mstatus` = hardwired FS/SD plus MPP=M; all trap setup/handling CSRs,
delegation registers, counter enables and counters = 0; `mtvec` ←
`CPUConfig::mtvec_reset`; PC ← `CPUConfig::reset_vector`; `misa`
rebuilt from the enabled extensions. Extension enables take effect at
reset (they re-legalize `mstatus`, the `mepc` mask and `misa`).

## Deliberately not implemented

- **`sedeleg`/`sideleg`** — no U-mode trap delegation (N extension was
  removed from the ratified spec).
- **`pmpcfg*`/`pmpaddr*`** — no physical memory protection.
- **Hypervisor CSRs**, **debug CSRs**, **performance `mhpmevent*`/
  `mhpmcounter*`** — out of scope for this model.
- **`satp` translation effects** — stored, but there is no MMU.
