#include "FreeRTOS.h"
#include "task.h"
#include <queue.h>
#include "semphr.h"
#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hc06.h"
#include "ssd1306/ssd1306.h"
#include "hardware/i2c.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"

#define UART_ID uart0
const int ADC_PIN_X=26;
const int ADC_PIN_Y=27;
const uint BTN_PIN = 16;

ssd1306_t disp;

typedef struct adc {
    int axis;
    int val;
} adc_t;

SemaphoreHandle_t xSemaphorePIN;
QueueHandle_t xQueueADC;

void btn_callback(uint gpio, uint32_t events){
    if (gpio==BTN_PIN){
        xSemaphoreGiveFromISR(xSemaphorePIN,0);
    }
}

void x_task(void *p){
    adc_init();
    adc_gpio_init(ADC_PIN_X);

    int value_vec[5]={0,0,0,0,0};
    while (1)
    {
        adc_select_input(0);
        int value = adc_read();
        int s=0;
        value=(value-2047)/8;
        for (int i=0;i<4;i++){
            value_vec[i]=value_vec[i+1];
            s+=value_vec[i];
        }
        value_vec[4]=value;
        s+=value;
        s=s/5;
        if (s>255){
            s=255;
        }
        if (s<-255){
            s=-255;
        }
        if (s<-30||s>30){
            adc_t x;
            x.axis=0;
            x.val=s;
            xQueueSend(xQueueADC, &x, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
}

void y_task(void *p){
    adc_init();
    adc_gpio_init(ADC_PIN_Y);


    int value_vec[5]={0,0,0,0,0};
    while (1)
    {
        adc_select_input(1);
        int value = adc_read();
        int s=0;
        value=(value-2047)/8;
        for (int i=0;i<4;i++){
            value_vec[i]=value_vec[i+1];
            s+=value_vec[i];
        }
        value_vec[4]=value;
        s+=value;
        s=s/5;
        if (s>255){
            s=255;
        }
        if (s<-255){
            s=-255;
        }
        if (s<-30||s>30){
            adc_t y;
            y.axis=1;
            y.val=s;
            xQueueSend(xQueueADC, &y, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void com_task(void *p){
    adc_t adc_xy;
    while (1){
        if (xQueueReceive(xQueueADC, &adc_xy,  pdMS_TO_TICKS(50))){
            
            uart_putc(UART_ID,adc_xy.axis);
            uart_putc(UART_ID,adc_xy.val);
            uart_putc(UART_ID,(adc_xy.val >> 8));
            uart_putc(UART_ID,-1);
        }
    }
}

void bluetooth_task(void *p){
    char pin[5];
    srand(time_us_32());
    while(true){
        if (xSemaphoreTake(xSemaphorePIN,pdMS_TO_TICKS(10))){
            ssd1306_clear(&disp);
            ssd1306_draw_string(&disp,8,12,1,"Gerando PIN");
            ssd1306_show(&disp);
            for(int i=0;i<4;i++){
                pin[i]='0'+ rand() % 10;
            }
            pin[4] ='\0';
            vTaskDelay(pdMS_TO_TICKS(500));
            ssd1306_clear(&disp);
            ssd1306_draw_string(&disp, 8, 12, 2, "PIN: ");
            ssd1306_draw_string(&disp, 64, 12, 2, pin);
            ssd1306_show(&disp);
            hc06_config("BERNARDO",pin);
        }
    }
}

void init_uart_hc06(void) {
    uart_init(HC06_UART_ID, HC06_BAUD_RATE);

    // Set the TX and RX pins by using the function select on the GPIO
    // Set datasheet for more information on function select
    gpio_set_function(HC06_TX_PIN, UART_FUNCSEL_NUM(HC06_UART_ID, HC06_TX_PIN));
    gpio_set_function(HC06_RX_PIN, UART_FUNCSEL_NUM(HC06_UART_ID, HC06_RX_PIN));

    int __unused actual = uart_set_baudrate(HC06_UART_ID, HC06_BAUD_RATE);

    // Set UART flow control CTS/RTS, we don't want these, so turn them off
    uart_set_hw_flow(HC06_UART_ID, false, false);

    // Set our data format
    uart_set_format(HC06_UART_ID, 8, 1, UART_PARITY_NONE);
}

void oled_init(void) {

    i2c_init(i2c1, 400000);

    gpio_set_function(2, GPIO_FUNC_I2C);
    gpio_set_function(3, GPIO_FUNC_I2C);

    gpio_pull_up(2);
    gpio_pull_up(3);

    disp.external_vcc = false;

    // OLED 128x32
    ssd1306_init(&disp, 128, 32, 0x3C, i2c1);

    ssd1306_clear(&disp);
    ssd1306_show(&disp);
}

int main(void)
{
    stdio_init_all();
    init_uart_hc06();
    oled_init();

    gpio_init(BTN_PIN);
    gpio_set_dir(BTN_PIN, GPIO_IN);
    gpio_pull_up(BTN_PIN);
    gpio_set_irq_enabled_with_callback(BTN_PIN, GPIO_IRQ_EDGE_FALL, true, &btn_callback);

    xSemaphorePIN = xSemaphoreCreateBinary();
    xQueueADC = xQueueCreate(32, sizeof(adc_t));

    //xTaskCreate(x_task, "X_axis", 256, NULL, 1, NULL);
    //xTaskCreate(y_task, "Y_axis", 256, NULL, 1, NULL);
    //xTaskCreate(com_task, "comunication", 256, NULL, 1, NULL);
    xTaskCreate(bluetooth_task, "Bluetooth_config", 1024, NULL, 1, NULL);
    vTaskStartScheduler();
    while (1)
    ;
}
