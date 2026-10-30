# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong

"""PicoPWM HID client: identity, configuration, channel status, and the report 20 transaction."""

from __future__ import annotations

import threading
import time
from typing import Any, Callable

from . import protocol as p
from .transport import open_device

ACTION_TIMEOUT_S = 3.0
SAVE_TIMEOUT_S = 15.0
POLL_S = 0.05


class DeviceError(RuntimeError):
    """The device rejected a command, or its outcome could not be confirmed."""


def _blank_channels() -> list[dict[str, Any]]:
    return [{"channel": channel, "freq_hz": None, "duty": None} for channel in range(p.CHANNEL_COUNT)]


class PicoPwmHid:
    """Thread-safe client for one board; opens the device lazily and reopens after an I/O failure."""

    def __init__(self, opener: Callable[[], Any] = open_device, sleep: Callable[[float], None] = time.sleep,
                 monotonic: Callable[[], float] = time.monotonic):
        self._opener = opener
        self._sleep = sleep
        self._monotonic = monotonic
        self._lock = threading.RLock()
        self._device: Any = None
        self._identity: dict[str, Any] | None = None
        self._configuration: dict[str, Any] | None = None
        self._pins: dict[str, Any] | None = None
        self._transaction_id = 0

    def close(self) -> None:
        with self._lock:
            device, self._device = self._device, None
            self._identity = self._configuration = None
            self._pins = None
            if device is not None:
                try:
                    device.close()
                except OSError:
                    pass

    def _open(self) -> Any:
        if self._device is None:
            self._device = self._opener()
        return self._device

    def _read(self, report_id: int, size: int) -> bytes:
        # hidapi returns the report ID as the first byte for numbered reports.
        data = bytes(self._open().get_feature_report(report_id, size + 1))
        if len(data) != size + 1 or data[0] != report_id:
            raise p.ProtocolError(f"feature report {report_id} must contain exactly {size} payload bytes")
        return data[1:]

    def _send(self, report_id: int, payload: bytes) -> None:
        if self._open().send_feature_report(bytes([report_id]) + payload) < 0:
            raise OSError("send_feature_report failed")

    def _ensure_identity(self) -> dict[str, Any]:
        if self._identity is None:
            identity = p.decode_identity(self._read(p.IDENTITY_REPORT, p.IDENTITY_SIZE))
            if identity["can_read_memory"]:
                identity.update(p.decode_memory(self._read(p.MEMORY_REPORT, p.MEMORY_SIZE)))
            self._identity = identity
        return self._identity

    def status(self) -> dict[str, Any]:
        """Read the dashboard state; connection problems are reported in the result, not raised."""
        with self._lock:
            state: dict[str, Any] = {
                "connected": False, "identity": None, "identity_error": None, "configuration": None,
                "configuration_error": None, "channels": _blank_channels(),
                "bank_errors": [None] * p.BANK_COUNT, "error": None,
                "runtime": None, "runtime_error": None,
                "pins": None, "pins_error": None,
            }
            try:
                state["identity"] = self._ensure_identity()
                state["connected"] = True
                if self._identity["can_read_pins"]:
                    try:
                        if self._pins is None:
                            self._pins = p.decode_pins(self._read(p.PINS_REPORT, p.PINS_SIZE))
                        state["pins"] = self._pins
                    except p.ProtocolError as error:
                        state["pins_error"] = str(error)
                self._configuration = None
                try:
                    self._configuration = p.decode_configuration(
                        self._read(p.CONFIGURATION_REPORT, p.CONFIGURATION_SIZE))
                except p.ProtocolError as error:
                    state["configuration_error"] = str(error)
                state["configuration"] = self._configuration
                if self._identity["can_read_runtime"]:
                    try:
                        state["runtime"] = p.decode_runtime(self._read(p.RUNTIME_REPORT, p.RUNTIME_SIZE))
                    except p.ProtocolError as error:
                        state["runtime_error"] = str(error)
                for bank in range(p.BANK_COUNT):
                    try:
                        entries = p.decode_bank_status(
                            self._read(p.BANK_STATUS_REPORT + bank, p.BANK_STATUS_SIZE), bank)
                        state["channels"][bank * p.BANK_SIZE:(bank + 1) * p.BANK_SIZE] = entries
                    except p.ProtocolError as error:
                        state["bank_errors"][bank] = str(error)
            except p.ProtocolError as error:
                self.close()
                state.update(connected=False, identity=None, pins=None, identity_error=str(error), error=str(error))
            except (OSError, RuntimeError) as error:
                self.close()
                state.update(connected=False, identity=None, pins=None, error=str(error))
            return state

    def _perform(self, operation: int, **fields: int) -> None:
        identity = self._ensure_identity()
        capability = {p.OP_SAVE_CONFIG: "can_save", p.OP_SET_CLOCK_PROFILE: "can_set_clock",
                      p.OP_SAVE_REBOOT: "can_save_reboot", p.OP_SET_LED: "can_set_led", p.OP_REBOOT: "can_reboot"}
        if operation in capability and not identity[capability[operation]]:
            raise DeviceError("the firmware does not advertise this capability")
        saving = operation in (p.OP_SAVE_CONFIG, p.OP_SAVE_REBOOT)
        timeout = SAVE_TIMEOUT_S if saving else ACTION_TIMEOUT_S
        if p.decode_action_result(self._read(p.ACTION_REPORT, p.ACTION_SIZE))["status"] == p.STATUS_PENDING:
            raise DeviceError("a previous command is still pending")
        self._transaction_id = self._transaction_id % 255 + 1
        transaction_id = self._transaction_id
        self._send(p.ACTION_REPORT, p.encode_action_request(transaction_id, operation, **fields))
        deadline = self._monotonic() + timeout
        first = True
        while True:
            result = p.decode_action_result(self._read(p.ACTION_REPORT, p.ACTION_SIZE))
            if result["transaction_id"] != transaction_id or result["operation"] != operation:
                raise DeviceError("the device did not accept the request; no change was made" if first
                                  else "the result no longer matches the request; outcome is unconfirmed")
            first = False
            if result["status"] != p.STATUS_PENDING:
                if result["status"] != p.STATUS_OK:
                    raise DeviceError(f"command failed: {result['status_name']}")
                return
            if self._monotonic() >= deadline:
                raise DeviceError("timed out awaiting the result; outcome is unconfirmed")
            self._sleep(POLL_S)

    def _run(self, operation: int, configuration: bool, **fields: int) -> dict[str, Any]:
        with self._lock:
            try:
                self._perform(operation, **fields)
            except (OSError, p.ProtocolError, RuntimeError) as error:
                if isinstance(error, DeviceError):
                    raise
                self.close()
                raise DeviceError(f"HID transfer failed; outcome is unconfirmed: {error}") from error
            if configuration:
                self._configuration = None
            return self.status()

    def set_channel(self, channel: int, freq_hz: int, duty: int) -> dict[str, Any]:
        if not 0 <= channel < p.CHANNEL_COUNT:
            raise ValueError(f"channel must be between 0 and {p.CHANNEL_COUNT - 1}")
        if not 0 <= freq_hz <= 0xFFFFFFFF:
            raise ValueError("freq_hz must be an unsigned 32-bit integer")
        if not 0 <= duty <= 100:
            raise ValueError("duty must be between 0 and 100")
        try:
            return self._run(p.OP_SET_CHANNEL, False, channel=channel, arg0=freq_hz, arg1=duty)
        except DeviceError as error:
            if str(error) != "command failed: INVALID":
                raise
            for attempt in range(3):
                state = self.status()
                actual = state["channels"][channel]
                if actual["freq_hz"] == freq_hz and actual["duty"] == duty:
                    return state
                if attempt < 2:
                    self._sleep(POLL_S)
            readback = (f"{actual['freq_hz']} Hz / {actual['duty']}%"
                        if actual["freq_hz"] is not None else "unavailable")
            raise DeviceError(
                f"command failed: INVALID (requested {freq_hz} Hz / {duty}%, readback {readback})") from error

    def stop(self) -> dict[str, Any]:
        return self._run(p.OP_STOP, False)

    def set_bank(self, bank: int, backend: int, role: int) -> dict[str, Any]:
        if bank not in (0, 1, 2) or backend not in (0, 1, 2) or role not in (0, 1):
            raise ValueError("bank, backend, or role is out of range")
        return self._run(p.OP_SET_BANK, True, channel=bank, arg0=backend | role << 8)

    def set_i2c_address(self, address: int) -> dict[str, Any]:
        if not 0 <= address <= 127:
            raise ValueError("I2C address must be 0-127")
        return self._run(p.OP_SET_I2C_ADDRESS, True, arg0=address)

    def set_clock_profile(self, profile: int, acknowledge_overclock: bool) -> dict[str, Any]:
        with self._lock:
            mask = self.status()["identity"]["clock_profile_mask"] if self._identity is None else \
                self._identity["clock_profile_mask"]
        if not 0 <= profile <= 4 or not mask & 1 << profile:
            raise ValueError("choose a clock profile the firmware advertises")
        if profile > 1 and not acknowledge_overclock:
            raise ValueError("acknowledge the overclock first")
        return self._run(p.OP_SET_CLOCK_PROFILE, True, arg0=profile,
                         flags=p.FLAG_ACK_OVERCLOCK if profile > 1 else 0)

    def save_configuration(self, acknowledge_overclock: bool) -> dict[str, Any]:
        with self._lock:
            state = self.status()
            if state["configuration"] is None:
                raise DeviceError("read the configuration first")
            overclock = state["configuration"]["clock_profile"] > 1
        if overclock and not acknowledge_overclock:
            raise ValueError("acknowledge the overclock first")
        return self._run(p.OP_SAVE_CONFIG, True,
                         flags=p.FLAG_CONFIRM_SAVE | (p.FLAG_ACK_OVERCLOCK if overclock else 0))

    def set_led(self, on: bool) -> dict[str, Any]:
        return self._run(p.OP_SET_LED, False, arg0=int(on))

    def reboot(self) -> dict[str, Any]:
        with self._lock:
            self._run_reboot(p.OP_REBOOT, p.FLAG_CONFIRM_SAVE)
            return {"rebooting": True}

    def _run_reboot(self, operation: int, flags: int) -> None:
        try:
            self._perform(operation, flags=flags)
        except (OSError, p.ProtocolError, RuntimeError) as error:
            if isinstance(error, DeviceError):
                raise
            raise DeviceError(f"HID transfer failed; outcome is unconfirmed: {error}") from error
        finally:
            self.close()

    def save_and_reboot(self, profile: int, address: int, roles: list[int],
                        acknowledge_overclock: bool) -> dict[str, Any]:
        """Validate the entire draft before submitting targets and one confirmed save/reboot."""
        with self._lock:
            identity = self._ensure_identity()
            if not identity["can_save_reboot"] or not identity["can_set_clock"]:
                raise DeviceError("update firmware to enable Save and Reboot")
            if not 0 <= profile <= 4 or not identity["clock_profile_mask"] & 1 << profile:
                raise ValueError("choose a clock profile the firmware advertises")
            if not 8 <= address <= 119 or len(roles) != 3 or any(type(role) is not int or role not in (0, 1)
                                                              for role in roles):
                raise ValueError("expected I2C address 8-119 and three bank roles")
            if profile > 1 and not acknowledge_overclock:
                raise ValueError("acknowledge the overclock first")
            try:
                self._perform(p.OP_SET_CLOCK_PROFILE, arg0=profile,
                              flags=p.FLAG_ACK_OVERCLOCK if profile > 1 else 0)
                self._perform(p.OP_SET_I2C_ADDRESS, arg0=address)
                for bank, role in enumerate(roles):
                    self._perform(p.OP_SET_BANK, channel=bank, arg0=bank | role << 8)
                self._run_reboot(p.OP_SAVE_REBOOT,
                                 p.FLAG_CONFIRM_SAVE | (p.FLAG_ACK_OVERCLOCK if profile > 1 else 0))
            except (OSError, p.ProtocolError, RuntimeError) as error:
                self.close()
                raise DeviceError(f"Save and Reboot did not complete; target settings may have changed: {error}") from error
            return {"rebooting": True}
