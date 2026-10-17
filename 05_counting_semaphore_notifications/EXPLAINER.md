# Line-by-Line Explainer: Counting Semaphore Notifications

This demo builds on `04_binary_semaphore_event_coalescing`. Sensor produces three samples while Control sleeps, but the semaphore's maximum count is now three instead of one. Control can consume three pending notification tokens. The RPM values are still stored separately in one shared variable protected by a mutex.

## Headers and timing constants

```c
#include <zephyr/kernel.h>
```

Includes Zephyr kernel APIs used for threads, mutexes, semaphores, and sleeping.

```c
#include <zephyr/sys/printk.h>
```

Includes `printk()` for console output.

```c
#define STACK_SIZE              1024
```

Sets the stack allocation for each thread to 1024 bytes.

```c
#define SENSOR_INTERVAL_MS      1000
```

Sets the sensor's sleep interval to at least 1000 ms between measurements.

```c
#define CONTROL_START_DELAY_MS  3500
```

Makes Control wait at least 3500 ms before it checks the semaphore. Sensor's three measurements are produced at roughly 1, 2, and 3 seconds, so all three notifications should be pending before Control starts taking them.

## Shared value, mutex, and semaphore

```c
static int measured_rpm = 0;
```

Creates one shared RPM variable, initially zero. Each sample overwrites the previous value, so after the third sample this variable contains 1250 RPM. It does not keep a history of samples.

```c
K_MUTEX_DEFINE(motor_lock);
```

Defines and initializes a mutex that protects `measured_rpm`. Sensor locks it while writing, and Control locks it while reading. The mutex protects the value; it does not count sensor events.

```c
K_SEM_DEFINE(sample_ready, 0, 3);
```

Defines a counting semaphore named `sample_ready`. Its initial count is zero and its maximum count is three. Each give can add one pending token until the count reaches three. Unlike the previous binary semaphore (maximum count one), this semaphore can remember three pending notifications.

The semaphore stores tokens, not data. A count of three means three notifications are pending; it does not mean three RPM values are stored.

## Sensor thread

```c
static void sensor_thread(void *a, void *b, void *c)
```

Declares the entry function Zephyr runs for the Sensor thread. Thread entry functions accept three pointer arguments.

```c
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
```

Marks the arguments as intentionally unused to avoid compiler warnings.

```c
    for (int sample = 1; sample <= 3; sample++) {
```

Runs the loop three times, producing sample numbers 1, 2, and 3. The sensor thread exits after the third iteration.

```c
        k_msleep(SENSOR_INTERVAL_MS);
```

Sleeps for at least one second before producing the next sample, yielding the CPU while asleep.

```c
        int rpm = 1100 + 50 * sample;
```

Calculates simulated sensor data: 1150 RPM for sample 1, 1200 RPM for sample 2, and 1250 RPM for sample 3.

```c
        k_mutex_lock(&motor_lock, K_FOREVER);
        measured_rpm = rpm;
        k_mutex_unlock(&motor_lock);
```

Locks the shared variable, writes the new sample, then unlocks. `K_FOREVER` means the thread waits as long as necessary to acquire the mutex. Control uses the same mutex when reading.

```c
        k_sem_give(&sample_ready);
```

Adds one token to the semaphore after publishing the sample. Since Control is still sleeping, the first give raises the count to one, the second to two, and the third to three.

```c
        printk("[SENSOR] Sample %d, RPM=%d, sem count=%u\n",
               sample, rpm, k_sem_count_get(&sample_ready));
```

Prints the sample number, RPM, and semaphore count. Expected counts are 1, 2, and 3. `k_sem_count_get()` reports pending tokens; it does not report how many RPM values are stored.

```c
    }
}
```

Closes the three-iteration loop and the Sensor function.

## Control thread

```c
static void control_thread(void *a, void *b, void *c)
```

Declares the entry function for Control.

```c
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
```

Marks the required thread arguments as unused.

```c
    k_msleep(CONTROL_START_DELAY_MS);
```

Sleeps at least 3500 ms. During this delay, Sensor produces all three measurements and gives three semaphore tokens.

```c
    printk("[CONTROL] Count before takes = %u\n",
           k_sem_count_get(&sample_ready));
```

Prints the number of tokens waiting before Control begins. Expected count: three.

```c
    for (int i = 1; i <= 3; i++) {
```

Starts a loop that tries to take three tokens, one at a time.

```c
        int rc = k_sem_take(&sample_ready, K_NO_WAIT);
```

Tries to take a token without blocking. `K_NO_WAIT` means return immediately if no token is available. With three tokens pending, each of the three calls should succeed.

```c
        if (rc == 0) {
```

Checks whether the take succeeded. A zero return value means one token was consumed.

```c
            printk("[CONTROL] Take %d succeeded; count=%u\n",
                   i, k_sem_count_get(&sample_ready));
```

Prints the take number and remaining count. Expected counts after the first, second, and third takes are 2, 1, and 0.

```c
        } else {
            printk("[CONTROL] Take %d failed: no token\n", i);
        }
```

Prints an error message if a token was unavailable. With the intended timing, this branch should not run during the first three takes.

```c
    }
```

Closes the loop after three take attempts.

```c
    int rc = k_sem_take(&sample_ready, K_NO_WAIT);
```

Attempts a fourth take without waiting. Since the three pending tokens have already been consumed, this call should fail immediately with a nonzero result.

```c
    printk("[CONTROL] Fourth take: %s\n",
           rc == 0 ? "unexpected success" : "no token available");
```

Prints whether the fourth take unexpectedly succeeded or correctly found no token. The conditional expression selects the message based on the result.

```c
    k_mutex_lock(&motor_lock, K_FOREVER);
    int latest_rpm = measured_rpm;
    k_mutex_unlock(&motor_lock);
```

Locks the shared measurement, copies it into the local variable `latest_rpm`, and unlocks. The three semaphore tokens did not preserve the three samples. Since Sensor wrote each sample into the same variable, this read gets the latest value: 1250 RPM.

```c
    printk("[CONTROL] Latest RPM in memory = %d\n", latest_rpm);
```

Prints the copied latest value. Expected result: 1250.

```c
    printk("[CONTROL] Three tokens did NOT store three RPM values.\n");
```

States the lesson directly: a semaphore can count notifications, but it is not a data buffer.

```c
}
```

Closes the Control entry function. It returns after the demonstration finishes.

## Thread definitions

```c
K_THREAD_DEFINE(control_tid, STACK_SIZE, control_thread,
                NULL, NULL, NULL, 2, 0, 0);
```

Defines Control with a 1024-byte stack, three null arguments, priority 2, no special options, and no startup delay in the macro. The function itself sleeps for 3500 ms. In Zephyr's preemptible priorities, the lower numeric value is higher priority.

```c
K_THREAD_DEFINE(sensor_tid, STACK_SIZE, sensor_thread,
                NULL, NULL, NULL, 5, 0, 0);
```

Defines Sensor with the same stack size, priority 5, and no startup delay. Control has higher priority, but it is sleeping while Sensor produces its samples.

## Main function

```c
int main(void)
```

Declares the application main function.

```c
    printk("\n=== Counting Semaphore: Three Pending Notifications ===\n");
    printk("Sensor: three samples; Control starts after 3.5 s\n");
```

Prints a title and summarizes the sample and delay timing.

```c
    return 0;
```

Returns from `main`. The statically defined Control and Sensor threads continue independently.

## Expected output pattern

Sensor should report semaphore counts 1, 2, and 3. Control should report count 3 before taking, then the three successful takes should leave counts 2, 1, and 0. Its fourth take should find no token. The latest RPM should be 1250.

The comparison with the previous demo is:

| Demo | Semaphore maximum | Pending tokens after three gives | What happens to the RPM samples? |
| --- | ---: | ---: | --- |
| Binary semaphore | 1 | 1 | Only the latest value remains in the shared variable |
| Counting semaphore | 3 | 3 | Only the latest value remains in the shared variable |

If Control must process all three RPM values, store the values in a queue. A counting semaphore can indicate how many items are available, but the queue is what stores each item's data.
