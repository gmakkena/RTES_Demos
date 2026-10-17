# Binary Semaphore: Sensor and Control Synchronization

This Zephyr application contrasts periodic polling with waiting for a sensor-ready event.

## Run the two demonstrations

In `src/main.c`, build and run once with:

```c
#define USE_SEMAPHORE 0
```

Control wakes every 250 ms, including before the first sensor sample is available. The initial RPM is zero, so the demo reports a false stall alarm.

Then set the value to `1`, rebuild, and run again. Control blocks on `k_sem_take()` and continues only after Sensor publishes a measurement and gives the semaphore.

The semaphore signals that a sample is ready. A mutex protects the shared RPM while Sensor writes it and Control copies it; the semaphore itself does not carry or protect the RPM value.

The semaphore has a count limit of one. If Sensor signals several times before Control takes the semaphore, the notifications can coalesce. This demo uses the shared variable to hold the latest measurement; use a queue if every sample must be processed.
