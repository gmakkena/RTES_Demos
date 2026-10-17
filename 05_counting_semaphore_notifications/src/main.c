#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define STACK_SIZE              1024
#define SENSOR_INTERVAL_MS      1000
#define CONTROL_START_DELAY_MS  3500

/* The latest motor speed still lives in ordinary shared memory. */
static int measured_rpm = 0;
K_MUTEX_DEFINE(motor_lock);

/* Changed from the previous demo: the maximum count is now 3. */
K_SEM_DEFINE(sample_ready, 0, 3);

/* Sensor thread: produces three measurements and notifications. */
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

		k_sem_give(&sample_ready);
		printk("[SENSOR] Sample %d, RPM=%d, sem count=%u\n",
		       sample, rpm, k_sem_count_get(&sample_ready));
	}
}

/* Control thread: deliberately starts after all three notifications. */
static void control_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	k_msleep(CONTROL_START_DELAY_MS);

	printk("[CONTROL] Count before takes = %u\n",
	       k_sem_count_get(&sample_ready));

	for (int i = 1; i <= 3; i++) {
		int rc = k_sem_take(&sample_ready, K_NO_WAIT);
		if (rc == 0) {
			printk("[CONTROL] Take %d succeeded; count=%u\n",
			       i, k_sem_count_get(&sample_ready));
		} else {
			printk("[CONTROL] Take %d failed: no token\n", i);
		}
	}

	int rc = k_sem_take(&sample_ready, K_NO_WAIT);
	printk("[CONTROL] Fourth take: %s\n",
	       rc == 0 ? "unexpected success" : "no token available");

	k_mutex_lock(&motor_lock, K_FOREVER);
	int latest_rpm = measured_rpm;
	k_mutex_unlock(&motor_lock);

	printk("[CONTROL] Latest RPM in memory = %d\n", latest_rpm);
	printk("[CONTROL] Three tokens did NOT store three RPM values.\n");
}

K_THREAD_DEFINE(control_tid, STACK_SIZE, control_thread,
		NULL, NULL, NULL, 2, 0, 0);

K_THREAD_DEFINE(sensor_tid, STACK_SIZE, sensor_thread,
		NULL, NULL, NULL, 5, 0, 0);

int main(void)
{
	printk("\n=== Counting Semaphore: Three Pending Notifications ===\n");
	printk("Sensor: three samples; Control starts after 3.5 s\n");

	return 0;
}
