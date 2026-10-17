#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define STACK_SIZE 1024

struct motor_state {
	int target_rpm;
	int measured_rpm;
	int pwm_percent;
};

static struct motor_state motor_data = {
	.target_rpm = 1200,
	.measured_rpm = 1150,
	.pwm_percent = 40,
};

K_MUTEX_DEFINE(motor_lock);

/* Low-priority diagnostics thread (priority 8). */
static void diagnostics_low(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	k_mutex_lock(&motor_lock, K_FOREVER);

	printk("[L] Diagnostics acquired motor_lock; priority=%d\n",
	       k_thread_priority_get(k_current_get()));
	printk("[L] Motor: target=%d, measured=%d, pwm=%d\n",
	       motor_data.target_rpm,
	       motor_data.measured_rpm,
	       motor_data.pwm_percent);

	/* Simulate a CPU-intensive critical section while holding the mutex. */
	k_busy_wait(140000);
	printk("[L] Still holding lock; effective priority=%d\n",
	       k_thread_priority_get(k_current_get()));

	k_busy_wait(140000);
	printk("[L] Unlocking motor_lock; priority=%d\n",
	       k_thread_priority_get(k_current_get()));

	k_mutex_unlock(&motor_lock);

	printk("[L] After unlock, priority restored=%d\n",
	       k_thread_priority_get(k_current_get()));
}

/* High-priority motor-control thread (priority 2). */
static void control_high(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	/* Give the low-priority thread time to acquire motor_lock first. */
	k_msleep(40);

	printk("[H] Control requests motor_lock; priority=%d\n",
	       k_thread_priority_get(k_current_get()));
	k_mutex_lock(&motor_lock, K_FOREVER);

	printk("[H] Control acquired motor_lock\n");
	motor_data.target_rpm = 1800;
	motor_data.pwm_percent = 60;
	printk("[H] Updated motor: target=%d, pwm=%d\n",
	       motor_data.target_rpm,
	       motor_data.pwm_percent);

	k_mutex_unlock(&motor_lock);
}

/* Medium-priority telemetry thread (priority 5). */
static void telemetry_medium(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	k_msleep(70);
	printk("[M] Telemetry running; priority=%d\n",
	       k_thread_priority_get(k_current_get()));

	k_busy_wait(50000);
	printk("[M] Telemetry finished\n");
}

K_THREAD_DEFINE(high_tid, STACK_SIZE,
		control_high, NULL, NULL, NULL, 2, 0, 0);

K_THREAD_DEFINE(med_tid, STACK_SIZE,
		telemetry_medium, NULL, NULL, NULL, 5, 0, 0);

K_THREAD_DEFINE(low_tid, STACK_SIZE,
		diagnostics_low, NULL, NULL, NULL, 8, 0, 0);

int main(void)
{
	printk("\n=== Zephyr Mutex Priority Inheritance Demonstration ===\n");
	printk("High=2, Medium=5, Low=8\n");

	return 0;
}
