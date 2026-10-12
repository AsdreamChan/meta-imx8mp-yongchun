

import pytest
import subprocess


def pytest_addoption(parser):
    parser.addoption(
        "--board", action="store", default="root@10.194.1.225", help="board to test, as user@host"
    )

@pytest.fixture(scope="session")
def board(request):
    return request.config.getoption("--board")


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
