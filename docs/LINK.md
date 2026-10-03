# The space link

How bytes get between the ground and the spacecraft, and why they arrive
intact. The flight side is in [`fsw/apps/ttc/`](../fsw/apps/ttc/); the ground
side is [`gnd/pyground/link.py`](../gnd/pyground/link.py), written separately
from the standards rather than translated from the C++.

```
 GROUND                                                     SPACECRAFT
 ──────                                                     ──────────
 telecommand packet                                         TtcApp: PUS services
   │ FOP-1: sequence number, window, retransmission           ▲
   ▼                                                          │ accepted frames
 TC transfer frame (CCSDS 232.0-B), CRC-16                  FARM-1: in order only
   │                                                          ▲
   ▼                                                          │ frame checks
 CLTU (231.0-B): EB90 + BCH(63,56) codeblocks + tail  ────▶ CLTU decoder: hunt, BCH
                                                         
 packets ◀── reassembly per virtual channel ◀── TM frame ◀── TmFramer: VC0 live,
   ▲            (first header pointer)       (132.0-B)        VC1 playback, VC7 idle
   │                                            ▲              │ CLCW in every OCF
 CLCW ──▶ FOP-1                    RS decode ◀── │              ▼
                               de-randomise ◀── ASM sync ◀── RS(255,223) + randomise + ASM
```

## Downlink: TM frames and CADUs

**Frames.** Every TM transfer frame is 223 bytes: one Reed-Solomon codeword.
It has a 6-byte header, a 213-byte data field and a 4-byte operational
control field carrying the CLCW. Packets are laid end to end across frames.
The first header pointer gives the offset of the first packet that *starts*
in this frame, which is how a receiver that lost a frame resynchronises on
the next. `0x7FF` means no packet starts in this frame; `0x7FE` means it
carries idle data only.

**Virtual channels** keep live telemetry (VC0) from queueing behind playback
(VC1). Idle frames (VC7) go out at least every half second, so the CLCW keeps
reaching the ground when there is nothing else to send. Each VC has its own
frame counter, so a gap shows which stream lost data.

**Latency.** A frame goes out when its data field is full, or 100 ms after
its oldest byte arrived, with the rest filled by an idle packet. Without the
time limit, a lone event report would wait for unrelated telemetry to push it
out.

**Coding** (131.0-B):

| Step | What it buys |
|---|---|
| Reed-Solomon (255,223), dual basis | Corrects any 16 corrupted bytes per frame |
| Pseudo-randomiser, h(x) = x⁸+x⁷+x⁵+x³+1 | Bit transitions for the receiver's clock recovery |
| Attached sync marker `1ACFFC1D` | A receiver can find frame boundaries in a stream it joined anywhere |

The RS encoder (C++) and encoder/decoder (Python) are both checked against
vectors from Phil Karn's libfec. That ties each one to an outside reference,
not only to each other. The dual-basis conversion is the classic source of
CCSDS RS bugs.

At two frames per 20 ms tick the downlink carries 100 frames/s, about
207 kbit/s coded: roughly an S-band CubeSat radio.

## Uplink: CLTUs and TC frames

**CLTU** (231.0-B): start sequence `EB90`, then 8-byte codeblocks, each 7
information bytes plus a BCH(63,56) parity byte, then the tail sequence. The
decoder hunts for the start sequence, which is what recovers framing after
noise. It corrects one bit per codeblock. An uncorrectable codeblock ends the
CLTU, and the frame layer's checks reject what is left.

**TC frame** (232.0-B): a 5-byte header (bypass and control flags, spacecraft
ID, virtual channel, length, sequence number), the data, and a CRC-16. Frames
for another spacecraft, a wrong length or a bad CRC are dropped silently.
None of their fields can be trusted to say who to report to.

## COP-1: guaranteed, ordered delivery

**FARM-1** (on board) accepts a sequence-controlled frame only if its number
N(S) equals the expected V(R):

| N(S) relative to V(R) | Response |
|---|---|
| equal | Accept, V(R)+1 |
| ahead, within W/2 | Discard; set *retransmit*: a frame went missing |
| behind, within W/2 | Discard quietly: a duplicate |
| anywhere else | Discard; **lockout** until the ground sends Unlock |

The state reaches the ground in the CLCW, in every TM frame.

**FOP-1** (ground) keeps every frame until the CLCW acknowledges it. It
retransmits everything unacknowledged on the retransmit flag or a timeout. It
keeps at most W/2 frames outstanding, so one lost frame cannot push later
ones out of the FARM's window. At start it sends Unlock and Set V(R), so both
ends agree.

**Bypass frames** (type BD) skip FARM-1's sequencing. They exist for when the
sequence itself is broken, and for tests that deliberately send malformed
commands.

## The front-end processor

COSMOS speaks packets. `make fep` runs `pyground fep`, which holds the coded
link and serves plain packets on TCP 50002. It is the same split real
missions have between the ground station's modem and SLE provider and mission
control.

## What the tests prove

- C++ unit tests: RS against libfec; the randomiser's first bytes; BCH
  corrects all 63 single-bit errors and flags a double; spanning packets and
  the first header pointer; CLTU decoding after garbage and with bit errors;
  FARM-1's whole table, lockout and Unlock included.
- Python tests (`make test-gnd`): the same RS vectors, plus libfec's own
  decode case; 200 random codewords with up to 16 errors repaired; beyond 16
  flagged, never mis-corrected; frame reassembly through noise and
  mid-stream joins; FOP-1's window and retransmission.
- SIL (`make sil`): a CLTU with three bit errors after a burst of garbage
  still executes. With every third uplink CLTU lost, six commands all
  execute, once each, in order.
