"""Shared plumbing for scenarios: start the flight binary, connect the
simulator and a ground client, and collect what comes down."""

from __future__ import annotations

import pathlib
import socket
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "gnd"))

from pyground.client import GroundClient          # noqa: E402

FSW = ROOT / "build" / "fsw"


def free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return int(s.getsockname()[1])


class Flight:
    """The flight software as a child process on its own ports and NVM file."""

    def __init__(self, time_scale: float, ttc_port: int | None = None):
        self.ttc, self.sim = ttc_port or free_port(), free_port()
        self.nvm = ROOT / "build" / f"sim_nvm_{self.sim}.bin"
        self.args = [str(FSW), "--ttc-port", str(self.ttc), "--sim-port", str(self.sim),
                     "--time-scale", str(time_scale), "--nvm", str(self.nvm)]

    def __enter__(self):
        self.nvm.unlink(missing_ok=True)
        self.proc = subprocess.Popen(self.args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return self

    def __exit__(self, *_):
        self.proc.terminate()
        try:
            self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.proc.kill()
        self.nvm.unlink(missing_ok=True)


class Downlink:
    """Drains the ground link without ever blocking the lockstep loop."""

    def __init__(self, port: int):
        self.client = GroundClient(port=port)
        self.client.connect()
        self.events: list[tuple[float, str]] = []
        self.event_aux: dict[str, list] = {}
        self.hk: dict[str, dict] = {}

    def drain(self, sim_t: float, on_event=None) -> None:
        for tm in self.client.poll_nowait():
            if tm.name.startswith("EVENT"):
                name = tm.fields["event_name"]
                self.events.append((sim_t, name))
                self.event_aux.setdefault(name, []).append(tm.fields.get("aux"))
                if on_event:
                    on_event(sim_t, name)
            else:
                self.hk[tm.name] = tm.fields

    def saw(self, name: str) -> bool:
        return any(n == name for _, n in self.events)

    def close(self) -> None:
        self.client.close()


class Checks:
    def __init__(self):
        self.fails: list[str] = []

    def __call__(self, ok: bool, what: str) -> None:
        print(f"  {'.' if ok else 'x'}  {what}")
        if not ok:
            self.fails.append(what)

    def result(self) -> int:
        print(f"\n{'FAILED: ' + str(len(self.fails)) if self.fails else 'all checks passed'}")
        return 1 if self.fails else 0
