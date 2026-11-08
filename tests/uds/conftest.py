"""Fixtures for testing a UDS server over ISO-TP.

Each test gets its own server process, so it starts in the default
session, locked, with the original VIN and no failed key attempts. The
tester is udsoncan over a kernel ISO-TP socket (SocketConnection), so
neither can-isotp nor python-can is needed.

Environment:
  UDS_SERVER  command that starts the server; the interface name is
              appended (for example ~/uds-work/v2/uds-server, or
              "python3 uds_server.py")
  UDS_IFACE   CAN interface, default vcan0
"""

import os
import shlex
import socket
import subprocess
import time
from pathlib import Path

import pytest
import udsoncan
from udsoncan.client import Client
from udsoncan.configs import default_client_config
from udsoncan.connections import SocketConnection

import uds_config as cfg

STARTUP_TIMEOUT = 2.0


class Server:
    """A running server and the log it writes (req:/rsp: lines)."""

    def __init__(self, proc: subprocess.Popen, log: Path):
        self.proc = proc
        self.log = log

    def requests(self) -> list[str]:
        """Requests the server has logged, as hex strings such as '27 01'."""
        return [line[len("req: "):]
                for line in self.log.read_text().splitlines()
                if line.startswith("req: ")]


@pytest.fixture(scope="session")
def iface() -> str:
    name = os.environ.get("UDS_IFACE", cfg.IFACE_DEFAULT)
    try:
        socket.if_nametoindex(name)
    except OSError:
        pytest.skip(f"{name} does not exist; for vcan0: "
                    "sudo ip link add dev vcan0 type vcan && "
                    "sudo ip link set vcan0 up")
    return name


@pytest.fixture
def server(iface: str, tmp_path: Path):
    command = os.environ.get("UDS_SERVER")
    if not command:
        pytest.skip("set UDS_SERVER to the command that starts the server")

    argv = [os.path.expanduser(arg) for arg in shlex.split(command)]
    log = tmp_path / "server.log"
    with open(log, "w") as out:
        proc = subprocess.Popen(argv + [iface], stdout=out,
                                stderr=subprocess.STDOUT)

    # Wait for the banner: a request sent before the server has bound
    # its socket would go unanswered.
    deadline = time.monotonic() + STARTUP_TIMEOUT
    while "UDS server on" not in log.read_text():
        if proc.poll() is not None:
            pytest.fail(f"server exited early:\n{log.read_text()}")
        if time.monotonic() > deadline:
            proc.kill()
            pytest.fail("server did not print its banner in time")
        time.sleep(0.01)

    yield Server(proc, log)

    proc.terminate()
    proc.wait(timeout=STARTUP_TIMEOUT)


@pytest.fixture
def client(server: Server, iface: str):
    sock = socket.socket(socket.AF_CAN, socket.SOCK_DGRAM, socket.CAN_ISOTP)
    # (interface, rx_addr, tx_addr): the tester receives what the server sends.
    sock.bind((iface, cfg.SERVER_TX_ID, cfg.SERVER_RX_ID))

    config = dict(default_client_config)
    config["data_identifiers"] = {
        cfg.VIN_DID: udsoncan.AsciiCodec(len(cfg.ORIGINAL_VIN)),
        cfg.PART_NUMBER_DID: udsoncan.AsciiCodec(len(cfg.PART_NUMBER)),
        cfg.UNKNOWN_DID: ">H",
    }
    config["security_algo"] = cfg.security_algo
    config["request_timeout"] = 2

    with Client(SocketConnection(sock), config=config) as uds:
        yield uds
    sock.close()        # SocketConnection.close() leaves the socket open
