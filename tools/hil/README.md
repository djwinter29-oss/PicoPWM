# PicoPWM HIL

A repository-local Python project for CDC loopback tests. Requires Python 3.10+ and the
[HIL fixture](../../docs/tests/hil-test-connections.md).

The [launcher](../host/run-hil.sh) creates an environment and installs this project automatically:

```sh
tools/host/run-hil.sh --port /dev/ttyACM0 --board pico \
    --firmware build/pico/pico_pwm.uf2 --confirm-fixture
```

Or install in your own environment, from the repository root:

```sh
python -m pip install --editable tools/hil
picopwm-hil --help
```

Use an editable install because the tool writes its fixed record relative to this repository's source.
Dependencies and the `picopwm-hil` command are defined in [pyproject.toml](pyproject.toml).
Runner source lives in [src/](src/) and hardware-free regression tests in [tests/](tests/).
See the [test plan](../../docs/tests/hil-test-plan.md) for scope, limits and record handling.

Run hardware-free regression tests:

```sh
python -m unittest discover -s tools/hil/tests -p 'test_*.py'
```
