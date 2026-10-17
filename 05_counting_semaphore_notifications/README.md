# Counting Semaphore: Three Pending Notifications

This demo changes the previous binary semaphore's maximum count from one to three. Sensor produces three measurements while Control sleeps. Each `k_sem_give()` adds a token, so Control later finds three pending tokens and consumes them one at a time.

The semaphore counts notifications, not RPM values. `measured_rpm` is still one shared variable protected by `motor_lock`; after all three sensor updates, it contains only the latest sample, 1250 RPM. Use a queue if Control needs each sample's RPM value.
