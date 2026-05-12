#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/toolchain.h>
#include <zephyr/devicetree.h>

#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/time_units.h>

#include <zephyr/drivers/uart.h>

#include <zephyr/logging/log.h>
#include <zephyr/logging/log_core.h>

LOG_MODULE_REGISTER(TH_1_LOGIC, LOG_LEVEL_INF);

// report dev is just the UART log to output data on
// dev is what communicates with sigrok
static const struct device *report_dev = DEVICE_DT_GET(DT_NODELABEL(uart0));
static const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(uart1));



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


// true only when the capture is already running
static volatile bool capture_running = false;
static volatile bool simulate_running = false;



void check_respond_command(char command_byte);
void write_uart(uint8_t *msg, size_t len, const struct device *dev);
void write_uart_log(uint8_t *msg, size_t len, const struct device *dev);
static void dev_command_cb(const struct device *cb_dev, struct uart_event *evt, void *unused1);



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

    while (1)
    {
        if (capture_running)
        {
            k_msleep(100);
            for (int i = 0; i < 100; i++)
            {
                // something does not work with sending the commands
                // but the configuration works
                k_msleep(100);
                write_uart((uint8_t[]){'\x02', '\0', '\0', '\0'}, 4, dev);
                k_msleep(100);
                write_uart((uint8_t[]){'\x00', '\0', '\0', '\0'}, 4, dev);
            }
            capture_running = false;
        }
        else
        {
            printk("nocap\n");
            k_sleep(K_SECONDS(1));
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
            capture_running = false;
            goto RESET_CMD_BUF;
        case 0x02:
            /* write_uart_log((uint8_t[]){'1', 'S', 'L', 'O'}, 4, dev); */
            write_uart_log((uint8_t[]){'1', 'A', 'L', 'S'}, 4, dev);
            goto RESET_CMD_BUF;

        case 0x04:
            /* write_uart_log((uint8_t[]){ */
            /*     0x01, 'T', 'H', '-', '1', '-', 'L', 'O', 'G', 'I', 'C', */
            /*     0x00, 0x40, 0x04, 0x00 */
            /* }, 15, dev); */
            write_uart_log((uint8_t[]){
                0x01, 'T', 'H', '-', '1', '-', 'L', 'O', 'G', 'I', 'C', 0x00,
                0x23, 0x00, 0x00, 0x00, 0x64,
                0x21, 0x00, 0x00, 0x01, 0x90,
                0x40, 0x04,
                0x00
            }, 25, dev);
            goto RESET_CMD_BUF;

        case 0x01:
            capture_running = true;
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
            LOG_WRN("Unimplemented long command! (c=%d)", command_buf[0]);
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

