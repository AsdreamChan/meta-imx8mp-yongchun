"""What the tests know about the server under test.

These values mirror uds-server.c (the Claude-drafted C server); a second
server, such as the Python one, has to match them to pass.
"""

IFACE_DEFAULT = "vcan0"

# The server listens on 0x7E0 and answers on 0x7E8; the tester is the
# other way round.
SERVER_RX_ID = 0x7E0
SERVER_TX_ID = 0x7E8

DEFAULT_SESSION = 0x01
EXTENDED_SESSION = 0x03
SECURITY_LEVEL = 1

VIN_DID = 0xF190
PART_NUMBER_DID = 0xF187
UNKNOWN_DID = 0x1234        # not served; listed only so udsoncan will send it

ORIGINAL_VIN = "YONGCHUNIMX8MP001"
NEW_VIN = "NEWVIN01234567890"
PART_NUMBER = "YC-UDS-0001"

# Demonstration algorithm, not security: key = seed XOR SECRET_MASK.
SECRET_MASK = 0x5A3C96F0


def security_algo(level, seed, params):
    """Key for a seed. udsoncan passes the arguments by these names."""
    value = int.from_bytes(seed, "big") ^ SECRET_MASK
    return value.to_bytes(len(seed), "big")


def wrong_key(seed):
    """A key that differs from the right one in the last bit."""
    key = bytearray(security_algo(SECURITY_LEVEL, seed, None))
    key[-1] ^= 0x01
    return bytes(key)
