# PicoPWM host tools

This package provides a transport-neutral Python client and a Flask management
interface for the firmware's 24 logical channels. It supports both firmware
control paths:

- USB CDC at 115200 baud through `pyserial`
- I2C bus 1, default address `0x40`, through `smbus2`

## Install

From `host/python`:

```sh
python -m pip install -e '.[all]'
```

Install only the path you need with `.[cdc]` or `.[i2c]`.

## Run the web interface

USB CDC (Linux default `/dev/ttyACM0`):

```sh
PICOPWM_TRANSPORT=cdc PICOPWM_CDC_PORT=/dev/ttyACM0 picopwm-web
```

I2C:

```sh
PICOPWM_TRANSPORT=i2c PICOPWM_I2C_BUS=1 PICOPWM_I2C_ADDRESS=0x40 picopwm-web
```

Open <http://127.0.0.1:5000>. The dashboard reads all 24 channels, applies
frequency and duty updates, and exposes the firmware stop-all operation.

The Flask app factory accepts an injected `PicoPWMClient`, which keeps tests and
lab integrations independent of physical hardware:

```python
from picopwm.app import create_app
from picopwm.client import PicoPWMClient

app = create_app(PicoPWMClient(my_backend))
```

## Shared Web UI

The shared frontend source is in `host/web/`: `index.html`, `css/style.css`,
and `js/app.js`. Flask serves these files from the repository checkout, and the
.NET host links them into its static web root. Keep UI changes in this source
directory rather than copying them into either host project. The documented
editable install keeps this shared directory available to the Python app.

Both hosts expose the same JSON API:

- `GET /api/channels`
- `POST /api/channels/{channel}` with `{"frequencyHz": 1000, "dutyPercent": 50}`
- `POST /api/stop`

Run the focused tests from this directory with:

```sh
python -m pytest -q
```
