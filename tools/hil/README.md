# PicoPWM HIL

CDC loopback runner for the currently configured generator bank.

## Preflight

- Run from a repository checkout on the computer connected to the board.
- Obtain operator confirmation of the port, board, flashed artifact, fixture,
  and permission to drive outputs.
- Exactly one running bank must be a generator and the other two monitors.
  The runner never changes roles, saves configuration, or reboots.
- Use the [three-bank resistor fixture](../../docs/validation/hil/connections.md);
  all GPIOs are 3.3 V only. Close other serial terminals and dashboards.
- Preserve needed `latest.md` reports under dated filenames before starting;
  each run replaces it.

## Run

Requires Python 3.10+. The [launcher](../host/run-hil.sh) creates a venv and installs the runner:

```sh
tools/host/run-hil.sh --port /dev/ttyACM0 --board pico \
    --firmware build/pico/pico_pwm.uf2 --confirm-fixture
```

Or install in your own environment, from the repository root:

```sh
python -m pip install --editable tools/hil
picopwm-hil --help
```

The operator supplies board and artifact identity; the runner cannot verify
either. `--confirm-fixture` is mandatory because outputs are driven.
Completion stops generators but does not restore previous waveforms.

The subset checks all eight junctions at 100, 500, and 1000 Hz and 25%, 50%,
and 75% duty, simultaneous outputs, and stop readback. Defaults are 5%
frequency tolerance (minimum 1 Hz), 5 percentage points duty tolerance, and
3-second command and monitor timeouts. Choose limits before the run.

## Results and Cleanup

Started runs replace `docs/validation/hil/records/latest.md`, even on failure;
invalid CLI arguments leave it unchanged. Reports include metadata, results,
unexecuted scope, and CDC transcript. Nonzero exit or report-write failure
means failure. A pass covers this subset only. If cleanup cannot confirm
stop, disconnect board power.

## Windows and Custom Environments

The launcher is for Linux/macOS. On Windows or with a custom environment,
install and invoke the project directly:

```sh
python -m pip install --editable tools/hil
picopwm-hil --port COM5 --board pico --firmware build/pico/pico_pwm.uf2 --confirm-fixture
```

Use an editable install: records are written inside the checkout.
Dependencies and entry point: [pyproject.toml](pyproject.toml).

## Hardware-Free Checks

```sh
python -m unittest discover -s tools/hil/tests -p 'test_*.py'
```

The [full HIL plan](../../docs/validation/hil/test-plan.md) also covers
instrument accuracy, other transports, and the remaining qualification checks.
