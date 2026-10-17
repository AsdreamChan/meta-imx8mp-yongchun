

import pytest
import subprocess
import os
import serial


def pytest_addoption(parser):
    parser.addoption(
        "--board", action="store", default="root@10.194.1.225", help="board to test, as user@host"
    )
    parser.addoption(
        "--console", action="store", default="/dev/ttyACM0", help="console to test"
    )

@pytest.fixture(scope="session")
def board(request):
    return request.config.getoption("--board")

@pytest.fixture(scope="function")
def console(request):
    path = request.config.getoption("--console")
    if os.path.exists(path):
        port = serial.Serial(path, 115200, timeout=1)
        yield port
        port.close()
    else:
        pytest.skip("no serial console at " + path)


@pytest.fixture(scope="session")
def ssh(board):
    def _ssh(cmd, timeout=30):
        result = subprocess.run(
                ['ssh',
                 '-o', 'BatchMode=yes',
                 '-o', 'ConnectTimeout=5',

                 # A lab board: its host key changes on every reflash and no one is there
                 # to answer the prompt. Fine for an EVB, not for a product.
                 '-o', 'StrictHostKeyChecking=no',
                 '-o', 'UserKnownHostsFile=/dev/null',

                 board, cmd],
                capture_output=True, text=True, timeout=timeout)
        return result

    return _ssh
