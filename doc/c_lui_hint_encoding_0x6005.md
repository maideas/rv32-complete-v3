# RV32C decode of 0x6005: C.LUI HINT

This session decoded the 16-bit opcode 0x6005 against the RV32 reference model, identifying it as c.lui x0, 1. After a reported mismatch with another reference model, the decode was double-checked by hand and by compiling a small test against the project's rv32c decoder. Conclusion: per the ratified RVC spec, C.LUI with rd = x0 and nzimm != 0 is a valid HINT encoding that executes as a NOP, whereas models treating it as an illegal instruction follow an outdated draft or the prose-only description.

## User

> which RV32 instruction is encoded by the opcode value 0x6005

## Assistant

Decoding `0x6005` = `0110 0000 0000 0101` (a 16-bit compressed instruction, since bits [1:0] = `01` → quadrant C1):

- **funct3** (bits [15:13]) = `011` → C.ADDI16SP / C.LUI group
- **rd** (bits [11:7]) = `00000` = x0 (not x2, so it's the C.LUI branch)
- **nzimm** = bit[12]=0, bits[6:2]=`00001` → nzimm6 = 1 (non-zero, so not the reserved encoding)

So `0x6005` is **`c.lui x0, 1`** — a valid **HINT encoding** per the RISC-V spec (C.LUI with rd = x0, nzimm ≠ 0). In this model's decoder (`riscv_rv32c.hpp`, funct3 case `0b011`) it decodes as C_LUI and executes as a NOP, since the write to x0 is discarded. This is also one of the HINT encodings the illegal-opcode generator explicitly never emits.

## User

> can you please double check, because there is a mismatch with another reference model

## Assistant

Let me verify by actually running the model's decoder on that value, and re-check the bit math carefully.

## Assistant

I re-verified both by hand and by running this project's actual decoder on the value:

```
$ ./build/check6005
type_illegal=0 mnemonic=c.lui zero, 1 rd=0 imm=0x00001000
```

Bit-level check of `0x6005` = `011 0 00000 00001 01`:

| Field | Bits | Value | Meaning |
|---|---|---|---|
| op [1:0] | `01` | quadrant C1 | |
| funct3 [15:13] | `011` | C.LUI / C.ADDI16SP | |
| rd [11:7] | `00000` | x0 | not x2 → C.LUI branch |
| nzimm[17], nzimm[16:12] | bit 12 = 0, bits [6:2] = `00001` | nzimm6 = 1 ≠ 0 | not the reserved nzimm=0 encoding |

So the decode is confirmed: **`c.lui x0, 1`** (imm = 0x1000).

About the mismatch — this is a well-known divergence point between models, and the ratified spec is on this model's side:

- **Ratified RVC spec (unprivileged ISA V20191213, Table 16.3 "RVC HINT instructions", page 111):** C.LUI with **rd = x0, nzimm ≠ 0** is explicitly a **HINT** — a *valid* encoding that executes as a NOP. That's exactly what this model does (decodes as C_LUI; the x0 write is discarded). This is why `riscv_illegal_opgen.hpp` deliberately excludes it from illegal stimulus.
- **Older drafts / some reference models:** early RVC drafts stated "C.LUI is only valid when rd ≠ {x0, x2}", and some models implemented that literally, raising an **illegal-instruction exception** for rd = x0. Also note the instruction *description text* in the current spec still says "valid only when rd ≠ {x0, x2}", but the normative HINT table carves out rd = x0 as a HINT — models that only read the prose get this wrong.

So if your other reference model flags `0x6005` as illegal, that model is following the outdated/prose-only interpretation. Per the ratified spec (and matching Spike's behavior), `0x6005` should decode and execute as a NOP HINT, not trap. Note the reserved cases are different: C.LUI with **nzimm = 0** (any rd ≠ x2) is reserved, and C.ADDI16SP (rd = x2) with nzimm = 0 is reserved — this model traps on those, correctly.
