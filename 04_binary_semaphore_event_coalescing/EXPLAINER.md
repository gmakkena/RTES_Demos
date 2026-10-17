# Line-by-Line Explainer: Binary Semaphore Event Coalescing

This demo explores what a binary semaphore remembers when a producer signals several times before a consumer takes the semaphore. Sensor produces three measurements while Control sleeps. The semaphore count has a maximum of one, while the shared RPM variable is overwritten with each new measurement.

## Headers and timing constants

```c
#include <zephyr/kernel.h>
```

Includes Zephyr kernel declarations for threads, mutexes, semaphores, sleeping, and related APIs.

```c
#include <zephyr/sys/printk.h>
```

Includes `printk()`, which this demo uses to print its progress to the console.

```c
#define STACK_SIZE              1024
```

Sets the stack allocation for each thread to 1024 bytes.

```c
#define SENSOR_INTERVAL_MS      1000
```

Sets the sensor's sleep interval to at least 1000 ms between samples.

```c
#define CONTROL_START_DELAY_MS  3500
```

Makes Control sleep for at least 3500 ms before it first takes the semaphore. Sensor's three samples are produced at roughly 1, 2, and 3 seconds, so they are all produced before Control takes the semaphore.

## Shared measurement and synchronization objects

```c
static int measured_rpm = 0;
```

Defines the shared RPM value, initially zero. Each sensor sample overwrites this same variable. `static` limits access to this source file.

```c
K_MUTEX_DEFINE(motor_lock);
```

Defines and initializes the mutex that protects `measured_rpm`. Sensor locks it while writing; Control locks it while copying the value. The mutex protects the data access, but it does not signal that a new sample exists.

```c
K_SEM_DEFINE(sample_ready, 0, 1);
```

Defines a semaphore with an initial count of zero and a maximum count of one. Zero means no notification is pending. Once given, its count becomes one. Further gives while it is already one do not raise the count above one.

The semaphore count records whether there is **at least one pending notification**. It is not a count of how many sensor samples were produced, and it does not store the RPM values.

## Sensor thread

```c
static void sensor_thread(void *a, void *b, void *c)
```

Declares the entry function Zephyr runs for the Sensor thread. Zephyr thread entry functions accept three pointer arguments.

```c
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
```

Marks the required thread arguments as unused, avoiding compiler warnings.

```c
    for (int sample = 1; sample <= 3; sample++) {
```

Starts a loop that produces exactly three samples, numbered 1, 2, and 3. Unlike a continuous sensor thread, this one exits after the third sample so the demo has a clear endpoint.

```c
        k_msleep(SENSOR_INTERVAL_MS);
```

Sleeps for at least one second before producing each sample. The thread yields the CPU during this wait.

```c
        int rpm = 1100 + 50 * sample;
```

Calculates simulated sensor data: sample 1 is 1150 RPM, sample 2 is 1200 RPM, and sample 3 is 1250 RPM. No physical sensor is read.

```c
        k_mutex_lock(&motor_lock, K_FOREVER);
        measured_rpm = rpm;
        k_mutex_unlock(&motor_lock);
```

Locks the shared data, writes the new RPM, then unlocks. `K_FOREVER` means wait without timing out if another thread owns the mutex. Control uses the same mutex when copying the value, so it cannot read during the write.

```c
        /* No other thread is taking while these first three are given. */
        k_sem_give(&sample_ready);
```

Gives one notification after publishing the sample. Control remains asleep until 3.5 seconds, so it does not take the semaphore between the sensor's three gives. The first give changes the count from zero to one. The next two gives happen while the count is already one, so the count stays one.

These extra gives are often described informally as “lost notifications.” More precisely, the binary semaphore coalesces them into one pending notification. The sample values themselves are overwritten in `measured_rpm`; they are not queued for later processing.

```c
        printk("[SENSOR] Sample %d, RPM=%d, sem count=%u\n",
               sample, rpm, k_sem_count_get(&sample_ready));
```

Prints the sample number, its RPM, and the current semaphore count. Since Control has not taken the semaphore, the count should be one on each of the three lines.

```c
    }
}
```

Closes the three-sample loop and the Sensor thread function. The thread exits after the loop finishes.

## Control thread

```c
static void control_thread(void *a, void *b, void *c)
```

Declares the Control thread entry function.

```c
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
```

Marks the thread arguments as intentionally unused.

```c
    k_msleep(CONTROL_START_DELAY_MS);
```

Sleeps for at least 3500 ms. While Control sleeps, Sensor produces its three samples and gives the semaphore three times.

```c
    printk("[CONTROL] Starting after three sensor samples\n");
```

Prints a message when Control resumes. This statement follows the configured delay; it assumes the sensor's three one-second intervals have elapsed.

```c
    printk("[CONTROL] Count before take = %u\n",
           k_sem_count_get(&sample_ready));
```

Reads and prints the semaphore's current count before taking it. Expected count: one, even though Sensor gave it three times.

```c
    int result = k_sem_take(&sample_ready, K_FOREVER);
```

Attempts to consume one semaphore token. Since one token is pending, the call returns immediately with result zero. If there were no token, `K_FOREVER` would make Control block until one was given.

```c
    if (result == 0) {
```

Checks whether the semaphore was successfully taken. A successful take consumes the pending token, reducing the count from one to zero.

```c
        k_mutex_lock(&motor_lock, K_FOREVER);
        int rpm = measured_rpm;
        k_mutex_unlock(&motor_lock);
```

Locks the measurement, copies it into a local variable, then unlocks. Sensor has already written all three samples, so the shared variable contains the latest one: 1250 RPM. The mutex makes the copy safe; the semaphore did not carry the value.

```c
        printk("[CONTROL] Took ONE notification; latest RPM=%d\n", rpm);
```

Prints that one pending notification was consumed and shows the latest RPM. The output should report 1250, not all three readings, because the shared variable stores only the most recent value.

```c
        printk("[CONTROL] Count after take = %u\n",
               k_sem_count_get(&sample_ready));
```

Prints the count after the take. Expected count: zero.

```c
    }
```

Closes the success block. With `K_FOREVER`, success is the expected outcome here; the check makes the subsequent read conditional on a successful take.

```c
    result = k_sem_take(&sample_ready, K_NO_WAIT);
```

Tries to take a second token but does not wait. Since the earlier take consumed the only pending token, this call should return immediately with a nonzero error result.

```c
    if (result != 0) {
        printk("[CONTROL] Second take: no pending notification\n");
    } else {
        printk("[CONTROL] Second take unexpectedly succeeded\n");
    }
```

Prints the expected outcome when no token is available. The `else` message helps expose an unexpected extra pending token.

```c
    printk("[CONTROL] Demonstration complete\n");
```

Prints the final message. Control then reaches the end of its entry function and exits.

## Thread definitions

```c
K_THREAD_DEFINE(control_tid, STACK_SIZE, control_thread,
                NULL, NULL, NULL, 2, 0, 0);
```

Defines the Control thread statically. It has a 1024-byte stack, receives three null arguments, uses priority 2, has no special options, and has no startup delay in this macro call. It then performs its own 3500 ms sleep.

```c
K_THREAD_DEFINE(sensor_tid, STACK_SIZE, sensor_thread,
                NULL, NULL, NULL, 5, 0, 0);
```

Defines the Sensor thread with the same stack size, priority 5, and no startup delay. Zephyr's smaller preemptible priority number is the higher priority, so Control's priority 2 is higher than Sensor's 5. Control is sleeping during sample production, allowing Sensor to run.

## Main function

```c
int main(void)
```

Declares the application's main function.

```c
    printk("\n=== Binary Semaphore: Count and Lost Notifications ===\n");
    printk("Sensor: three samples at 1 s intervals\n");
    printk("Control: first take after 3.5 s\n");
```

Prints the title and the scenario timing. These lines help identify the demo and interpret the thread output.

```c
    return 0;
```

Returns from `main`. The separately defined Sensor and Control threads continue running.

## Expected result

The three Sensor lines should each show semaphore count 1. Control should then show count 1 before its first take, process the latest RPM of 1250, show count 0 after the take, and report that its second nonblocking take found no pending notification.

The demonstration distinguishes three things:

1. **Sample count:** Sensor produced three measurements.
2. **Semaphore count:** only one notification is pending.
3. **Stored RPM:** only the latest measurement, 1250, remains in `measured_rpm`.

If Control must process every sample, a binary semaphore plus one shared “latest value” variable is not enough. Use a queue to store each sample, and use a notification mechanism to wake the consumer when the queue has data.
