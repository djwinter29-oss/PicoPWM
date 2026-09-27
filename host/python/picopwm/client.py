"""Transport-independent PicoPWM client API."""

from __future__ import annotations

from dataclasses import asdict, dataclass
from typing import Protocol


@dataclass(frozen=True)
class ChannelState:
    """Realized state published by the firmware for one logical channel."""

    channel: int
    frequency_hz: int
    duty_percent: int
    pulse_count: int

    @property
    def enabled(self) -> bool:
        return self.frequency_hz > 0

    def as_dict(self) -> dict[str, int | bool]:
        value = asdict(self)
        value["enabled"] = self.enabled
        return value


class PicoPWMBackend(Protocol):
    def channel_count(self) -> int: ...

    def get_channel(self, channel: int) -> ChannelState: ...

    def set_channel(self, channel: int, frequency_hz: int, duty_percent: int) -> ChannelState: ...

    def stop_all(self) -> None: ...

    def close(self) -> None: ...


class PicoPWMClient:
    """Small facade used by both CLI tooling and the Flask application."""

    def __init__(self, backend: PicoPWMBackend):
        self.backend = backend

    @property
    def channel_count(self) -> int:
        return self.backend.channel_count()

    def channels(self) -> list[ChannelState]:
        return [self.backend.get_channel(channel) for channel in range(self.channel_count)]

    def channel(self, channel: int) -> ChannelState:
        self._validate_channel(channel)
        return self.backend.get_channel(channel)

    def set_channel(self, channel: int, frequency_hz: int, duty_percent: int) -> ChannelState:
        self._validate_channel(channel)
        if frequency_hz < 0:
            raise ValueError("frequency must be zero or greater")
        if not 0 <= duty_percent <= 100:
            raise ValueError("duty must be between 0 and 100")
        return self.backend.set_channel(channel, frequency_hz, duty_percent)

    def stop_all(self) -> None:
        self.backend.stop_all()

    def close(self) -> None:
        self.backend.close()

    def _validate_channel(self, channel: int) -> None:
        if not 0 <= channel < self.channel_count:
            raise ValueError(f"channel must be between 0 and {self.channel_count - 1}")
