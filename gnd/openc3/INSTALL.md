# Running OpenC3 COSMOS against HYPERSAT

COSMOS is optional. The Python ground station in `gnd/pyground/` needs none of
this and does everything the automated tests need. Set COSMOS up when you want
real telemetry screens, limits monitoring and packet logging.

## 1. Install COSMOS

Follow the current instructions at <https://docs.openc3.com>. In outline:

```bash
git clone https://github.com/OpenC3/cosmos-project.git cosmos
cd cosmos
./openc3.sh run
```

Wait for the containers to come up, then open <http://localhost:2900>.

## 2. Build this plugin

From the repository root:

```bash
make gen                       # ensure the definitions match the dictionary
cd gnd/openc3
# Package this directory as a COSMOS plugin using the openc3 CLI from your
# COSMOS installation, then install the resulting .gem through the admin
# interface at http://localhost:2900/tools/admin
```

The exact packaging command depends on your COSMOS version; consult its
documentation for the current `openc3cli` invocation.

## 3. Start the spacecraft

```bash
make run
```

## 4. Connect

The `SAT_INT` interface connects as a TCP client to
`host.docker.internal:50001`.

- **macOS and Windows:** `host.docker.internal` resolves automatically.
- **Linux:** it may not. Either start the container with
  `--add-host=host.docker.internal:host-gateway`, or edit the `sat_host`
  variable in `plugin.txt` to your host's address on the Docker bridge.

In the COSMOS **Command and Telemetry Server** tool, `SAT_INT` should show as
connected with a rising packet count. Open **Telemetry Viewer** and select the
`SAT OVERVIEW` screen.

## 5. Send a command

Use the **Command Sender** tool: target `SAT`, command `TEST_CONNECTION`. A
`TEST_REPORT` packet and two verification reports should arrive within a
second.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| Interface will not connect | The spacecraft is not running, or something else already holds the single allowed connection — stop `pyground` first |
| Connects but no packets | `host.docker.internal` is not resolving; see step 4 |
| Packets arrive but do not decode | Regenerate with `make gen`. Cross-check with `make monitor`, which uses an independent decoder |
| Commands are rejected with `BAD_CRC` | The CRC protocol line in `plugin.txt` is not filling the `PACKET_CRC` field. Confirm against `make demo`, which is known to work |

If COSMOS and `pyground` disagree, `pyground` is the reference: it is small
enough to read, and the SIL suite proves it against the flight software on
every commit.
