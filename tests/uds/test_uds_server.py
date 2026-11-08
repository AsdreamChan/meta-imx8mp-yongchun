"""UDS server behaviour, checked with udsoncan over ISO-TP.

The cases follow t2.c, which checked the same behaviour without a
socket. Negative responses arrive as NegativeResponseException; the
NRC is in exc.response.code.
"""

import pytest
from udsoncan.exceptions import NegativeResponseException

import uds_config as cfg

NRC_SUBFUNCTION_NOT_SUPPORTED = 0x12
NRC_REQUEST_SEQUENCE_ERROR = 0x24
NRC_REQUEST_OUT_OF_RANGE = 0x31
NRC_SECURITY_ACCESS_DENIED = 0x33
NRC_INVALID_KEY = 0x35
NRC_EXCEEDED_ATTEMPTS = 0x36
NRC_TIME_DELAY_NOT_EXPIRED = 0x37
NRC_NOT_SUPPORTED_IN_SESSION = 0x7F


def nrc_of(call, *args):
    """Run a client call that must fail, and return its NRC."""
    with pytest.raises(NegativeResponseException) as exc:
        call(*args)
    return exc.value.response.code


def unlock(client):
    client.change_session(cfg.EXTENDED_SESSION)
    client.unlock_security_access(cfg.SECURITY_LEVEL)


# --- 0x10 DiagnosticSessionControl ------------------------------------------

def test_extended_session_reports_p2_timing(client):
    response = client.change_session(cfg.EXTENDED_SESSION)
    assert response.service_data.p2_server_max == pytest.approx(0.050)
    assert response.service_data.p2_star_server_max == pytest.approx(5.0)


def test_suppressed_session_change_still_switches(client, server):
    with client.suppress_positive_response:
        assert client.change_session(cfg.EXTENDED_SESSION) is None
    # A seed is only given in the extended session.
    client.request_seed(cfg.SECURITY_LEVEL)
    assert server.requests()[0] == "10 83"


# --- 0x22 ReadDataByIdentifier ----------------------------------------------

def test_read_vin(client):
    response = client.read_data_by_identifier(cfg.VIN_DID)
    assert response.service_data.values[cfg.VIN_DID] == cfg.ORIGINAL_VIN


def test_read_part_number(client):
    response = client.read_data_by_identifier(cfg.PART_NUMBER_DID)
    assert response.service_data.values[cfg.PART_NUMBER_DID] == cfg.PART_NUMBER


def test_read_unknown_did_is_out_of_range(client):
    assert nrc_of(client.read_data_by_identifier,
                  cfg.UNKNOWN_DID) == NRC_REQUEST_OUT_OF_RANGE


# --- 0x27 SecurityAccess ----------------------------------------------------

def test_no_seed_in_default_session(client):
    assert nrc_of(client.request_seed,
                  cfg.SECURITY_LEVEL) == NRC_NOT_SUPPORTED_IN_SESSION


def test_key_without_seed_is_a_sequence_error(client):
    client.change_session(cfg.EXTENDED_SESSION)
    assert nrc_of(client.send_key, cfg.SECURITY_LEVEL,
                  bytes(4)) == NRC_REQUEST_SEQUENCE_ERROR


def test_wrong_key_is_rejected(client):
    client.change_session(cfg.EXTENDED_SESSION)
    seed = client.request_seed(cfg.SECURITY_LEVEL).service_data.seed
    assert nrc_of(client.send_key, cfg.SECURITY_LEVEL,
                  cfg.wrong_key(seed)) == NRC_INVALID_KEY


def test_unlocked_server_gives_zero_seed_and_no_key_is_sent(client, server):
    unlock(client)
    response = client.unlock_security_access(cfg.SECURITY_LEVEL)
    assert response.service_data.seed == bytes(4)
    # udsoncan sends no key for an all-zero seed: only the first unlock did.
    keys = [r for r in server.requests() if r.startswith("27 02")]
    assert len(keys) == 1


def test_three_wrong_keys_lock_out_the_seed(client):
    client.change_session(cfg.EXTENDED_SESSION)
    codes = []
    for _ in range(3):
        seed = client.request_seed(cfg.SECURITY_LEVEL).service_data.seed
        codes.append(nrc_of(client.send_key, cfg.SECURITY_LEVEL,
                            cfg.wrong_key(seed)))
    assert codes == [NRC_INVALID_KEY, NRC_INVALID_KEY, NRC_EXCEEDED_ATTEMPTS]
    assert nrc_of(client.request_seed,
                  cfg.SECURITY_LEVEL) == NRC_TIME_DELAY_NOT_EXPIRED


# --- 0x2E WriteDataByIdentifier ---------------------------------------------

def test_no_write_in_default_session(client):
    assert nrc_of(client.write_data_by_identifier, cfg.VIN_DID,
                  cfg.NEW_VIN) == NRC_NOT_SUPPORTED_IN_SESSION


def test_write_needs_security_access(client):
    client.change_session(cfg.EXTENDED_SESSION)
    assert nrc_of(client.write_data_by_identifier, cfg.VIN_DID,
                  cfg.NEW_VIN) == NRC_SECURITY_ACCESS_DENIED


def test_read_only_did_cannot_be_written(client):
    unlock(client)
    assert nrc_of(client.write_data_by_identifier, cfg.PART_NUMBER_DID,
                  "X" * len(cfg.PART_NUMBER)) == NRC_REQUEST_OUT_OF_RANGE


def test_write_vin_and_read_it_back(client):
    unlock(client)
    client.write_data_by_identifier(cfg.VIN_DID, cfg.NEW_VIN)
    response = client.read_data_by_identifier(cfg.VIN_DID)
    assert response.service_data.values[cfg.VIN_DID] == cfg.NEW_VIN


def test_session_change_locks_again(client):
    unlock(client)
    client.change_session(cfg.EXTENDED_SESSION)
    assert nrc_of(client.write_data_by_identifier, cfg.VIN_DID,
                  cfg.NEW_VIN) == NRC_SECURITY_ACCESS_DENIED
