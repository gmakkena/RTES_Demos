#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define STACK_SIZE              1024
#define SENSOR_INTERVAL_MS      1000
#define CONTROL_START_DELAY_MS  3500

/* The shared measurement is protected by motor_lock. */
static int measured_rpm = 0;
K_MUTEX_DEFINE(motor_lock);

/* A binary semaphore stores at most one pending notification. */
K_SEM_DEFINE(sample_ready, 0, 1);

/* Sensor produces three samples before Control starts taking signals. */
static void sensor_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	for (int sample = 1; sample <= 3; sample++) {
		k_msleep(SENSOR_INTERVAL_MS);

		int rpm = 1100 + 50 * sample;

		k_mutex_lock(&motor_lock, K_FOREVER);
		measured_rpm = rpm;
		k_mutex_unlock(&motor_lock);

		/* Control is still sleeping, so these notifications accumulate only to one. */
		k_sem_give(&sample_ready);
		printk("[SENSOR] Sample %d, RPM=%d, sem count=%u\n",
		       sample, rpm, k_sem_count_get(&sample_ready));
	}
}

/* Control waits until after all three samples have been produced. */
static void control_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	k_msleep(CONTROL_START_DELAY_MS);

	printk("[CONTROL] Starting after three sensor samples\n");
	printk("[CONTROL] Count before take = %u\n",
	       k_sem_count_get(&sample_ready));

	/* One notification is pending, so this call returns immediately. */
	int result = k_sem_take(&sample_ready, K_FOREVER);
	if (result == 0) {
		k_mutex_lock(&motor_lock, K_FOREVER);
		int rpm = measured_rpm;
		k_mutex_unlock(&motor_lock);

		printk("[CONTROL] Took ONE notification; latest RPM=%d\n", rpm);
		printk("[CONTROL] Count after take = %u\n",
		       k_sem_count_get(&sample_ready));
	}

	/* Probe once more without waiting. It fails: no event is pending. */
	result = k_sem_take(&sample_ready, K_NO_WAIT);
	if (result != 0) {
		printk("[CONTROL] Second take: no pending notification\n");
	} else {
		printk("[CONTROL] Second take unexpectedly succeeded\n");
	}

	printk("[CONTROL] Demonstration complete\n");
}

K_THREAD_DEFINE(control_tid, STACK_SIZE, control_thread,
		NULL, NULL, NULL, 2, 0, 0);

K_THREAD_DEFINE(sensor_tid, STACK_SIZE, sensor_thread,
		NULL, NULL, NULL, 5, 0, 0);

int main(void)
{
	printk("\n=== Binary Semaphore: Count and Lost Notifications ===\n");
	printk("Sensor: three samples at 1 s intervals\n");
	printk("Control: first take after 3.5 s\n");

	return 0;
}
