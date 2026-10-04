#!/usr/bin/env python3
"""
The lessons' promises, checked against the real spacecraft.

Every lesson says "run this, and you will see that". This script takes those
commands and code snippets *out of the lesson files themselves* -- not copies
of them -- runs them against a live flight binary on port 50001, the port the
lessons use, and checks that what the lesson says you will see is what you
actually see.

It exists because the lessons once went stale for three phases without anyone
noticing: a tool printed garbage, outputs changed shape, "not built yet" notes
described things that had been built. Prose cannot be type-checked, but a
command and its expected output can.

Run:  make check-lessons      (needs `make build`)
"""

from __future__ import annotations

import os
import pathlib
import re
import socket
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
LEARN = ROOT / "learn"
FSW = ROOT / "build" / "fsw"

fails = 0


def check(ok: bool, what: str, detail: str = "") -> None:
    global fails
    print(f"  {'.' if ok else 'x'}  {what}")
    if not ok:
        fails += 1
        if detail:
            print("     --- output ---\n" + "\n".join("     " + l for l in detail.splitlines()[-25:]))


def block(lesson: str, lang: str, containing: str) -> str:
    """The first ```lang code block in the lesson that contains `containing`."""
    text = (LEARN / lesson / "README.md").read_text()
    for m in re.finditer(rf"```{lang}\n(.*?)```", text, re.S):
        if containing in m.group(1):
            return m.group(1)
    raise KeyError(f"{lesson}: no ```{lang} block containing {containing!r}")


def shown(lesson: str, line: str) -> bool:
    """Does the lesson itself show this line of output? (Keeps the expected
    strings below honest: they must appear in the lesson too.)"""
    return line in (LEARN / lesson / "README.md").read_text()


def run_bash(script: str, timeout: float = 30.0) -> str:
    r = subprocess.run(["bash", "-c", script], cwd=ROOT, capture_output=True, text=True, timeout=timeout)
    return r.stdout + r.stderr


def run_python_in_gnd(code: str, timeout: float = 60.0) -> str:
    with tempfile.NamedTemporaryFile("w", suffix=".py", dir=ROOT / "gnd", delete=False) as f:
        f.write(code)
        path = f.name
    try:
        r = subprocess.run([sys.executable, path], cwd=ROOT / "gnd", capture_output=True, text=True,
                           timeout=timeout)
        return r.stdout + r.stderr
    finally:
        os.unlink(path)


def expect(lesson: str, what: str, output: str, *lines: str) -> None:
    """Each expected line must be in the output AND shown in the lesson."""
    missing = [l for l in lines if l not in output]
    unshown = [l for l in lines if not shown(lesson, l)]
    check(not missing and not unshown, f"{lesson}: {what}",
          output + (f"\n(missing: {missing})" if missing else "") +
          (f"\n(expected text not in the lesson: {unshown})" if unshown else ""))


def port_free(port: int) -> bool:
    with socket.socket() as s:
        return s.connect_ex(("127.0.0.1", port)) != 0


def watchdog_exercise() -> None:
    """Lesson 18's exercise, as a student does it: crash the computer with
    the lesson's own command, then boot it again and read the banner."""
    nvm = pathlib.Path(tempfile.gettempdir()) / f"lessons_wdt_{os.getpid()}.bin"
    args = [str(FSW), "--ttc-port", "50001", "--sim-port", "0", "--nvm", str(nvm)]
    try:
        proc = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(0.5)
        run_bash(block("18-when-things-break", "bash", "send TEST_WATCHDOG"))
        try:
            code = proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            proc.kill()
            code = None
        check(code == 86, "18-when-things-break: TEST_WATCHDOG gets the computer reset (status 86)",
              f"exit status {code}")
        again = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        time.sleep(1.0)
        again.terminate()
        out = again.communicate(timeout=5)[0]
        expect("18-when-things-break", "the next boot knows it was the watchdog", out,
               "  boot        : #2, after WATCHDOG")
    finally:
        nvm.unlink(missing_ok=True)
        pathlib.Path(f"{nvm}.reset").unlink(missing_ok=True)


def main() -> int:
    if not FSW.exists():
        print("build/fsw not found -- run `make build` first")
        return 2
    if not port_free(50001):
        print("something is already listening on port 50001 (a `make run`?) -- stop it first")
        return 2

    nvm = pathlib.Path(tempfile.gettempdir()) / f"lessons_nvm_{os.getpid()}.bin"
    proc = subprocess.Popen([str(FSW), "--ttc-port", "50001", "--sim-port", "0", "--nvm", str(nvm)],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print("lessons, against a live spacecraft on port 50001")
    try:
        time.sleep(0.5)

        # ---- Part 1 -------------------------------------------------------
        out = run_bash("cd gnd && " + block("02-first-contact", "bash", "send TEST_CONNECTION").split("\n", 1)[1])
        expect("02-first-contact", "a connection test gets its three replies", out,
               "VERIF_ACCEPT_OK    tc(apid=0x00A, seq=0)", "TEST_REPORT",
               "VERIF_COMPLETE_OK  tc(apid=0x00A, seq=0)")
        out = run_bash("cd gnd && " + block("02-first-contact", "bash", "FLY_TO_MARS"))
        expect("02-first-contact", "an unknown command is stopped on the ground", out,
               "unknown telecommand 'FLY_TO_MARS'; known: ADJUST_TIME, DELETE_STORE_UP_TO, DISABLE_EVENT_ACTION")

        out = run_bash(block("04-checksums", "bash", "send_raw"))
        expect("04-checksums", "a flipped bit is caught and reported", out, "TC_REJECTED aux=BAD_CRC")
        out = run_bash(block("04-checksums", "bash", "fec_playground"))
        expect("04-checksums", "Reed-Solomon repairs 16 and refuses 17", out,
               "16    repaired perfectly (16 bytes fixed)",
               "17    TOO DAMAGED -- and it knows it, so it says so")

        out = run_bash(block("05-ccsds-packets", "bash", "--live"))
        check("total:" in out and "SYS_HK" in out and "07FF" not in out,
              "05-ccsds-packets: packet_explorer --live takes apart a real housekeeping packet", out)

        out = run_bash("cd gnd && " + block("06-pus-services", "bash", "SET_TIME_REPORT_RATE").strip()
                       + " --wait 2.5")
        expect("06-pus-services", "the clock reports itself", out, "TIME_REPORT        rate_exp=1")
        out = run_bash("cd gnd && " + block("06-pus-services", "bash", "ADJUST_TIME").strip() + " --wait 2")
        expect("06-pus-services", "the clock is set from the ground", out, "TIME_ADJUSTED aux=844300800")
        out = run_python_in_gnd(block("06-pus-services", "python", "INSERT_ACTIVITIES"))
        expect("06-pus-services", "a time-tagged command runs by itself", out,
               "SCHED_RELEASED aux=500", "TEST_REPORT", "VERIF_COMPLETE_OK  tc(apid=0x00A, seq=500)")

        out = run_python_in_gnd(block("07-did-it-work", "python", "unreliable_radio"))
        expect("07-did-it-work", "COP-1 delivers all six commands through a lossy radio", out,
               "completed: command seq 0", "completed: command seq 5")
        check(out.index("seq 0") < out.index("seq 5"), "07-did-it-work: ... in order", out)

        out = run_bash("cd gnd && python3 -m pyground send REPORT_STORE_SUMMARY store_id=1")
        expect("08-housekeeping-and-events", "the on-board store answers", out, "STORE_SUMMARY      store_id=1")
        out = run_bash("cd gnd && python3 -m pyground send RETRIEVE_BY_TIME store_id=1 from_s=5 to_s=7")
        expect("08-housekeeping-and-events", "stored packets come back marked as replays", out,
               "[replay] t=", "PLAYBACK_DONE aux=")

        out = run_bash("cd gnd && python3 -m pyground send SET_PARAM param_id=1 value=5")
        expect("09-parameters", "an out-of-range value is refused", out, "reason=ILLEGAL_ARG")

        # ---- Part 3 -------------------------------------------------------
        out = run_bash("cd gnd && " + block("17-power-and-modes", "bash", "mode=POINTING").split("\n", 1)[1])
        expect("17-power-and-modes", "POINTING from BOOT is refused with a reason", out,
               "EVENT_LOW          MODE_REFUSED aux=1028")

        out = run_bash("./build/tests/fsw_tests 2>&1 | grep nobody")
        expect("17-power-and-modes", "publishing into the void is still not an error", out,
               "publishing_to_a_topic_nobody_listens_to_is_not_an_error")
        out = run_bash(block("18-when-things-break", "bash", "edac_playground"))
        expect("18-when-things-break", "the syndrome names the broken bit; scrubbing saves the memory", out,
               "      bit 2       0 [0] 1  0  0  1  1       2          bit 2",
               "      1.6 s              0")
    finally:
        proc.terminate()
        proc.wait(timeout=5)
        nvm.unlink(missing_ok=True)

    watchdog_exercise()

    print(f"\n{'FAILED: ' + str(fails) if fails else 'every lesson checked here does what it says'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
