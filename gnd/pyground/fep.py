"""
Front-end processor: the part of a ground station that turns a coded radio
link into plain packets for the mission control system.

    spacecraft  <-- CLTUs / CADUs -->  FEP  <-- Space Packets over TCP -->  COSMOS

COSMOS is excellent at packets and knows nothing of transfer frames,
Reed-Solomon or COP-1. Real missions put a front end between the two for the
same reason (a modem plus an SLE provider, typically); this is that, in a
hundred lines. Telecommands from COSMOS are sent sequence-controlled through
FOP-1, so COSMOS gets guaranteed in-order delivery without knowing it exists.

    python3 -m pyground --port 50001 fep --listen 50002
"""

from __future__ import annotations

import socket
import time

from .client import GroundClient


def run(host: str, port: int, listen: int, verbose: bool = True) -> int:
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("0.0.0.0", listen))
    server.listen(1)
    server.setblocking(False)
    print(f"FEP: spacecraft at {host}:{port}, serving packets on port {listen}")

    with GroundClient(host, port) as gnd:
        peer: socket.socket | None = None
        uplink = bytearray()
        last_report = time.monotonic()
        while True:
            if peer is None:
                try:
                    peer, addr = server.accept()
                    peer.setblocking(False)
                    uplink.clear()
                    print(f"FEP: mission control connected from {addr[0]}")
                except BlockingIOError:
                    pass

            for tm in gnd.poll(timeout=0.02):
                if peer is not None:
                    try:
                        peer.sendall(tm.raw)
                    except OSError:
                        peer = None
                        print("FEP: mission control disconnected")

            if peer is not None:
                try:
                    chunk = peer.recv(65536)
                    if not chunk:
                        peer = None
                        print("FEP: mission control disconnected")
                    else:
                        uplink += chunk
                except BlockingIOError:
                    pass
                # Whole packets only, by the CCSDS length field. This side of
                # the FEP is a reliable local socket, so length framing is safe.
                while len(uplink) >= 6:
                    total = 6 + int.from_bytes(uplink[4:6], "big") + 1
                    if len(uplink) < total:
                        break
                    gnd.send_packet(bytes(uplink[:total]))
                    del uplink[:total]

            if verbose and time.monotonic() - last_report > 10.0:
                last_report = time.monotonic()
                s = gnd.link
                print(f"FEP: {s.cadus} frames, {s.rs_corrected} RS symbols corrected, "
                      f"{s.rs_failed} uncorrectable, lost {sum(s.frames_lost.values())}; "
                      f"COP-1 {gnd.fop.outstanding} outstanding, {gnd.fop.retransmissions} retransmitted")
