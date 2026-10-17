#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

/* Change to 1 for the second run. */
#define USE_MUTEX 1
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

/* Thread 1: motor control. */
static void control_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	k_msleep(1200);

	while (1) {
		if (USE_MUTEX) {
			k_mutex_lock(&motor_lock, K_FOREVER);
			printk("[CTRL] Mutex acquired\n");
		}

		/* Begin each cycle at the known, consistent baseline. */
		motor_data.target_rpm = 1200;
		motor_data.pwm_percent = 40;

		motor_data.target_rpm = 1800;
		printk("[CTRL] Target changed to 1800 RPM\n");

		/* Deliberately hold the update halfway complete. */
		k_msleep(700);

		motor_data.pwm_percent = 60;
		printk("[CTRL] PWM changed to 60%%\n");

		if (USE_MUTEX) {
			k_mutex_unlock(&motor_lock);
			printk("[CTRL] Mutex released\n");
		}

		k_msleep(1300);
	}
}

/* Thread 2: diagnostics. */
static void diagnostics_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	k_msleep(1500);

	while (1) {
		int rpm;
		int pwm;
		bool valid;

		printk("[DIAG] Requesting motor data\n");

		if (USE_MUTEX) {
			k_mutex_lock(&motor_lock, K_FOREVER);
			printk("[DIAG] Mutex acquired\n");
		}

		rpm = motor_data.target_rpm;
		pwm = motor_data.pwm_percent;

		if (USE_MUTEX) {
			k_mutex_unlock(&motor_lock);
			printk("[DIAG] Mutex released\n");
		}

		valid = (rpm == 1200 && pwm == 40) ||
			(rpm == 1800 && pwm == 60);

		printk("[DIAG] RPM=%d PWM=%d -> %s\n",
		       rpm, pwm, valid ? "CONSISTENT" : "INCONSISTENT");

		k_msleep(2000);
	}
}

K_THREAD_DEFINE(control_id, STACK_SIZE, control_thread,
		NULL, NULL, NULL, 5, 0, 0);

K_THREAD_DEFINE(diag_id, STACK_SIZE, diagnostics_thread,
		NULL, NULL, NULL, 5, 0, 0);

int main(void)
{
	printk("Motor Mutex Demonstration (USE_MUTEX=%d)\n", USE_MUTEX);
	return 0;
}
