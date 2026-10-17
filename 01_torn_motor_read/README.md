# Demo 01: motor-state mutex

The control thread changes a motor command from `(1200 RPM, 40% PWM)` to
`(1800 RPM, 60% PWM)`. It deliberately pauses for 700 ms after changing only
the RPM. The diagnostics thread reads during that pause.

The single source file supports both classroom runs:

```c
#define USE_MUTEX 0  /* first run: controlled failure */
#define USE_MUTEX 1  /* second run: protected update */
```

## Build and flash

From the workspace root, using its Python virtual environment:

```powershell
.\.venv\Scripts\west.exe build -b black_f407ve `
  -s my_app\01_torn_motor_read -d build\01_torn_motor_read -p always
.\.venv\Scripts\west.exe flash -d build\01_torn_motor_read
```

Replace `black_f407ve` if the class uses a different board.

## Run 1: `USE_MUTEX 0`

The timeline is intentionally arranged so diagnostics runs during the 700 ms
gap:

```text
[CTRL] Target changed to 1800 RPM
[DIAG] Requesting motor data
[DIAG] RPM=1800 PWM=40 -> INCONSISTENT
[CTRL] PWM changed to 60%
```

The two integers are individually readable, but the pair violates the motor
state invariant. This is the race students should identify.

## Run 2: `USE_MUTEX 1`

The control thread holds `motor_lock` across the complete two-field update.
Diagnostics requests the data at the same time, then blocks in
`k_mutex_lock()` until the controller releases the mutex:

```text
[CTRL] Mutex acquired
[CTRL] Target changed to 1800 RPM
[DIAG] Requesting motor data
[CTRL] PWM changed to 60%
[CTRL] Mutex released
[DIAG] Mutex acquired
[DIAG] RPM=1800 PWM=60 -> CONSISTENT
```

## Discussion points

- The 700 ms sleeps make the bad interleaving visible and repeatable; they are
  demonstration timing, not a synchronization solution.
- The protected resource is the **relationship** between RPM and PWM, not just
  either integer by itself.
- Both the writer and the reader must use the same mutex.
- Keep the critical section limited to updating or copying shared state. In a
  real system, move slow logging and I/O outside it.
- `measured_rpm` is present to keep the structure realistic, but this first
  invariant deliberately concerns only `target_rpm` and `pwm_percent`.
- With `USE_MUTEX=0`, concurrent access to ordinary C objects is intentionally
  unsafe. This is failure-demonstration code, not production code.

Ask before the second run: **When diagnostics prints “Requesting,” what output
do you expect next?** The pause before “Mutex acquired” makes blocking visible.
