# PicoPWM HIL

A repository-local Python runner for CDC loopback tests. It drives the
currently configured generator bank and does not change roles, save
configuration, or reboot. Use it only with the
[HIL fixture](../../docs/validation/hil/connections.md) and follow the
[test plan](../../docs/validation/hil/test-plan.md).

Requires Python 3.10+. The [launcher](../host/run-hil.sh) creates an
environment and installs this project automatically:

```sh
tools/host/run-hil.sh --port /dev/ttyACM0 --board pico \
    --firmware build/pico/pico_pwm.uf2 --confirm-fixture
```

Or install in your own environment, from the repository root:

```sh
python -m pip install --editable tools/hil
picopwm-hil --help
```

The artifact path and board model are supplied by the operator; the runner
cannot verify the flashed artifact's identity or detect the board model.
`--confirm-fixture` is required because the run drives outputs. The runner
stops generator channels on completion but does not restore previous waveforms.

See the [HIL test plan](../../docs/validation/hil/test-plan.md) for subset
coverage and test limits.

Each started run replaces `docs/validation/hil/records/latest.md`, including
failed runs. Invalid CLI arguments do not replace it. The report includes
metadata, results, unexecuted scope, and a CDC transcript. A passing exit
status applies only to this subset; nonzero exit or report-write failure is a
failure. If cleanup cannot confirm stop, disconnect board power. Preserve a
record under a dated filename before the next run if it must be retained.

The launcher is for Linux/macOS. On Windows or with a custom environment,
install and invoke the project directly:

```sh
python -m pip install --editable tools/hil
picopwm-hil --port COM5 --board pico --firmware build/pico/pico_pwm.uf2 --confirm-fixture
```

Use an editable install because the tool writes records under this repository.
Dependencies and the `picopwm-hil` command are defined in
[pyproject.toml](pyproject.toml). Hardware-free regression tests live in
[tests/](tests/).

Run hardware-free regression tests:

```sh
python -m unittest discover -s tools/hil/tests -p 'test_*.py'
```
