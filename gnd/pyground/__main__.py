"""
Command-line ground station.

    python -m pyground monitor                      watch the downlink
    python -m pyground send TEST_CONNECTION         uplink a telecommand
    python -m pyground send SET_PARAM param_id=1 value=500
    python -m pyground params                       read every parameter back
    python -m pyground demo                         a scripted pass

Run the flight software first:  ./build/fsw --verbose
"""

from __future__ import annotations

import argparse
import sys
import time

from .client import GroundClient
from .dictionary import COMMANDS, PARAMS
from .packets import build_tc


def _colour(name: str, text: str) -> str:
    """Severity colouring, disabled when stdout is not a terminal."""
    if not sys.stdout.isatty():
        return text
    codes = {"EVENT_HIGH": "31", "EVENT_MEDIUM": "33", "EVENT_LOW": "33",
             "EVENT_INFO": "36", "VERIF_ACCEPT_FAIL": "31",
             "VERIF_COMPLETE_FAIL": "31", "VERIF_ACCEPT_OK": "32",
             "VERIF_COMPLETE_OK": "32"}
    code = codes.get(name)
    return f"\033[{code}m{text}\033[0m" if code else text


def _show(tm) -> None:
    flag = " " if tm.crc_ok else "!"
    line = (f"{flag} t={tm.time_s:10.3f}  apid=0x{tm.apid:03X} seq={tm.sequence_count:5d}  "
            f"{tm.summary()}")
    print(_colour(tm.name, line))


def cmd_monitor(args: argparse.Namespace) -> int:
    with GroundClient(args.host, args.port) as gnd:
        print(f"connected to {args.host}:{args.port}; Ctrl-C to stop\n")
        try:
            while True:
                for tm in gnd.poll(timeout=1.0):
                    _show(tm)
        except KeyboardInterrupt:
            print(f"\n{gnd.tm_received} packets, {gnd.crc_failures} CRC failures")
    return 0


def cmd_send(args: argparse.Namespace) -> int:
    kwargs: dict[str, object] = {}
    for pair in args.args:
        if "=" not in pair:
            print(f"error: arguments must be name=value, got {pair!r}", file=sys.stderr)
            return 2
        key, _, raw = pair.partition("=")
        try:
            kwargs[key] = int(raw)
        except ValueError:
            try:
                kwargs[key] = float(raw)
            except ValueError:
                kwargs[key] = raw   # an enum state name, resolved by build_tc

    try:
        build_tc(args.command, **kwargs)   # validate before opening a socket
    except (KeyError, TypeError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    with GroundClient(args.host, args.port) as gnd:
        gnd.send(args.command, **kwargs)
        print(f"uplinked {args.command} {kwargs if kwargs else ''}\n")
        for tm in gnd.poll(timeout=args.wait):
            _show(tm)
    return 0


def cmd_params(args: argparse.Namespace) -> int:
    """Read every parameter back from the spacecraft, one ST[20,1] at a time."""
    with GroundClient(args.host, args.port) as gnd:
        print(f"{'ID':>3}  {'NAME':<22} {'ON-BOARD':>14}  {'DEFAULT':>12}  UNITS")
        print("-" * 70)
        for param_id, (name, _type, default, _lo, _hi, units, _desc) in sorted(PARAMS.items()):
            gnd.send("REPORT_PARAM", param_id=param_id)
            report = gnd.wait_for("PARAM_REPORT", timeout=2.0)
            if report is None:
                print(f"{param_id:>3}  {name:<22} {'no response':>14}")
                continue
            value = report.fields["value"]
            mark = " " if abs(value - float(default)) < 1e-9 else "*"
            print(f"{param_id:>3}  {name:<22} {value:>14.4g}{mark} {default:>12}  {units}")
        print("\n* differs from the compiled-in default")
    return 0


def cmd_commands(_args: argparse.Namespace) -> int:
    print(f"{'COMMAND':<20} {'PUS':<10} ARGUMENTS")
    print("-" * 70)
    for name, (service, subtype, arg_spec) in sorted(COMMANDS.items()):
        args_text = ", ".join(f"{a}:{t}" for a, t, _e in arg_spec) or "-"
        print(f"{name:<20} ST[{service},{subtype}]".ljust(31) + args_text)
    return 0


def cmd_demo(args: argparse.Namespace) -> int:
    """
    A scripted pass that exercises the whole vertical slice, in the order an
    operator would actually do it: prove the link, look at the spacecraft,
    change something, confirm it changed, then put it back.
    """
    steps = [
        ("connection test", lambda g: g.send("TEST_CONNECTION")),
        ("read the housekeeping period", lambda g: g.send("REPORT_PARAM", param_id=1)),
        ("speed housekeeping up to 4 Hz", lambda g: g.send("SET_PARAM", param_id=1, value=250)),
        ("read it back", lambda g: g.send("REPORT_PARAM", param_id=1)),
        ("try an illegal value", lambda g: g.send("SET_PARAM", param_id=1, value=5)),
        ("silence the power housekeeping", lambda g: g.send("DISABLE_HK", sid=3)),
        ("send an unknown service", lambda g: g.send_raw(_bad_service_packet())),
        ("restore the housekeeping period", lambda g: g.send("SET_PARAM", param_id=1, value=1000)),
        ("re-enable the power housekeeping", lambda g: g.send("ENABLE_HK", sid=3)),
    ]

    with GroundClient(args.host, args.port) as gnd:
        for label, action in steps:
            print(f"\n=== {label} " + "=" * (56 - len(label)))
            action(gnd)
            time.sleep(0.05)
            for tm in gnd.poll(timeout=0.8):
                _show(tm)
        print(f"\n{gnd.tc_sent} telecommands sent, {gnd.tm_received} packets received, "
              f"{gnd.crc_failures} CRC failures")
    return 0


def _bad_service_packet() -> bytes:
    """A well-formed packet addressing a service that does not exist."""
    import struct
    from .packets import crc16
    word0 = (1 << 12) | (1 << 11) | 0x00A
    word1 = (0x3 << 14) | 99
    packet = struct.pack(">HHH", word0, word1, 5 + 2 - 1)
    packet += struct.pack(">BBBH", 0x29, 99, 1, 0)   # service 99: not ours
    packet += struct.pack(">H", crc16(packet))
    return packet


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="pyground", description="HYPERSAT ground station")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=50001)
    sub = parser.add_subparsers(dest="mode", required=True)

    p = sub.add_parser("monitor", help="watch the downlink")
    p.set_defaults(func=cmd_monitor)

    p = sub.add_parser("send", help="uplink one telecommand")
    p.add_argument("command")
    p.add_argument("args", nargs="*", help="name=value pairs")
    p.add_argument("--wait", type=float, default=1.5,
                   help="seconds to watch the downlink afterwards")
    p.set_defaults(func=cmd_send)

    p = sub.add_parser("params", help="read every on-board parameter")
    p.set_defaults(func=cmd_params)

    p = sub.add_parser("commands", help="list the telecommands in the dictionary")
    p.set_defaults(func=cmd_commands)

    p = sub.add_parser("demo", help="a scripted pass exercising the whole slice")
    p.set_defaults(func=cmd_demo)

    args = parser.parse_args(argv)
    try:
        return int(args.func(args))
    except ConnectionError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
