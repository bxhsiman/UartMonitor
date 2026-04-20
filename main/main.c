/*
 * ESP32-C3 USB Serial/JTAG与UART1透传程序
 * USB Serial/JTAG <-> UART1 双向透传，带时间戳显示
 * 使用 idf.py monitor 可直接发送和接收数据
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "soc/gpio_num.h"
#include "sys/time.h"

// UART1配置宏定义（可根据需要修改GPIO）
#define UART1_TXD_PIN       (GPIO_NUM_9)   // 可修改
#define UART1_RXD_PIN       (GPIO_NUM_8)   // 可修改
#define UART1_RTS_PIN       (UART_PIN_NO_CHANGE)
#define UART1_CTS_PIN       (UART_PIN_NO_CHANGE)
#define UART1_BAUD_RATE     (115200)
#define UART1_PORT          (UART_NUM_1)

// 缓冲区配置
#define BUF_SIZE            (1024)
#define RD_BUF_SIZE         (BUF_SIZE)
#define INPUT_BUF_SIZE      (2048)   // 输入缓冲区大小

// 特殊键定义
#define KEY_ESC             (0x1B)   // ESC键：发送数据
#define KEY_BACKSPACE       (0x08)   // 退格键
#define KEY_DEL             (0x7F)   // Delete键

static const char *TAG = "USB_UART_BRIDGE";

// 获取时间戳字符串（毫秒精度）
static void get_timestamp(char *buf, size_t buf_size)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    time_t sec = tv.tv_sec;
    long ms = tv.tv_usec / 1000;

    struct tm timeinfo;
    localtime_r(&sec, &timeinfo);

    snprintf(buf, buf_size, "[%02d:%02d:%02d.%03ld]",
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, ms);
}

// USB Serial/JTAG 初始化
static void usb_serial_init(void)
{
    usb_serial_jtag_driver_config_t usb_serial_config = {
        .rx_buffer_size = BUF_SIZE * 2,
        .tx_buffer_size = BUF_SIZE * 2,
    };

    // 安装USB Serial/JTAG驱动
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb_serial_config));

    ESP_LOGI(TAG, "USB Serial/JTAG initialized");
}

// UART1初始化
static void uart1_init(void)
{
    const uart_config_t uart_config = {
        .baud_rate = UART1_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    // 配置UART参数
    ESP_ERROR_CHECK(uart_param_config(UART1_PORT, &uart_config));

    // 设置UART引脚
    ESP_ERROR_CHECK(uart_set_pin(UART1_PORT, UART1_TXD_PIN, UART1_RXD_PIN,
                                  UART1_RTS_PIN, UART1_CTS_PIN));

    // 安装UART驱动
    ESP_ERROR_CHECK(uart_driver_install(UART1_PORT, BUF_SIZE * 2, BUF_SIZE * 2, 0, NULL, 0));

    ESP_LOGI(TAG, "UART1 initialized: TXD=GPIO%d, RXD=GPIO%d, Baud=%d",
             UART1_TXD_PIN, UART1_RXD_PIN, UART1_BAUD_RATE);
}

// USB Serial/JTAG -> UART1 透传任务（按ESC发送模式）
static void usb_to_uart_task(void *arg)
{
    uint8_t *input_buffer = (uint8_t *)malloc(INPUT_BUF_SIZE);
    uint8_t *read_buf = (uint8_t *)malloc(RD_BUF_SIZE);

    if (!input_buffer || !read_buf) {
        ESP_LOGE(TAG, "Failed to allocate memory for usb_to_uart_task");
        if (input_buffer) free(input_buffer);
        if (read_buf) free(read_buf);
        vTaskDelete(NULL);
        return;
    }

    char timestamp[32];
    int input_len = 0;

    ESP_LOGI(TAG, ">> 输入模式：输入数据，按 ESC 发送到UART1");

    while (1) {
        // 从USB Serial/JTAG读取数据
        int len = usb_serial_jtag_read_bytes(read_buf, RD_BUF_SIZE, pdMS_TO_TICKS(20));

        if (len > 0) {
            for (int i = 0; i < len; i++) {
                uint8_t ch = read_buf[i];

                if (ch == KEY_ESC) {
                    // 按下ESC键，自动添加\r\n并发送缓冲区数据
                    if (input_len > 0) {
                        // 添加\r\n到缓冲区末尾
                        if (input_len + 2 <= INPUT_BUF_SIZE) {
                            input_buffer[input_len++] = '\r';
                            input_buffer[input_len++] = '\n';
                        }

                        get_timestamp(timestamp, sizeof(timestamp));
                        printf("\n%s USB->UART1 [%d bytes]: ", timestamp, input_len);

                        // 显示发送的数据
                        for (int j = 0; j < input_len; j++) {
                            if (input_buffer[j] >= 32 && input_buffer[j] <= 126) {
                                printf("%c", input_buffer[j]);
                            } else if (input_buffer[j] == '\r' || input_buffer[j] == '\n') {
                                printf("\\%c", input_buffer[j] == '\r' ? 'r' : 'n');
                            } else {
                                printf("<%02X>", input_buffer[j]);
                            }
                        }
                        printf("\n");

                        // 转发到UART1
                        int written = uart_write_bytes(UART1_PORT, (const char *)input_buffer, input_len);
                        uart_wait_tx_done(UART1_PORT, pdMS_TO_TICKS(100)); // 等待发送完成

                        if (written < 0) {
                            ESP_LOGE(TAG, "UART1 write error: %d", written);
                        } else {
                            ESP_LOGI(TAG, ">> 已发送 %d 字节到UART1（已自动添加\\r\\n）", written);
                            ESP_LOGI(TAG, ">> UART1引脚: TXD=GPIO%d, RXD=GPIO%d", UART1_TXD_PIN, UART1_RXD_PIN);
                        }

                        // 清空缓冲区
                        input_len = 0;
                    } else {
                        printf("\n");
                        ESP_LOGW(TAG, ">> 缓冲区为空，没有数据发送");
                    }
                    printf(">> ");
                    fflush(stdout);

                } else if (ch == KEY_BACKSPACE || ch == KEY_DEL) {
                    // 退格键
                    if (input_len > 0) {
                        input_len--;
                        printf("\b \b");  // 删除一个字符
                        fflush(stdout);
                    }

                } else {
                    // 普通字符，添加到缓冲区
                    if (input_len < INPUT_BUF_SIZE) {
                        input_buffer[input_len++] = ch;
                        // 回显字符
                        if (ch >= 32 && ch <= 126) {
                            printf("%c", ch);
                        } else if (ch == '\r') {
                            printf("\r\n");
                        } else if (ch == '\n') {
                            // 忽略单独的\n
                        } else {
                            printf("<%02X>", ch);
                        }
                        fflush(stdout);
                    } else {
                        ESP_LOGW(TAG, "\n>> 缓冲区已满 (%d 字节)，请按ESC发送", INPUT_BUF_SIZE);
                        printf(">> ");
                        fflush(stdout);
                    }
                }
            }
        } else if (len < 0) {
            ESP_LOGE(TAG, "USB Serial read error: %d", len);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    free(input_buffer);
    free(read_buf);
}

// UART1 -> USB Serial/JTAG 透传任务
static void uart_to_usb_task(void *arg)
{
    uint8_t *data = (uint8_t *)malloc(RD_BUF_SIZE);
    if (!data) {
        ESP_LOGE(TAG, "Failed to allocate memory for uart_to_usb_task");
        vTaskDelete(NULL);
        return;
    }

    char timestamp[32];

    while (1) {
        // 从UART1读取数据
        int len = uart_read_bytes(UART1_PORT, data, RD_BUF_SIZE, pdMS_TO_TICKS(20));

        if (len > 0) {
            // 显示接收到的数据（带时间戳）
            get_timestamp(timestamp, sizeof(timestamp));
            printf("%s UART1->USB [%d bytes]: ", timestamp, len);
            for (int i = 0; i < len; i++) {
                if (data[i] >= 32 && data[i] <= 126) {
                    printf("%c", data[i]);
                } else if (data[i] == '\r' || data[i] == '\n') {
                    printf("\\%c", data[i] == '\r' ? 'r' : 'n');
                } else {
                    printf("<%02X>", data[i]);
                }
            }
            printf("\n");

            // 转发到USB Serial/JTAG
            int written = usb_serial_jtag_write_bytes((const char *)data, len, pdMS_TO_TICKS(20));
            if (written < 0) {
                ESP_LOGE(TAG, "USB Serial write error: %d", written);
            }
        } else if (len < 0) {
            ESP_LOGE(TAG, "UART1 read error: %d", len);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    free(data);
}

void app_main(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║   ESP32-C3 USB-UART1 透传程序 (ESC发送模式)            ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "配置信息:");
    ESP_LOGI(TAG, "  - USB Serial/JTAG <-> UART1 透传模式");
    ESP_LOGI(TAG, "  - UART1: TXD=GPIO%d, RXD=GPIO%d, Baud=%d",
             UART1_TXD_PIN, UART1_RXD_PIN, UART1_BAUD_RATE);
    ESP_LOGI(TAG, "");

    // 初始化USB Serial/JTAG
    usb_serial_init();

    // 初始化UART1
    uart1_init();

    ESP_LOGI(TAG, "正在启动透传任务...");

    // 创建透传任务
    BaseType_t ret1 = xTaskCreate(usb_to_uart_task, "usb_to_uart", 8192, NULL, 10, NULL);
    BaseType_t ret2 = xTaskCreate(uart_to_usb_task, "uart_to_usb", 4096, NULL, 10, NULL);

    if (ret1 != pdPASS || ret2 != pdPASS) {
        ESP_LOGE(TAG, "Failed to create tasks!");
        return;
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "═══════════════════ 使用说明 ═══════════════════");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "【发送数据到UART1】");
    ESP_LOGI(TAG, "  1. 在终端中输入数据（可以输入很长的字符串）");
    ESP_LOGI(TAG, "  2. 支持回车换行（数据会保存在缓冲区）");
    ESP_LOGI(TAG, "  3. 按 ESC 键一次性发送所有缓冲数据");
    ESP_LOGI(TAG, "  4. 支持退格键(Backspace)删除字符");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "【接收UART1数据】");
    ESP_LOGI(TAG, "  - UART1收到的数据会实时显示（带时间戳）");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "【修改GPIO】");
    ESP_LOGI(TAG, "  - 编辑 main.c 的 UART1_TXD_PIN 和 UART1_RXD_PIN");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "════════════════════════════════════════════════");
    ESP_LOGI(TAG, "");

    // 拉高GPIO7
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << 7),
        .pull_down_en = 0,
        .pull_up_en = 0
    };
    vTaskDelay(pdMS_TO_TICKS(1000));
    gpio_config(&io_conf);
    gpio_set_level(GPIO_NUM_7, 0);  // 失效 
    vTaskDelay(pdMS_TO_TICKS(800));

    gpio_set_level(GPIO_NUM_7, 1); // 发送开机脉冲
    vTaskDelay(pdMS_TO_TICKS(800));
    gpio_set_level(GPIO_NUM_7,0);

    // gpio_set_level(GPIO_NUM_7, 1);

    // 等待一下让日志显示完整
    vTaskDelay(pdMS_TO_TICKS(100));
    printf(">> ");
    fflush(stdout);
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
