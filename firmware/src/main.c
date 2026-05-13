#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/toolchain.h>
#include <zephyr/devicetree.h>

#include <zephyr/sys/util.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util_macro.h>
#include <zephyr/sys/time_units.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/counter.h>

#include <zephyr/logging/log.h>
#include <zephyr/logging/log_core.h>

#include "th_ringbuffer.h"

LOG_MODULE_REGISTER(TH_1_LOGIC, LOG_LEVEL_INF);

// report dev is just the UART log to output data on
// dev is what communicates with sigrok
static const struct device *report_dev = DEVICE_DT_GET(DT_NODELABEL(uart0));
static const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(uart1));

// 1Mhz timer
static const struct device *const counter_dev = DEVICE_DT_GET(DT_NODELABEL(timer3));

// inputs
#define USER_NODE DT_PATH(zephyr_user)
static const struct gpio_dt_spec inputs[] = {
    GPIO_DT_SPEC_GET_BY_IDX(USER_NODE, input_gpios, 0),
    GPIO_DT_SPEC_GET_BY_IDX(USER_NODE, input_gpios, 1),
    GPIO_DT_SPEC_GET_BY_IDX(USER_NODE, input_gpios, 2),
    GPIO_DT_SPEC_GET_BY_IDX(USER_NODE, input_gpios, 3),
};
#define INPUT_PINS_MASK (BIT(inputs[0].pin) | BIT(inputs[1].pin) |  BIT(inputs[2].pin) | BIT(inputs[3].pin))
gpio_port_value_t input_port;

// DMA command buffer is only 1 byte so it can
// trigger an interrupt as soon as it receives
// anything.
// This is effectively copying the interrupt 
// driven uart method, but I need to use DMA
// uart for other parts of the system.
static uint8_t command_buf[1];

#define LONG_COMMAND_BUF_LEN 5
static volatile uint8_t c_buf_index = 0;
static volatile uint8_t long_command_buf[LONG_COMMAND_BUF_LEN];


bool record_complete = false;


void check_respond_command(char command_byte);
void write_uart(uint8_t *msg, size_t len, const struct device *dev);
void write_uart_log(uint8_t *msg, size_t len, const struct device *dev);
static void dev_command_cb(const struct device *cb_dev, struct uart_event *evt, void *unused1);

static void counter_1mhz_isr(const struct device *unused1, void *unused2);



int main()
{

    int err;
    if (!(err = device_is_ready(report_dev)))
    {
        LOG_ERR("Reporting serial connection not ready!");
        return -1;
    }

    if (!(err = device_is_ready(dev)))
    {
        LOG_ERR("Signal serial connection not ready!");
        return -1;
    }

    if ((err = uart_callback_set(dev, dev_command_cb, NULL)) != 0)
    {
        LOG_ERR("Failed to configure SUMP command callback! (err: %d)", err);
        return -1;
    }

    if ((err = uart_rx_enable(dev, command_buf, sizeof(command_buf), SYS_FOREVER_US)) != 0)
    {
        LOG_ERR("Failed to register SUMP command callback! (err: %d)", err);
        return -1;
    }

    LOG_INF("System Initialised!");


    if (!(err = device_is_ready(counter_dev)))
    {
        LOG_ERR("Counter device is not ready! (err: %d)", err);
        return -1;
    }
    struct counter_top_cfg top_cfg = {
        .ticks = 1,
        .callback = counter_1mhz_isr,
        .user_data = NULL,
        .flags = COUNTER_TOP_CFG_RESET_WHEN_LATE
    };

    counter_set_top_value(counter_dev, &top_cfg);
    LOG_INF("Counter initialised!");


    for (int i = 0; i < ARRAY_SIZE(inputs); i++)
    {
        if (!(err = gpio_is_ready_dt(&inputs[i])))
        {
            LOG_ERR("Input GPIO pin %d is not ready! (err: %d)", i, err);
            return -1;
        }

        if ((err = gpio_pin_configure_dt(&inputs[i], GPIO_INPUT)) != 0)
        {
            LOG_ERR("Failed to configure GPIO pin %d! (err: %d)", i, err);
            return err;
        }
    }
    LOG_INF("All pins initialised!");


    while (1)
    {
        if (record_complete)
        {
            uint8_t recorded_buf[100];
            ring_buffer_read(recorded_buf, 96);
            record_complete = false;
        }
        else
        {
            k_msleep(100);
        }
    }

}


// irq callback for each char sent from sigrok
static void dev_command_cb(const struct device *cb_dev, struct uart_event *evt, void *unused1)
{
    ARG_UNUSED(unused1);
    switch (evt->type)
    {
        // buffer full (ie got one byte)
        case UART_RX_RDY:
            check_respond_command(command_buf[0]);
            break;
        case UART_RX_BUF_REQUEST:
            uart_rx_buf_rsp(cb_dev, command_buf, sizeof(command_buf));
            break;
        default:
            break;
    }
}


void check_respond_command(char command_byte)
{
    long_command_buf[c_buf_index++] = command_byte;

    switch (long_command_buf[0])
    {
        case 0x00:
            // Rebooting takes too long. Just need to
            // reset all variables here.
            counter_stop(counter_dev);
            ring_buffer_clear();
            record_complete = false;
            goto RESET_CMD_BUF;

        case 0x01:
            counter_start(counter_dev);
            goto RESET_CMD_BUF;

        case 0x02:
            write_uart_log((uint8_t[]){'1', 'A', 'L', 'S'}, 4, dev);
            goto RESET_CMD_BUF;

        case 0x04:
            write_uart_log((uint8_t[]){
                0x01, 'T', 'H', '-', '1', '-', 'L', 'O', 'G', 'I', 'C', 0x00,
                0x23, 0x00, 0x00, 0x00, 0x64,
                0x21, 0x00, 0x00, 0x01, 0x90,
                0x40, 0x04,
                0x00
            }, 25, dev);
            goto RESET_CMD_BUF;


        case 0x05:
        case 0x06:
        case 0x07:
            LOG_WRN("Unimplemented command! (c=%d)", command_buf[0]);
            goto RESET_CMD_BUF;
        case 0xC0:
        case 0xC1:
        case 0xC2:
        case 0xC4:
        case 0xC5:
        case 0xC6:
        case 0xC8:
        case 0xC9:
        case 0xCA:
        case 0xCC:
        case 0xCD:
        case 0xCE:
        case 0x80:
        case 0x81:
        case 0x82:
            /* LOG_WRN("Unimplemented long command! (c=%02x)", command_buf[0]); */
            if (c_buf_index == 5)
                goto RESET_CMD_BUF;
            break;
        default:
            LOG_WRN("Invalid command sent! (c=%d)", command_buf[0]);
            goto RESET_CMD_BUF;
    }

    return;

RESET_CMD_BUF:
    LOG_HEXDUMP_INF((uint8_t*)long_command_buf, c_buf_index, "Received command:");
    memset((uint8_t *)long_command_buf, 0, LONG_COMMAND_BUF_LEN);
    c_buf_index = 0;
    return;

}

void write_uart(uint8_t *msg, size_t len, const struct device *tdev)
{
    uart_tx(dev, msg, len, SYS_FOREVER_US);
}


void write_uart_log(uint8_t *msg, size_t len, const struct device *tdev)
{
    uart_tx(dev, msg, len, SYS_FOREVER_US);
    LOG_HEXDUMP_INF((uint8_t*)msg, len, "Replied with:");
}


static void counter_1mhz_isr(const struct device *unused1, void *unused2)
{
    ARG_UNUSED(unused1);
    ARG_UNUSED(unused2);

    // tmp since we just hard record only 96 atm
    if (ringbuffer_index > 96)
    {
        record_complete = true;
        counter_stop(counter_dev);
    }

    gpio_port_get_raw(inputs[0].port, &input_port);
    ring_buffer_write_one(((uint8_t)(input_port & INPUT_PINS_MASK)));
}
