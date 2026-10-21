#!/usr/bin/env python3
"""The memory and timing budget, read out of the board's binary.

    make budget            (builds the board, runs it, writes docs/BUDGET.md)
    python3 tools/budget.py [--no-timing] [--output docs/BUDGET.md]

Four sections, each from a different source, none typed by hand:

  memory per component   the linker map (build-target/fsw.map): which object
                         file every byte of code and data came from
  the spacecraft object  the debug information (DWARF, read with gdb): how the
                         4 MiB static object divides between the applications
  stack                  the compiler's -fstack-usage files (one frame per
                         function), and the high-water mark the flight task's
                         stack actually reached in the timing run
  timing                 the scheduler's own per-task statistics, from the board
                         flying the nadir-pointing scenario in QEMU with
                         -icount: every instruction takes exactly 2^shift ns
                         of virtual time, so the numbers count instructions,
                         not a real Cortex-M7's cycles

Standard library only, like the rest of the Python here.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

BUILD = ROOT / "build-target"
ELF = BUILD / "fsw.elf"
MAP = BUILD / "fsw.map"

# The timing run.
ICOUNT_SHIFT = 2            # 4 ns per instruction: 250 million instructions a second
TIME_SCALE = 10.0
# The board stops itself after this many scheduler ticks and reports. The
# simulator flies until it does: about 1,600 s of the scenario, which covers
# detumble, the estimator converging, and ten minutes of fine pointing.
MAX_TICKS = 60000


# ---- memory per component ---------------------------------------------------------

def component_of(obj: str, sources: dict[str, str]) -> str:
    """Name the component an input file in the map belongs to."""
    m = re.search(r"lib([\w+]+)\.a\(([^)]+)\)", obj)
    if m:
        lib, member = m.group(1), m.group(2)
        if lib in ("fsw_core", "fsw_apps"):
            base = member.removesuffix(".obj").removesuffix(".o")
            return sources.get(base, f"fsw/{base}")
        return {
            "fsw_platform": "fsw/platform/freertos",
            "freertos": "FreeRTOS kernel",
            "c_nano": "C library (newlib-nano)",
            "g_nano": "C library (newlib-nano)",
            "m": "maths library (libm)",
            "gcc": "compiler runtime (libgcc)",
            "stdc++_nano": "C++ runtime (libstdc++)",
            "supc++_nano": "C++ runtime (libstdc++)",
        }.get(lib, f"lib{lib}")
    if "fsw.elf.dir" in obj:
        return "targets/mps2-an500"
    return "other"


def source_dirs() -> dict[str, str]:
    """Object basename -> its directory, from the shared source list."""
    text = (ROOT / "cmake" / "fsw_sources.cmake").read_text()
    out = {}
    for path in re.findall(r"(fsw/\S+\.cpp)", text):
        p = pathlib.PurePosixPath(path)
        out[p.name] = str(p.parent) if p.parent.name != "fsw" else str(p.with_suffix(""))
    return out


def parse_map(path: pathlib.Path) -> list[tuple[str, str, str, int]]:
    """(output section, input section, input file, bytes) for every input
    section placed."""
    lines = path.read_text().splitlines()
    start = next(i for i, l in enumerate(lines) if l.startswith("Linker script and memory map"))
    out: list[tuple[str, str, int]] = []
    current = ""
    pending: str | None = None
    entry = re.compile(r"^\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(\S.*)$")
    for line in lines[start:]:
        head = re.match(r"^(\.\w[\w.]*)\s", line + " ")
        if head and not line.startswith(" "):
            current = head.group(1)
            continue
        if pending is not None:
            m = entry.match(line)
            if m:
                out.append((current, pending, m.group(3), int(m.group(2), 16)))
            pending = None
            continue
        m = re.match(r"^ (\.\S+|COMMON)\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(\S.*)$", line)
        if m:
            out.append((current, m.group(1), m.group(4), int(m.group(3), 16)))
        elif re.match(r"^ (\.\S+|COMMON)$", line):
            pending = line.strip()
    return out


def section_sizes() -> dict[str, int]:
    out = subprocess.run(["arm-none-eabi-size", "-A", str(ELF)],
                         capture_output=True, text=True, check=True).stdout
    sizes = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1].isdigit():
            sizes[parts[0]] = int(parts[1])
    return sizes


def memory_table() -> tuple[list[str], dict[str, int]]:
    sources = source_dirs()
    per: dict[str, dict[str, int]] = {}
    counted = {"code": 0, "data": 0, "bss": 0}
    for section, insec, obj, size in parse_map(MAP):
        kind = {".text": "code", ".ARM.exidx": "code", ".data": "data", ".bss": "bss"}.get(section)
        # String literals are merged by the linker across every object, so
        # the map's per-object sizes for them are before merging; they are
        # accounted for as one row below.
        if kind is None or size == 0 or ".str1." in insec:
            continue
        comp = component_of(obj, sources)
        per.setdefault(comp, {"code": 0, "data": 0, "bss": 0})[kind] += size
        counted[kind] += size
    sizes = section_sizes()
    totals = {"code": sizes[".text"] + sizes.get(".ARM.exidx", 0), "data": sizes[".data"],
              "bss": sizes[".bss"]}
    rows = ["| Component | Code + constants | Initialised data | Zeroed data |",
            "|---|---:|---:|---:|"]
    for comp, s in sorted(per.items(), key=lambda kv: -(kv[1]["code"] + kv[1]["data"])):
        rows.append(f"| `{comp}` | {s['code']:,} | {s['data']:,} | {s['bss']:,} |")
    rest = {k: totals[k] - counted[k] for k in totals}
    rows.append(f"| string literals, merged across components; alignment | {rest['code']:,} | "
                f"{rest['data']:,} | {rest['bss']:,} |")
    rows.append(f"| **total** | **{totals['code']:,}** | **{totals['data']:,}** | **{totals['bss']:,}** |")
    return rows, totals


def largest_symbols(n: int = 12) -> list[str]:
    out = subprocess.run(["arm-none-eabi-nm", "-C", "--size-sort", "-S", "-r", str(ELF)],
                         capture_output=True, text=True, check=True).stdout
    code, data = [], []
    for line in out.splitlines():
        parts = line.split(maxsplit=3)
        if len(parts) < 4:
            continue
        size, kind, name = int(parts[1], 16), parts[2], parts[3]
        (code if kind in "tTwW" else data).append((size, name))
    rows = ["| Bytes | Code | | Bytes | Data |", "|---:|---|---|---:|---|"]
    for i in range(n):
        c = code[i] if i < len(code) else (0, "")
        d = data[i] if i < len(data) else (0, "")
        rows.append(f"| {c[0]:,} | `{shorten(c[1])}` | | {d[0]:,} | `{shorten(d[1])}` |")
    return rows


def shorten(name: str) -> str:
    name = name.replace("(anonymous namespace)::", "").replace("{anonymous}::", "")
    name = re.sub(r"\([^()]*\)( const)?$", "()", name)    # drop the parameter list
    return name.replace("fsw::", "")


def spacecraft_layout() -> list[str]:
    """How fsw::Spacecraft divides up, from the debug information."""
    def members(type_name: str) -> list[tuple[int, str]]:
        try:
            out = subprocess.run(["gdb", "-batch", "-nx", "-ex", "set max-value-size unlimited",
                                  "-ex", f"ptype/o '{type_name}'", str(ELF)],
                                 capture_output=True, text=True).stdout
        except FileNotFoundError:
            return []
        rows = []
        for line in out.splitlines():
            # Members at the first level of nesting only.
            m = re.match(r"^/\*\s+\d+(?::\s*\d+)?\s+\|\s+(\d+) \*/    (?! )(.*?)(?: \{|;)$", line)
            if m:
                decl = m.group(2)
                rows.append((int(m.group(1)), decl))
        return rows
    sc = members("fsw::Spacecraft")
    if not sc:
        return ["(gdb was not available to read the debug information)"]
    total = sum(s for s, _ in sc)
    rows = ["| Bytes | Member of `fsw::Spacecraft` |", "|---:|---|"]
    for size, decl in sorted(sc, reverse=True):
        if size >= 64:
            rows.append(f"| {size:,} | `{decl.removeprefix('class ').removeprefix('struct ')}` |")
    rows.append(f"| **{total:,}** | **sum of the members** (rows under 64 bytes not shown) |")
    ttc = [(s, d) for s, d in members("fsw::ttc::TtcApp") if s >= 1024]
    if ttc:
        rows += ["", "| Bytes | Inside `ttc::TtcApp` (1 KiB and over) |", "|---:|---|"]
        rows += [f"| {s:,} | `{d.removeprefix('class ').removeprefix('struct ')}` |"
                 for s, d in sorted(ttc, reverse=True)]
    return rows


# ---- stack ---------------------------------------------------------------------------

def stack_frames(n: int = 10) -> list[str]:
    frames = []
    for su in BUILD.rglob("*.su"):
        for line in su.read_text().splitlines():
            try:
                where, size, kind = line.rsplit("\t", 2)
            except ValueError:
                continue
            func = where.split(":", 3)[-1]
            frames.append((int(size), kind, shorten(func)))
    frames.sort(reverse=True)
    rows = ["| Bytes | Function | |", "|---:|---|---|"]
    rows += [f"| {s:,} | `{f}` | {k} |" for s, k, f in frames[:n]]
    return rows


# ---- timing ----------------------------------------------------------------------------

def timing_run() -> dict:
    """Fly the pointing scenario on the board under -icount and collect its report."""
    from sim.scenarios.nadir_pointing import SCENARIO
    from sim.sil import board
    from sim.sil.bridge import Bridge
    from sim.sil.harness import free_port
    from sim.sil.simulation import Simulation

    ttc, simp = free_port(), free_port()
    cmd = board.command(ttc, simp, ["--time-scale", str(TIME_SCALE), "--max-ticks", str(MAX_TICKS),
                                    "--watchdog-ms", "0"], icount=ICOUNT_SHIFT)
    t0 = time.time()
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        bridge = Bridge(simp)
        bridge.connect()
        sim = Simulation(SCENARIO, bridge)
        pointing_at = None
        try:
            while time.time() - t0 < 1800:
                sim.step()
                if pointing_at is None and sim.flags & 2:
                    pointing_at = sim.t
        except (ConnectionError, OSError, RuntimeError):
            pass                     # the board has stopped at --max-ticks
        bridge.close()
        console, _ = proc.communicate(timeout=120)
    finally:
        if proc.poll() is None:
            proc.kill()
    report = {"wall_s": time.time() - t0, "sim_s": sim.t, "pointing_at": pointing_at,
              "tasks": [], "console": console}
    for line in console.splitlines():
        m = re.match(r"^\s+(\w+)\s+(\d+)\s+(\d+)\s+(\d+)$", line)
        if m:
            report["tasks"].append((m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4))))
        m = re.match(r"^stopped after (\d+) ticks, (\d+) overruns", line)
        if m:
            report["ticks"], report["overruns"] = int(m.group(1)), int(m.group(2))
        m = re.match(r"^stack\s+:\s+(\d+) bytes never used", line)
        if m:
            report["stack_unused"] = int(m.group(1))
    return report


def timing_table(r: dict) -> list[str]:
    ns_per = 2 ** ICOUNT_SHIFT
    rows = ["| Task | Rate | Runs | Worst case (µs) | Mean (µs) | Worst case, instructions |",
            "|---|---|---:|---:|---:|---:|"]
    rates = {"ttc_rx": "50 Hz", "ttc_tm": "10 Hz", "ttc_dl": "50 Hz", "io": "50 Hz", "modes": "10 Hz"}
    worst_tick = 0.0
    for name, runs, max_us, mean_us in r["tasks"]:
        # The scheduler measures on the spacecraft's clock, which runs
        # TIME_SCALE times faster than the board's.
        w, m = max_us / TIME_SCALE, mean_us / TIME_SCALE
        worst_tick += w
        rows.append(f"| `{name}` | {rates.get(name, '?')} | {runs:,} | {w:,.0f} | {m:,.1f} | "
                    f"{w * 1000 / ns_per:,.0f} |")
    rows.append(f"| **all five in one tick** | | | **{worst_tick:,.0f}** | | of a 20,000 µs tick: "
                f"**{100 * worst_tick / 20000:.0f} %** |")
    return rows


# ---- the report -------------------------------------------------------------------------

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--output", default=str(ROOT / "docs" / "BUDGET.md"))
    ap.add_argument("--no-timing", action="store_true", help="skip the QEMU run")
    args = ap.parse_args()
    if not ELF.exists():
        print("build-target/fsw.elf not found -- run `make target` first")
        return 2

    mem_rows, totals = memory_table()
    out = [
        "# HYPERSAT budget: memory and timing on the board",
        "",
        "> Generated by `tools/budget.py` from `build-target/fsw.elf` (`make budget`).",
        "> Do not edit by hand: every number here comes out of the binary or a run of it.",
        "",
        "The board is an Arm MPS2 AN500 (Cortex-M7, double-precision FPU) under FreeRTOS,",
        "as QEMU emulates it. See [Lesson 11](../learn/11-portability/) for how the",
        "flight software got here, and [ARCHITECTURE.md](ARCHITECTURE.md) for why it is",
        "built so that this report can exist at all: no heap, every object static, so",
        "the linker knows the whole memory footprint before the program ever runs.",
        "",
        "## Memory",
        "",
        f"Code and constants: **{totals['code']:,} bytes** (of 4 MiB SSRAM1, standing in for flash).",
        f"Initialised data: **{totals['data']:,} bytes**. Zeroed data: **{totals['bss']:,} bytes**",
        "(in the 16 MiB PSRAM). The board's \"non-volatile\" storage is a further 8 KiB of",
        "RAM that survives a reset, outside all of these.",
        "",
        "### Per component",
        "",
        "From the linker map. Zeroed data is counted where the object is *defined*: the",
        "whole spacecraft is one static object in the board's `main.cpp`, so it all lands",
        "under `targets/mps2-an500`. The next table divides it up.",
        "",
        *mem_rows,
        "",
        "### Inside the spacecraft object",
        "",
        "From the debug information. The packet store, the TT&C application's mass memory,",
        "is nearly all of it, by design: sized for about three hours of the default",
        "housekeeping, two orbits out of contact with margin (`ttc_app.hpp`).",
        "",
        *spacecraft_layout(),
        "",
        "### Largest symbols",
        "",
        *largest_symbols(),
        "",
    ]

    timing = None if args.no_timing else timing_run()
    out += [
        "## Stack",
        "",
        "The flight task has a 32 KiB stack (`kFlightStackWords` in",
        "`targets/mps2-an500/main.cpp`); interrupts use the separate 8 KiB main stack.",
        "",
    ]
    if timing and "stack_unused" in timing:
        used = 32768 - timing["stack_unused"]
        out += [f"**Measured: {used:,} bytes** of the flight task's 32 KiB were ever used during the",
                "timing run below (FreeRTOS fills a task's stack with a known pattern and finds the",
                f"deepest byte overwritten): {100 * used / 32768:.0f} %.", ""]
    out += [
        "The largest single frames, from `-fstack-usage`. Static means the frame size is",
        "fixed at compile time; all of them are, because nothing allocates on the stack",
        "dynamically. The deepest *path* is not computed from these: the scheduler's tasks,",
        "the bus's subscribers and the ports are all called through pointers, which a",
        "static call graph cannot follow. The measurement above is the evidence instead.",
        "",
        *stack_frames(),
        "",
    ]

    out += ["## Timing", ""]
    if timing is None:
        out += ["(not measured in this run: `--no-timing`)", ""]
    else:
        out += [
            f"The board flew the first {timing['sim_s']:,.0f} s of the nadir-pointing scenario in QEMU",
            "(" + (f"detumble, the estimator converging, and fine pointing from t = {timing['pointing_at']:,.0f} s"
                   if timing["pointing_at"] else "pointing never engaged in this run") + ") with",
            f"`-icount shift={ICOUNT_SHIFT}`: every instruction advances the board's clock by exactly",
            f"{2 ** ICOUNT_SHIFT} ns, as if a processor retired {1000 // 2 ** ICOUNT_SHIFT} million instructions a second, one",
            "per cycle. So these are **instruction counts expressed as time, not",
            "measurements of a real Cortex-M7**: QEMU models no caches and no pipeline, and",
            "counts a double-precision divide, many cycles on real hardware, as one",
            "instruction like any other. Interrupts that arrive while a task runs are",
            "counted in that task's time.",
            "They are good for comparing tasks, and for the order of magnitude; they are not",
            "a substitute for a logic analyser on a real board.",
            "",
            *timing_table(timing),
            "",
            f"{timing['ticks']:,} ticks, {timing['overruns']} overrun{'' if timing['overruns'] == 1 else 's'}."
            " The worst cases did not",
            "necessarily happen in the same tick, so their sum is a pessimistic bound on one",
            "tick's work. The scheduler measured on the spacecraft's clock, which ran",
            f"{TIME_SCALE:g} times faster than the board's; the table divides that back out.",
            "",
            f"The run took {timing['wall_s']:.0f} s of wall-clock time. QEMU's instruction count is",
            "deterministic; when each sensor frame arrives from the simulator is not, so a",
            "second run gives slightly different numbers.",
            "",
        ]
    pathlib.Path(args.output).write_text("\n".join(out))
    print(f"wrote {args.output}")
    if timing:
        for row in timing_table(timing):
            print("  " + row)
    return 0


if __name__ == "__main__":
    sys.exit(main())
