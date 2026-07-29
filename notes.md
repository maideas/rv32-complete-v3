# Notes: RISC-V Hardware Implementation Q&A

## Q: which RISC-V extensions are the most important regarding performance for a hardware implementation, that has separate instruction and data busses without caches, 32 bit data width, and some instruction memory wait states ?

For that kind of system (Harvard-style buses, no caches, 32-bit data bus, wait-stated instruction memory), the ranking is pretty clear:

## 1. C (compressed instructions) — by far the biggest win
This is the extension that directly attacks your bottleneck:
- ~25–30% smaller code → fewer instruction fetches → fewer wait states burned.
- With no I-cache, every fetch goes to memory, so fetch bandwidth is often *the* performance limiter. RVC effectively doubles it for most of the hot path.
- Your model already handles the subtlety here: fetches can be 16-bit, so the hardware should fetch 32 bits at a time and buffer half-words across (possibly unaligned) fetch boundaries — a "fetch buffer" pairs perfectly with RVC.
- Caveat for hardware: 32-bit instructions crossing a 32-bit word boundary cause an extra fetch; good compilers align branch targets to mitigate this.

## 2. M (multiply/divide)
- Without it, multiplication is a ~20–40 instruction software loop; with wait-stated instruction memory that's extra painful because you also pay fetch cost for the loop.
- A hardware multiplier (even multi-cycle iterative) is cheap in area and a large win. Division can be a slow multi-cycle unit — that's fine, it still beats emulation.
- Note: M instructions don't access memory, so they don't touch your data bus; they're pure pipeline additions.

## 3. Zba / Zbb / Zbs (bit manipulation) — modest but cheap
- Address calculation (`sh1add`..`sh3add`), min/max, clz, rotations map to 1 cycle vs. 2–4 instructions each. Fewer instructions executed = fewer fetches on your wait-stated bus.
- Low implementation cost (mostly ALU shuffling), so good ROI.

## 4. F (single-precision float) — only if your workload needs it
- Software float emulation is brutal (100+ cycles per op, plus instruction fetch traffic). If you do any meaningful FP math, a hardware FPU pays for itself; if not, skip it entirely — it's the most expensive extension in area.
- 32-bit FP loads/stores fit your 32-bit data bus naturally, no misalignment issues.

## Not important for performance here
- **A (atomics):** only matters for multicore/OS sync; on a single-hart MCU-style core it costs bus complexity for zero speed.
- **Zicsr/Zifencei:** required for practical software, but not a performance feature.
- **Misaligned access in hardware:** not an extension, but important — with a 32-bit data bus, handle misaligned loads/stores in hardware (two bus cycles). RVC makes misaligned *instruction* words common anyway.

## Things that matter more than any extension
Given your memory system, microarchitecture beats ISA:
1. **Instruction fetch buffering** (fetch 32b, consume 16/32b) + prefetch during stalls.
2. **Branch target buffer / simple static prediction** — every taken branch refills through wait states.
3. **Single-cycle loads/stores when data memory has no wait state** — keep data bus wait-state-free if at all possible, since ~30% of instructions touch it.
4. **Zero-wait-state "tightly coupled" instruction memory** for hot loops, even a small one, acts like a poor man's cache.

**Bottom line:** implement **C + M** first, add **Zba/Zbb** if you have spare ALU area, and **F** only for FP-heavy workloads — but invest most of your effort in the fetch buffer and branch handling, since instruction-memory wait states are your real enemy.

## Q: does it make sense to implement parallel to the usual execution path a second one, that can execute a compressed instruction in the same cycle ?

Yes, this is actually a well-known and sensible technique — limited dual-issue using compressed instructions — but the value depends heavily on how you scope it. Let me break it down.

## Why it's attractive in your system

Your bottleneck is instruction fetch through wait-stated memory. RVC already halves fetch traffic; a second narrow issue slot for compressed instructions additionally lets you:

- **Execute 2 instructions while only fetching 32 bits once** — in the best case two 16-bit instructions ride on a single fetch and complete in one cycle, so instruction memory wait states are amortized over twice the work.
- Compressed instructions are deliberately the *most frequent* operations (add, mv, addi, lw/sw with small offsets, branches), so a cheap second path covers a large fraction of dynamic instructions (~50–60% of RVC code by instruction count).

## The hard part: what the second pipe must handle

Even "simple" compressed instructions need:

1. **Dependency checking in the same cycle** — the pair must be checked for RAW/WAW hazards (e.g. `c.addi a0,1; c.add a0,a1` cannot dual-issue). This comparator logic grows fast and lands on the critical path.
2. **Register file ports** — two reads + one write is standard; dual-issue wants up to 4 reads + 2 writes. Extra ports cost real area/timing in an MCU-class core. (Trick: a "write port sharing" scheme where the second slot only issues when it needs ≤1 write, which is always true — but reads are the problem.)
3. **What the second pipe may execute** — the sane options:
   - **ALU-only second slot** (c.add, c.mv, c.addi, c.li, c.and, etc.): cheap, no memory port conflicts. This is the sweet spot.
   - Allowing loads/stores on both slots: requires two data-bus transactions or arbitration — usually not worth it; restrict memory ops to pipe 0.
   - Branches on the second slot complicate fetch redirect; commonly pipe 0 only.

## Alternatives that give most of the win for less complexity

- **Macro-op fusion in decode**: fuse adjacent compressed pairs into one internal operation, e.g.
  - `c.li rd,imm` + `c.add rd,rs` → one add-immediate
  - `c.addi16sp` + `c.swsp` sequences in prologues
  - compare+branch fusion (a classic, works with non-compressed too)
  
  Fusion needs *no* extra register ports — decode just emits one richer micro-op. Much cheaper than true dual-issue and captures a big chunk of the benefit.
- **Just do fetch buffering well**: a 32-bit fetch buffer that can deliver two 16-bit instructions per fetch, combined with single-cycle execute and good branch handling, often makes IPC ≈ 1 achievable; dual-issue pushes toward 1.2–1.4 IPC at best for RVC-heavy code, but with diminishing returns if fetch wait states still dominate.

## Realistic assessment

| Approach | IPC gain | Complexity | Timing risk |
|---|---|---|---|
| Fetch buffer (32b in, 16/32b out) | large (vs. naive) | low | low |
| Macro-op fusion (2–3 patterns) | +5–15% | low–medium | low |
| Dual-issue, 2nd slot ALU-only | +15–30% | medium–high | hazard logic on critical path |
| Full dual-issue incl. memory ops | +35%+ | high | high |

**My recommendation:** for a cache-less 32-bit embedded core, do the fetch buffer first — it removes most of your wait-state pain. Then add *decode-time fusion* of a handful of compressed instruction pairs (prologue/epilogue idioms, li+op, compare+branch). Only go for a true parallel second execution path if you're already timing-closure comfortable and profiling shows ALU chains (not fetch) as the bottleneck — otherwise you're adding superscalar hazard logic to fix a memory problem.

Also note: cores like the Rocket-class or higher-end embedded cores (e.g., some Andes/SiFive parts) do implement exactly this "narrow dual-issue with RVC" idea — so it's proven, but they do it in deeper pipelines where the hazard checks hide behind existing pipe stages. In a short 2–3 stage pipeline, the same-cycle dependency check can directly limit your clock frequency, which may cost you more than the IPC gain.

## Q: cool. exactly, what I had in mind

Glad it lines up! If you want to evaluate the idea before committing to RTL, your reference model here is a good starting point — you could:

1. **Instrument the model**: count dual-issue-eligible compressed pairs (both 16-bit, second is ALU-only, no RAW/WAW hazard) in the execute loop. That gives you a realistic upper-bound IPC estimate from actual code traces, basically for free.
2. **Add a cycle counter**: model fetch wait states + the dual-issue slot, then compare "fetch-buffer only" vs. "fetch-buffer + second slot" cycle counts on representative firmware. Tells you whether the hazard logic is worth the timing risk in *your* pipeline.

Happy to help add either of those to `riscv_cpu.hpp` / the test suite if you want numbers. Good luck with the hardware design! 🚀

## Q: when implementing such parallel execution, does it make sense to switch to fully serial execution in debug mode ?

Yes — and in practice this question mostly answers itself, because real debug mode on RISC-V doesn't "run" normally anyway:

## How it works with the RISC-V Debug Spec

- **Halt mode**: when the debugger takes control (ebreak, haltreq, trigger match), the hart enters debug mode, stops executing program code, and the debugger pokes at state via abstract commands or a program buffer. Nothing is free-running, so dual-issue never comes into play.
- **Single-step**: the `step` bit in `dcsr` executes *exactly one instruction* and re-enters debug mode. That's inherently serial — the hardware retires one instruction, updates all architected state, then halts. Dual-issue is bypassed by construction here.
- **Program buffer execution**: debugger-supplied snippets executed one at a time (or small loops), again serially with full state visibility.

So your pipeline essentially needs a mux: **normal run mode** (dual-issue, fused ops, whatever) vs. **debug mode** (one instruction, fully retired, all side effects visible before the next). That's standard and low-cost.

## Why serial-in-debug is the right call

1. **Deterministic, transparent state** — the debugger must see exact per-instruction PC, register, and memory state. Dual-issue retiring two instructions per cycle makes "what happened" ambiguous (e.g. a trigger on the second instruction of a pair).
2. **Triggers/breakpoints stay simple** — matching on an instruction that might issue in slot 1 of a pair, potentially *after* its dependency was resolved speculatively, is a nightmare. Serial execution means triggers fire exactly where expected.
3. **Simpler verification** — debug logic is notoriously under-tested; keeping it on a simple serial path reduces bug surface.

## One caution: don't create a third behavioral mode

Avoid a "run serially but free-running" debug option (some cores offer this for "real-time debugging"). It creates Heisenbugs: code that works serially but breaks dual-issue (or vice versa) — e.g., timing-sensitive driver loops, or hazard bugs in *your own* bypass logic that only manifest in the parallel path. If someone needs to observe a running system, prefer:

- hardware triggers that halt precisely,
- a trace port (instruction trace) rather than slowing execution.

## Minor details to keep consistent

- **Performance counters** (`mcycle`, `minstret`): document that `minstret` counts instructions (so dual-issue retires 2/cycle). Debuggers and profilers rely on this being sane.
- **Step granularity**: if you implement macro-op *fusion*, the debug spec requires stepping at *architectural instruction* granularity — so fused pairs must either be split in debug mode or reported as two retirements. This is the strongest argument for keeping debug strictly serial and unfused.

**Bottom line:** yes — debug halt/step should execute strictly one architectural instruction at a time, unfused and single-issue. It's simpler, spec-aligned, and the performance of debug-mode execution is irrelevant anyway.
