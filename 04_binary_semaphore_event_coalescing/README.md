# Binary Semaphore Event Coalescing

This demo shows the count limit of a binary semaphore. Sensor produces three samples while Control is sleeping. The first `k_sem_give()` makes the semaphore count one; later gives cannot raise it above its limit of one.

When Control wakes, it takes the one pending notification and reads the latest RPM value, 1250. The two earlier RPM values have been overwritten in the shared variable, and their notifications did not accumulate. The follow-up `K_NO_WAIT` take shows that no second notification remains.

This is event coalescing, not a queue of three measurements. Use a queue when Control must receive and process every sample.
