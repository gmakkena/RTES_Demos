/*
 * PIC32CM GC00 Curiosity Pro - interactive Zephyr shell demo.
 *
 * Type "help", "boardinfo", "led 0 on", "led 1 toggle", etc.
 * LEDs are defined by board devicetree aliases led0 and led1.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/shell/shell.h>

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)

#if !DT_NODE_EXISTS(LED0_NODE) || !DT_NODE_EXISTS(LED1_NODE)
#error "This application needs led0 and led1 devicetree aliases"
#endif

static const struct gpio_dt_spec leds[] = {
    GPIO_DT_SPEC_GET(LED0_NODE, gpios),
    GPIO_DT_SPEC_GET(LED1_NODE, gpios)
};

static bool led_state[2];
static bool led_ready;

static int cmd_boardinfo(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "Board: %s", CONFIG_BOARD);
    shell_print(sh, "Console: PKoB4 virtual COM port (SERCOM4), 115200 8N1");
    shell_print(sh, "LED aliases: led0 = PA20, led1 = PA18 (active-low)");
    shell_print(sh, "LED GPIO initialized: %s", led_ready ? "yes" : "no");
    shell_print(sh, "Uptime: %lld ms", (long long)k_uptime_get());
    return 0;
}
SHELL_CMD_ARG_REGISTER(boardinfo, NULL,
                       "Show board name, LED connections, and uptime.",
                       cmd_boardinfo, 1, 0);

static int cmd_led(const struct shell *sh, size_t argc, char **argv)
{
    int n;
    bool new_value;
    int ret;

    if (argc != 3) {
        shell_error(sh, "Usage: led <0|1> <on|off|toggle|status>");
        return -EINVAL;
    }

    if ((strlen(argv[1]) != 1U) || (argv[1][0] != '0' && argv[1][0] != '1')) {
        shell_error(sh, "LED must be 0 or 1");
        return -EINVAL;
    }

    if (!led_ready) {
        shell_error(sh, "LED GPIO unavailable or not configured");
        return -ENODEV;
    }

    n = argv[1][0] - '0';
    new_value = led_state[n];

    if (strcmp(argv[2], "status") == 0) {
        shell_print(sh, "LED%d: %s", n, new_value ? "ON" : "OFF");
        return 0;
    }

    if (strcmp(argv[2], "on") == 0) {
        new_value = true;
    } else if (strcmp(argv[2], "off") == 0) {
        new_value = false;
    } else if (strcmp(argv[2], "toggle") == 0) {
        new_value = !new_value;
    } else {
        shell_error(sh, "Unknown action: %s", argv[2]);
        shell_print(sh, "Use on, off, toggle, or status");
        return -EINVAL;
    }

    /* GPIO_ACTIVE_LOW is handled automatically by gpio_pin_set_dt(). */
    ret = gpio_pin_set_dt(&leds[n], (int)new_value);
    if (ret < 0) {
        shell_error(sh, "LED%d GPIO operation failed: %d", n, ret);
        return ret;
    }

    led_state[n] = new_value;
    shell_print(sh, "LED%d: %s", n, new_value ? "ON" : "OFF");
    return 0;
}
SHELL_CMD_ARG_REGISTER(led, NULL,
                       SHELL_HELP("Control the two onboard LEDs.",
                                  "<0|1> <on|off|toggle|status>"),
                       cmd_led, 3, 0);

int main(void)
{
    if (!gpio_is_ready_dt(&leds[0]) || !gpio_is_ready_dt(&leds[1])) {
        return 0;
    }

    if (gpio_pin_configure_dt(&leds[0], GPIO_OUTPUT_INACTIVE) < 0) {
        return 0;
    }
    if (gpio_pin_configure_dt(&leds[1], GPIO_OUTPUT_INACTIVE) < 0) {
        return 0;
    }

    led_state[0] = false;
    led_state[1] = false;
    led_ready = true;

    /* Shell runs in its own Zephyr thread even after main returns. */
    return 0;
}
