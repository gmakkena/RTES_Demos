#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define STACK_SIZE          1024
#define USE_SEMAPHORE       1
#define SENSOR_INTERVAL_MS  1000
#define CONTROL_PERIOD_MS   250

/* No valid sensor measurement initially. */
static int measured_rpm;

/* Protects reads and writes of measured_rpm. */
K_MUTEX_DEFINE(motor_lock);

/* Binary semaphore: initial count 0, maximum count 1. */
K_SEM_DEFINE(sample_ready, 0, 1);

/* Sensor thread - priority 5. */
static void sensor_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	int sample_no = 0;

	while (1) {
		k_msleep(SENSOR_INTERVAL_MS);

		sample_no++;
		int rpm = 1100 + 50 * sample_no;

		k_mutex_lock(&motor_lock, K_FOREVER);
		measured_rpm = rpm;
		k_mutex_unlock(&motor_lock);

		printk("[SENSOR] New measurement %d: rpm=%d\n",
		       sample_no, rpm);

#if USE_SEMAPHORE
		printk("[SENSOR] Giving semaphore\n");
		k_sem_give(&sample_ready);
#endif
	}
}

/* Control thread - priority 2. */
static void control_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	while (1) {
#if USE_SEMAPHORE
		printk("[CONTROL] Waiting for measurement...\n");

		k_sem_take(&sample_ready, K_FOREVER);

		printk("[CONTROL] Semaphore received\n");
#else
		/* Control runs whether the sensor is ready or not. */
		k_msleep(CONTROL_PERIOD_MS);
#endif

		int rpm;

		k_mutex_lock(&motor_lock, K_FOREVER);
		rpm = measured_rpm;
		k_mutex_unlock(&motor_lock);

		if (rpm == 0) {
			printk("[CONTROL] rpm=0 -> FALSE STALL ALARM!\n");
		} else {
			printk("[CONTROL] Processing RPM=%d\n", rpm);
		}
	}
}

K_THREAD_DEFINE(control_tid, STACK_SIZE,
		control_thread,
		NULL, NULL, NULL, 2, 0, 0);

K_THREAD_DEFINE(sensor_tid, STACK_SIZE,
		sensor_thread,
		NULL, NULL, NULL, 5, 0, 0);

int main(void)
{
	printk("\n=== Binary Semaphore Demo ===\n");
	printk("USE_SEMAPHORE=%d\n", USE_SEMAPHORE);

	return 0;
}
