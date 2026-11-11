# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong

"""PicoPWM HID client: identity, configuration, channel status, and the report 20 transaction."""

from __future__ import annotations

from copy import deepcopy
from types import TracebackType
import threading
import time
import logging
from typing import Any, Callable, TypedDict

from ..hid import protocol as p
from ..hid.transport import open_device

ACTION_TIMEOUT_S = 3.0
SAVE_TIMEOUT_S = 15.0
POLL_S = 0.05


class DeviceStatus(TypedDict):
    """Device snapshot; unavailable reports carry explicit per-report errors."""

    connected: bool
    identity: p.Identity | None
    identity_error: str | None
    configuration: p.Configuration | None
    configuration_error: str | None
    channels: list[p.ChannelState]
    bank_errors: list[str | None]
    error: str | None
    runtime: p.Runtime | None
    runtime_error: str | None
    pins: p.Pins | None
    pins_error: str | None


class DeviceError(RuntimeError):
    """The device rejected a command, or its outcome could not be confirmed."""

    def __init__(self, message: str, *, status: int | None = None):
        super().__init__(message)
        self.status = status


class RebootResult(TypedDict):
    """Confirmed reboot command acknowledgement, not reconnection verification."""

    rebooting: bool


def _integer(value: int, name: str, minimum: int, maximum: int) -> None:
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError(f"{name} must be an integer between {minimum} and {maximum}")


def _boolean(value: bool, name: str) -> None:
    if type(value) is not bool:
        raise ValueError(f"{name} must be a boolean")


def _blank_channels() -> list[p.ChannelState]:
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
        self._identity: p.Identity | None = None
        self._configuration: p.Configuration | None = None
        self._pins: p.Pins | None = None
        self._transaction_id = 0

    def __enter__(self) -> PicoPwmHid:
        """Return this client without opening hardware until the first operation."""
        return self

    def __exit__(self, exc_type: type[BaseException] | None, exc: BaseException | None,
                 traceback: TracebackType | None) -> None:
        """Close the handle without suppressing exceptions or stopping outputs."""
        self.close()

    def close(self) -> None:
        with self._lock:
            device, self._device = self._device, None
            self._identity = self._configuration = None
            self._pins = None
            if device is not None:
                try:
                    device.close()
                except OSError:
                    logging.getLogger(__name__).warning("Failed to close HID device", exc_info=True)

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

    def _ensure_identity(self) -> p.Identity:
        if self._identity is None:
            identity = p.decode_identity(self._read(p.IDENTITY_REPORT, p.IDENTITY_SIZE))
            if identity["can_read_memory"]:
                identity.update(p.decode_memory(self._read(p.MEMORY_REPORT, p.MEMORY_SIZE)))
            self._identity = identity
        return self._identity

    def _command_identity(self) -> p.Identity:
        """Read command prerequisites without leaking transport or parser exceptions."""
        try:
            return self._ensure_identity()
        except (OSError, p.ProtocolError, RuntimeError) as error:
            self.close()
            raise DeviceError(f"Unable to read device identity: {error}") from error

    def status(self) -> DeviceStatus:
        """Return an independent snapshot; connection problems are reported, not raised."""
        with self._lock:
            state: DeviceStatus = {
                "connected": False, "identity": None, "identity_error": None, "configuration": None,
                "configuration_error": None, "channels": _blank_channels(),
                "bank_errors": [None] * p.BANK_COUNT, "error": None,
                "runtime": None, "runtime_error": None,
                "pins": None, "pins_error": None,
            }
            try:
                identity = self._ensure_identity()
                state["identity"] = identity
                state["connected"] = True
                if identity["can_read_pins"]:
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
                if identity["can_read_runtime"]:
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
                state.update(connected=False, identity=None, pins=None,
                             identity_error=str(error), error=str(error))
            except (OSError, RuntimeError) as error:
                self.close()
                state.update(connected=False, identity=None, pins=None, error=str(error))
            return deepcopy(state)

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
                    raise DeviceError(f"command failed: {result['status_name']}", status=result["status"])
                return
            if self._monotonic() >= deadline:
                raise DeviceError("timed out awaiting the result; outcome is unconfirmed")
            self._sleep(POLL_S)

    def _run(self, operation: int, configuration: bool, **fields: int) -> DeviceStatus:
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

    def set_channel(self, channel: int, freq_hz: int, duty: int) -> DeviceStatus:
        _integer(channel, "channel", 0, p.CHANNEL_COUNT - 1)
        _integer(freq_hz, "freq_hz", 0, 0xFFFFFFFF)
        _integer(duty, "duty", 0, 100)
        with self._lock:
            try:
                return self._run(p.OP_SET_CHANNEL, False, channel=channel, arg0=freq_hz, arg1=duty)
            except DeviceError as error:
                if error.status != p.STATUS_INVALID:
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
                    f"command failed: INVALID (requested {freq_hz} Hz / {duty}%, readback {readback})",
                    status=error.status) from error

    def stop(self) -> DeviceStatus:
        return self._run(p.OP_STOP, False)

    def set_bank(self, bank: int, backend: int, role: int) -> DeviceStatus:
        _integer(bank, "bank", 0, 2)
        _integer(backend, "backend", 0, 2)
        _integer(role, "role", 0, 1)
        return self._run(p.OP_SET_BANK, True, channel=bank, arg0=backend | role << 8)

    def set_i2c_address(self, address: int) -> DeviceStatus:
        _integer(address, "address", 0, 127)
        return self._run(p.OP_SET_I2C_ADDRESS, True, arg0=address)

    def set_clock_profile(self, profile: int, acknowledge_overclock: bool) -> DeviceStatus:
        _integer(profile, "profile", 0, 4)
        _boolean(acknowledge_overclock, "acknowledge_overclock")
        if profile > 1 and not acknowledge_overclock:
            raise ValueError("acknowledge the overclock first")
        with self._lock:
            if not self._command_identity()["clock_profile_mask"] & 1 << profile:
                raise ValueError("choose a clock profile the firmware advertises")
            return self._run(p.OP_SET_CLOCK_PROFILE, True, arg0=profile,
                             flags=p.FLAG_ACK_OVERCLOCK if profile > 1 else 0)

    def save_configuration(self, acknowledge_overclock: bool) -> DeviceStatus:
        _boolean(acknowledge_overclock, "acknowledge_overclock")
        with self._lock:
            state = self.status()
            if state["configuration"] is None:
                raise DeviceError("read the configuration first")
            overclock = state["configuration"]["clock_profile"] > 1
            if overclock and not acknowledge_overclock:
                raise ValueError("acknowledge the overclock first")
            return self._run(p.OP_SAVE_CONFIG, True,
                             flags=p.FLAG_CONFIRM_SAVE | (p.FLAG_ACK_OVERCLOCK if overclock else 0))

    def set_led(self, on: bool) -> DeviceStatus:
        _boolean(on, "on")
        return self._run(p.OP_SET_LED, False, arg0=int(on))

    def reboot(self) -> RebootResult:
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
                        acknowledge_overclock: bool) -> RebootResult:
        """Validate the entire draft before submitting targets and one confirmed save/reboot."""
        _integer(profile, "profile", 0, 4)
        _integer(address, "address", 8, 119)
        _boolean(acknowledge_overclock, "acknowledge_overclock")
        if not isinstance(roles, list) or len(roles) != 3:
            raise ValueError("expected three bank roles")
        roles = roles.copy()
        for role in roles:
            _integer(role, "role", 0, 1)
        if profile > 1 and not acknowledge_overclock:
            raise ValueError("acknowledge the overclock first")
        with self._lock:
            identity = self._command_identity()
            if not identity["can_save_reboot"] or not identity["can_set_clock"]:
                raise DeviceError("update firmware to enable Save and Reboot")
            if not 0 <= profile <= 4 or not identity["clock_profile_mask"] & 1 << profile:
                raise ValueError("choose a clock profile the firmware advertises")
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
                raise DeviceError(
                    f"Save and Reboot did not complete; target settings may have changed: {error}",
                    status=error.status if isinstance(error, DeviceError) else None) from error
            return {"rebooting": True}
