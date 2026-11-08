# UDS server tests (pytest + udsoncan)

Written by Claude on 2026-11-06. They check a UDS server over a kernel
ISO-TP socket, using udsoncan as the tester. Each test starts its own
server process, so tests do not share state. They run on the build host
over `vcan0`, not on the board.

The expected negative response codes are the ones the C server
(`recipes-examples/uds-server/`, also drafted by Claude) returns, so
these are regression tests for that server, not a conformance test
against ISO 14229-1.

## Setup (once)

udsoncan is in `tests/pyproject.toml` with the board tests' packages:

```sh
cd tests
uv sync
```

## Run

From `tests/`:

```sh
sudo modprobe vcan && sudo modprobe can-isotp
sudo ip link add dev vcan0 type vcan && sudo ip link set vcan0 up   # if missing

gcc -Wall -Wextra -o /tmp/uds-server ../recipes-examples/uds-server/files/uds-server.c
UDS_SERVER=/tmp/uds-server uv run pytest uds -v
```

Nothing else may answer on 0x7E0/0x7E8 while the tests run: stop any
`uds-server` or `isotprecv` started by hand. `candump -tA vcan0` in
another window is fine and shows what udsoncan sends.

- `UDS_SERVER`: the command that starts the server; the interface name
  is appended. For the Python server: `UDS_SERVER="python3 uds_server.py"`.
- `UDS_IFACE`: the interface, default `vcan0`.

Without `UDS_SERVER`, or without the interface, the tests are skipped.

## Files

| File | What |
|---|---|
| `uds_config.py` | IDs, DIDs, the expected strings, the demonstration seed/key algorithm |
| `conftest.py` | Fixtures: the interface check, one server per test, a udsoncan client |
| `test_uds_server.py` | 15 tests for 0x10, 0x22, 0x27 and 0x2E |

The server must log each request as `req: XX XX ...`: one test reads the
log to see that udsoncan sent no key for an all-zero seed.
