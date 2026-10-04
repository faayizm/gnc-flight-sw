# `fsw/apps/io/` — the hardware boundary

On a real spacecraft this is the device drivers. Here it is the simulator
bridge, and still the only code that talks to "hardware":

| File | What |
|---|---|
| `sim_bridge.hpp/.cpp` | The bridge protocol. The header comment *is* the wire specification; `sim/sil/bridge.py` mirrors it |
| `sim_io_app.hpp/.cpp` | Publishes each sensor sample on `Topic::SensorData`, then replies with one actuator frame built from what ADCS and EPS published in response |

The bus is synchronous, so by the time `publish()` returns, every consumer of
a sample has run and answered. The simulator's lockstep, and with it the
determinism of every scenario, depends on that.
