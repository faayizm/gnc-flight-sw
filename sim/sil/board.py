"""The board, in QEMU: how to start build-target/fsw.elf so that it looks to
the ground and the simulator exactly like build/fsw.

The board's three UARTs become TCP servers on the host -- console, TT&C,
simulator -- and its options arrive by semihosting, in the laptop's syntax.
Options that only mean something on the laptop (the NVM file, the ports,
which here belong to QEMU) are translated or dropped.

    python3 -m sim.sil.board --ttc-port 50001 --sim-port 50000 --time-scale 10
    make target-run QEMU_ARGS="--time-scale 10"
"""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
ELF = ROOT / "build-target" / "fsw.elf"

# Options the board's main understands, with whether they take a value.
BOARD_OPTIONS = {"--time-scale": True, "--watchdog-ms": True, "--max-ticks": True, "--verbose": False}


def command(ttc_port: int, sim_port: int, fsw_args: list[str], icount: int | None = None) -> list[str]:
    """The QEMU command line for the board with these flight-software options."""
    args: list[str] = []
    i = 0
    while i < len(fsw_args):
        a = fsw_args[i]
        takes_value = BOARD_OPTIONS.get(a)
        if takes_value is None:                     # --nvm, --ttc-port...: laptop only
            i += 2 if a.startswith("--") and i + 1 < len(fsw_args) and not fsw_args[i + 1].startswith("--") else 1
            continue
        args.append(a)
        if takes_value:
            args.append(fsw_args[i + 1])
            i += 1
        i += 1
    semi = "enable=on,target=native,arg=fsw" + "".join(f",arg={a}" for a in args)
    cmd = ["qemu-system-arm", "-M", "mps2-an500", "-display", "none", "-monitor", "none",
           "-kernel", str(ELF), "-semihosting-config", semi,
           "-serial", "stdio",
           "-serial", f"tcp:127.0.0.1:{ttc_port},server=on,wait=off,nodelay=on",
           "-serial", f"tcp:127.0.0.1:{sim_port},server=on,wait=off,nodelay=on"]
    if icount is not None:
        # Deterministic: every instruction advances virtual time by 2^icount ns.
        cmd += ["-icount", f"shift={icount},align=off,sleep=on"]
    return cmd


def main() -> int:
    ap = argparse.ArgumentParser(description="Run the HYPERSAT board in QEMU")
    ap.add_argument("--ttc-port", type=int, default=50001)
    ap.add_argument("--sim-port", type=int, default=50000)
    ap.add_argument("--icount", type=int, help="run with -icount shift=N (deterministic timing)")
    ns, rest = ap.parse_known_args()
    if not ELF.exists():
        print(f"{ELF.relative_to(ROOT)} not found -- run `make target` first")
        return 2
    cmd = command(ns.ttc_port, ns.sim_port, rest, ns.icount)
    print(f"QEMU mps2-an500: TT&C on 127.0.0.1:{ns.ttc_port}, simulator on 127.0.0.1:{ns.sim_port}, "
          "console below. Ctrl-C to stop.", flush=True)
    try:
        return subprocess.call(cmd)
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    sys.exit(main())
