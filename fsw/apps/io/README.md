# `fsw/apps/io/` — the hardware boundary

On a real spacecraft this is the device drivers. Here it is the simulator
bridge, and still the only code that talks to "hardware":

| File | What |
|---|---|
| `sim_bridge.hpp/.cpp` | The bridge protocol. The header comment *is* the wire specification; `sim/sil/bridge.py` mirrors it |
| `sensor_screen.hpp` | Refuses readings that are physically impossible, or frozen (identical, ten in a row), by clearing their valid flags before anyone else sees them; readmits a sensor after twenty good readings |
| `sim_io_app.hpp/.cpp` | Screens each sensor sample, reports gaps in its timestamps, publishes it on `Topic::SensorData`, then replies with one actuator frame built from what ADCS and EPS published in response |

The bus is synchronous, so by the time `publish()` returns, every consumer of
a sample has run and answered. The simulator's lockstep, and with it the
determinism of every scenario, depends on that.
