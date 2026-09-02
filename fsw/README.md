# `fsw/` — the flight software

Everything that would be uploaded to a spacecraft.

```
fsw/
├── core/       portable flight core     no OS, no heap, no exceptions
├── hal/        the ports                four interfaces; the portability seam
├── platform/   the adapters             the ONLY OS-aware code in the tree
├── apps/       the applications         TT&C now; ADCS, EPS, FDIR to come
├── generated/  generated from the dictionary — never edit by hand
└── main.cpp    the composition root: the whole spacecraft, wired in one file
```

## The dependency rule

Dependencies point one way only, and this is what makes the thing portable:

```
  main.cpp  ────────────────────────────┐  knows everything
      │                                 │
      ▼                                 ▼
   apps/  ────▶  core/  ────▶  hal/  ◀── platform/
                                          implements the ports
```

- `core/` and `apps/` may include `hal/`. They may **not** include `platform/`.
- `platform/` implements `hal/`. Nothing includes `platform/` except `main.cpp`.
- No system header (`<sys/socket.h>`, `<pthread.h>`, `<time.h>`) appears
  anywhere except under `platform/`.

Porting to FreeRTOS, to bare metal, or to a flight processor means writing a new
directory under `platform/` and a new `main.cpp`. Not one line of `core/` or
`apps/` changes. That claim is the point of the layout, and it is checked by
`make check-layering`.

## Enforced constraints

`fsw_core` and `fsw_apps` are compiled with `-fno-exceptions -fno-rtti` and the
full warning set as errors. These are build failures, not review comments:

| Rule | Why |
|---|---|
| No allocation after init | Fragmentation is unrecoverable in orbit, and worst-case memory must be provable on the ground |
| No exceptions | Unbounded stack use and non-deterministic timing on the control path |
| No RTTI | Costs ROM, and its need usually signals a design that should have been static |
| No `float`/`double` narrowing in silence | `-Wconversion` catches the truncation that quietly corrupts a telemetry field |
| One thread | Determinism. A bug seen once in orbit must be reproducible on the ground |

## Building

```bash
make build        # everything, plus the unit tests
make run          # start the spacecraft
```

See the `README.md` in each subdirectory for what belongs there.
