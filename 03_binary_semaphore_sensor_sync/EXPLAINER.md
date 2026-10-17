# Line-by-Line Explainer: Binary Semaphore Sensor Sync

This guide follows `src/main.c` from top to bottom. The demo has two modes, selected at compile time:

- `USE_SEMAPHORE 0`: Control checks the RPM every 250 ms, even if Sensor has not produced a measurement.
- `USE_SEMAPHORE 1`: Control blocks until Sensor signals that a measurement is ready.

Change the macro and rebuild to switch modes.

## 1. Header files

```c
#include <zephyr/kernel.h>
```

Includes Zephyr kernel declarations. This program uses threads, sleeping, and a semaphore.

```c
```

The atomic API is not needed in this version; the shared RPM is protected by `motor_lock` instead.

```c
#include <zephyr/sys/printk.h>
```

Includes Zephyr's `printk()` function, used for output on the console.

## 2. Constants and shared synchronization objects

```c
#define STACK_SIZE          1024
```

Sets the stack allocation to 1024 bytes for each thread.

```c
#define USE_SEMAPHORE       0
```

Selects which control behavior is compiled. Zero selects periodic polling. Change it to one to enable semaphore waiting, then rebuild. `#if` below is a preprocessor condition, so this is not a runtime switch.

```c
#define SENSOR_INTERVAL_MS  1000
#define CONTROL_PERIOD_MS   250
```

Sets the sensor's production interval to 1000 ms and the polling control period to 250 ms. The control period is used only when `USE_SEMAPHORE` is zero.

```c
/* No valid sensor measurement initially. */
static int measured_rpm;
```

Creates the shared RPM value, initialized to zero because it has static storage duration. In this example zero means “no sample has arrived yet.” `static` restricts the symbol to this source file. This ordinary integer must be accessed under the mutex by both threads.

```c
/* Protects reads and writes of measured_rpm. */
K_MUTEX_DEFINE(motor_lock);
```

Defines and initializes a mutex for the shared RPM. Sensor owns the mutex while writing the new value; Control owns it while copying the value. The mutex protects the data access. `sample_ready`, separately, tells Control when a measurement has been produced.

### Is a mutex the same as a semaphore?

No. A mutex provides ownership and exclusive access: a thread locks it, accesses shared data, then unlocks it. A semaphore has no ownership; Sensor can give a token to notify Control, which takes that token and proceeds.

In this demo the two primitives have different jobs:

| Object | Job in this demo |
| --- | --- |
| `motor_lock` | Prevents Sensor from writing `measured_rpm` while Control is copying it, and vice versa |
| `sample_ready` | Lets Control block until Sensor has published a measurement |

The mutex alone would still allow Control to read the initial zero safely. The semaphore alone would notify Control but would not protect the shared RPM if the read and write could overlap.

```c
/* Binary semaphore: initial=0, maximum=1 */
K_SEM_DEFINE(sample_ready, 0, 1);
```

Defines and initializes `sample_ready` with a count of zero and a maximum count of one. Since the count starts at zero, a thread taking it must wait until another thread gives it. The count limit of one makes it binary.

## 3. Sensor thread

```c
static void sensor_thread(void *a, void *b, void *c)
```

Declares the entry function Zephyr runs for the Sensor thread. Zephyr thread entry functions accept three pointer arguments.

```c
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
```

Marks the required arguments as intentionally unused so the compiler does not warn about them.

```c
    int sample_no = 0;
```

Creates a local counter for the number of samples this thread has generated. It starts at zero.

```c
    while (1) {
```

Begins an infinite loop. Sensor continues producing measurements until the system stops.

```c
        k_msleep(SENSOR_INTERVAL_MS);
```

Sleeps for at least 1000 ms before producing the next measurement. The thread gives up the CPU while asleep.

```c
        sample_no++;
```

Increments the sample counter. The first generated sample is numbered 1.

```c
        int rpm = 1100 + 50 * sample_no;
```

Calculates a simulated RPM value. The first sample is 1150 RPM, the next is 1200 RPM, then 1250 RPM, and so on. This is simulated sensor data; no hardware sensor is read.

```c
k_mutex_lock(&motor_lock, K_FOREVER);
measured_rpm = rpm;
k_mutex_unlock(&motor_lock);
```

Locks `motor_lock`, writes the new RPM, and unlocks the mutex. This prevents Control from copying the integer at the same time Sensor changes it. The semaphore notification below is what tells Control that a new measurement is ready.

```c
        printk("[SENSOR] New measurement %d: rpm=%d\n",
               sample_no, rpm);
```

Prints the sample number and value. The two `%d` placeholders are filled by `sample_no` and `rpm`.

```c
#if USE_SEMAPHORE
```

Begins a compile-time conditional block. Its contents are included only when `USE_SEMAPHORE` is nonzero.

```c
        printk("[SENSOR] Giving semaphore\n");
```

Logs that Sensor is about to notify Control. The log occurs before the actual give operation.

```c
        k_sem_give(&sample_ready);
```

Gives one token to the semaphore. If Control is blocked in `k_sem_take()`, this makes Control ready to run. A semaphore has no owner, so Sensor can give it even though Control is the thread that takes it.

```c
#endif
```

Ends the conditional block. In polling mode there is no semaphore notification.

```c
    }
}
```

Closes the infinite loop and the Sensor entry function. Since the loop is infinite, the function normally does not return.

## 4. Control thread

```c
static void control_thread(void *a, void *b, void *c)
```

Declares the entry function for the Control thread.

```c
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
```

Marks the required thread arguments as unused.

```c
    while (1) {
```

Starts Control's infinite processing loop.

```c
#if USE_SEMAPHORE
```

Selects the event-driven branch when the application is built with `USE_SEMAPHORE` set to one.

```c
        printk("[CONTROL] Waiting for measurement...\n");
```

Prints immediately before Control waits. In semaphore mode this message appears before the first sensor measurement, then Control blocks.

```c
        k_sem_take(&sample_ready, K_FOREVER);
```

Attempts to take one semaphore token. The initial count is zero, so Control blocks here until Sensor calls `k_sem_give()`. `K_FOREVER` means there is no timeout. This prevents Control from processing the initial zero as if it were a new measurement.

```c
        printk("[CONTROL] Semaphore received\n");
```

Prints after the take succeeds, meaning a sensor-ready notification was available.

```c
#else
```

Begins the alternative compile-time branch, used when `USE_SEMAPHORE` is zero.

```c
        /* Control runs whether sensor is ready or not */
        k_msleep(CONTROL_PERIOD_MS);
```

Sleeps for at least 250 ms, then continues to read the current RPM. This is polling: Control checks periodically whether a nonzero value has appeared, but it does not wait for a sensor notification. Before the first sensor sample, it can report the zero value as a false stall alarm. After a sample arrives, it can process the same value multiple times before the next sample.

```c
#endif
```

Ends the compile-time choice. Whichever branch is compiled, Control next reads the RPM.

```c
int rpm;

k_mutex_lock(&motor_lock, K_FOREVER);
rpm = measured_rpm;
k_mutex_unlock(&motor_lock);
```

Atomically copies the current shared RPM into a local variable. The local `rpm` is used for the rest of this loop iteration.

```c
        if (rpm == 0) {
```

Checks whether the shared value is still the initial “no measurement yet” value.

```c
            printk("[CONTROL] rpm=0 -> FALSE STALL ALARM!\n");
```

Prints the simulated false alarm. It is “false” because this demo uses zero to mean no sample has arrived, not because the motor was measured and found stopped.

```c
        } else {
```

Selects the normal processing path when the RPM is nonzero.

```c
            printk("[CONTROL] Processing RPM=%d\n", rpm);
```

Prints the RPM value Control is processing.

```c
        }
    }
}
```

Closes the conditional, the processing loop, and the Control entry function. The loop means Control keeps polling or waiting and processing for the lifetime of the app.

## 5. Thread creation

```c
K_THREAD_DEFINE(control_tid, STACK_SIZE,
                control_thread,
                NULL, NULL, NULL, 2, 0, 0);
```

Defines the Control thread statically. It runs `control_thread`, has a 1024-byte stack, receives three null arguments, has priority 2, uses no special options, and has no startup delay specified. In Zephyr preemptible priorities, the smaller number is higher priority.

```c
K_THREAD_DEFINE(sensor_tid, STACK_SIZE,
                sensor_thread,
                NULL, NULL, NULL, 5, 0, 0);
```

Defines the Sensor thread with the same stack size and no startup delay, at priority 5. Control (priority 2) outranks Sensor (priority 5). When Control blocks waiting on the semaphore, Sensor can run and produce the measurement that wakes it.

## 6. Main function

```c
int main(void)
```

Declares the application's main function.

```c
    printk("\n=== Binary Semaphore Demo ===\n");
```

Prints a title. `\n` inserts a newline before and after the title.

```c
    printk("USE_SEMAPHORE=%d\n", USE_SEMAPHORE);
```

Prints the compiled mode, which helps confirm whether the polling or semaphore version was built.

```c
    return 0;
```

Returns from `main`. The statically defined Sensor and Control threads continue running independently.

## What students should observe

### Build with `USE_SEMAPHORE 0`

Control wakes every 250 ms. Before Sensor's first 1000 ms delay completes, Control reads zero and prints the false stall alarm. When Sensor eventually writes a nonzero RPM, Control may print that same value several times before Sensor updates it again.

### Build with `USE_SEMAPHORE 1`

Control prints that it is waiting, then blocks. Sensor waits 1000 ms, writes the first RPM, and gives the semaphore. Control wakes and processes the measurement. It then waits again for the next sensor notification instead of polling every 250 ms.

The two objects have different jobs: `measured_rpm` stores the data, while `sample_ready` signals that data has been published. The semaphore is not a mutex and does not protect the RPM variable; the atomic operations make this individual integer safe to read and write.

Because `sample_ready` has a maximum count of one, multiple notifications can coalesce if Sensor gives repeatedly before Control takes the token. This example uses a shared variable for the latest RPM. If the application must process every sample, store samples in a queue instead of relying only on a binary semaphore.

## Why this version uses a mutex instead of an atomic

The shared value is now an ordinary `int`, so every access to it must follow the same locking rule:

```c
k_mutex_lock(&motor_lock, K_FOREVER);
measured_rpm = rpm;       /* Sensor writes while holding the mutex. */
k_mutex_unlock(&motor_lock);

k_mutex_lock(&motor_lock, K_FOREVER);
int rpm = measured_rpm;   /* Control reads while holding the mutex. */
k_mutex_unlock(&motor_lock);
```

The mutex has an owner and protects the read or write section. It does not tell Control that a new sample exists; `sample_ready` does that. In polling mode, the mutex still prevents a read from overlapping the write, but Control may safely read the initial zero or repeatedly read an older sample. In semaphore mode, Control waits for a notification first, then uses the mutex to copy the shared value safely.

This is intentionally a simple one-value example. A mutex is especially useful when the protected state contains multiple related fields that must be read or updated together.
