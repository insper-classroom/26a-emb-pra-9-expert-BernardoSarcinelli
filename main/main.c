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

const int LED_PIN_R=15;
const int LED_PIN_G=14;
const int LED_PIN_B=13;
const int ADC_PIN_X=26;
const int ADC_PIN_Y=27;
const uint BTN_PIN=16;


ssd1306_t disp;

typedef struct adc {
    int axis;
    int val;
} adc_t;

TaskHandle_t h_x, h_y, h_com, h_led;

SemaphoreHandle_t xSemaphoreST;
SemaphoreHandle_t xSemaphorePIN;
QueueHandle_t xQueueADC;

void uart_rx_handler() {
    uint8_t ch = uart_getc(HC06_UART_ID);
    printf("%c\n",ch);
    xSemaphoreGiveFromISR(xSemaphoreST,0);
}

void deinit_uart_irq() {
    int UART_IRQ = HC06_UART_ID == uart0 ? UART0_IRQ : UART1_IRQ;
    
    // desabilita interrupcao do uart
    uart_set_irq_enables(HC06_UART_ID, false, false);
    
    // remove o handler
    irq_set_enabled(UART_IRQ, false);
    irq_remove_handler(UART_IRQ, uart_rx_handler);
}

void init_uart_irq() {
     // Turn off FIFO's - we want to do this character by character
    uart_set_fifo_enabled(HC06_UART_ID, false);

    // Set up a RX interrupt
    // We need to set up the handler first
    // Select correct interrupt for the UART we are using
    int UART_IRQ = HC06_UART_ID == uart0 ? UART0_IRQ : UART1_IRQ;

    // And set up and enable the interrupt handlers
    irq_set_exclusive_handler(UART_IRQ, uart_rx_handler);
    irq_set_enabled(UART_IRQ, true);

    // Now enable the UART to send interrupts - RX only
    uart_set_irq_enables(HC06_UART_ID, true, false);
}

void btn_callback(uint gpio, uint32_t events){
    if (events==0x04){
        if (gpio==BTN_PIN){
            xSemaphoreGiveFromISR(xSemaphorePIN,0);
        }
        if (gpio==HC06_STATE_PIN){
            xSemaphoreGiveFromISR(xSemaphoreST,0);
        }
    }else if (events==0x08){
        if (gpio==HC06_STATE_PIN){
            xSemaphoreGiveFromISR(xSemaphoreST,0);
        }
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
            uart_putc_raw(HC06_UART_ID,adc_xy.axis);
            uart_putc_raw(HC06_UART_ID,adc_xy.val);
            uart_putc_raw(HC06_UART_ID,(adc_xy.val >> 8));
            uart_putc_raw(HC06_UART_ID,-1);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void bluetooth_task(void *p){
    char pin[5];
    while(true){
        if (xSemaphoreTake(xSemaphorePIN,pdMS_TO_TICKS(10))){
            srand(time_us_32());
            ssd1306_clear(&disp);
            ssd1306_draw_string(&disp,8,12,1,"Gerando PIN");
            ssd1306_show(&disp);
            for(int i=0;i<4;i++){
                pin[i]='0'+ rand() % 10;
            }
            pin[4] ='\x0';
            deinit_uart_irq();       // desliga IRQ antes do hc06_config
            vTaskSuspend(h_x);
            vTaskSuspend(h_y);
            vTaskSuspend(h_com);
            vTaskSuspend(h_led);

            hc06_config("BERN",pin);


            vTaskResume(h_x);
            vTaskResume(h_y);
            vTaskResume(h_com);
            vTaskResume(h_led);

            init_uart_irq();


            ssd1306_clear(&disp);
            ssd1306_draw_string(&disp, 8, 12, 2, "PIN: ");
            ssd1306_draw_string(&disp, 64, 12, 2, pin);
            ssd1306_show(&disp);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void led_task(void *p){
    gpio_set_function(LED_PIN_R, GPIO_FUNC_PWM);
    const uint slice_num_r = pwm_gpio_to_slice_num(LED_PIN_R);
    const uint chan_r = pwm_gpio_to_channel(LED_PIN_R);

    gpio_set_function(LED_PIN_G, GPIO_FUNC_PWM);
    const uint slice_num_g = pwm_gpio_to_slice_num(LED_PIN_G);
    const uint chan_g = pwm_gpio_to_channel(LED_PIN_G);

    gpio_set_function(LED_PIN_B, GPIO_FUNC_PWM);
    const uint slice_num_b = pwm_gpio_to_slice_num(LED_PIN_B);
    const uint chan_b = pwm_gpio_to_channel(LED_PIN_B);

    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, 4.0f);
    pwm_config_set_wrap(&config, 255);

    pwm_init(slice_num_r, &config, true);
    if (slice_num_g != slice_num_r) {
        pwm_init(slice_num_g, &config, true);
    }
    if (slice_num_b != slice_num_r && slice_num_b != slice_num_g) {
        pwm_init(slice_num_b, &config, true);
    }

    pwm_set_chan_level(slice_num_r, chan_r, 0);
    pwm_set_chan_level(slice_num_g, chan_g, 0);
    pwm_set_chan_level(slice_num_b, chan_b, 0);
    int state=0;
    while (1){
        if (xSemaphoreTake(xSemaphoreST,pdMS_TO_TICKS(10))){
            state = !state;
        }
        if (state){
            //azul acesso
            pwm_set_chan_level(slice_num_b, chan_b, 255);
        }
        if (!state){
            //fade in/out
            for (int i = 0; i <= 255; i++) {
                pwm_set_chan_level(slice_num_b, chan_b, i);
                vTaskDelay(pdMS_TO_TICKS(5));
            }
            for (int i = 255; i >= 0; i--) {
                pwm_set_chan_level(slice_num_b, chan_b, i);
                vTaskDelay(pdMS_TO_TICKS(5));
            }
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

    gpio_init(HC06_STATE_PIN);
    gpio_set_dir(HC06_STATE_PIN, GPIO_IN);
    gpio_set_irq_enabled(HC06_STATE_PIN, GPIO_IRQ_EDGE_FALL|GPIO_IRQ_EDGE_RISE, true);


    //init_uart_irq();

    xSemaphoreST = xSemaphoreCreateBinary();
    xSemaphorePIN = xSemaphoreCreateBinary();
    xQueueADC = xQueueCreate(32, sizeof(adc_t));

    xTaskCreate(x_task, "X_axis", 256, NULL, 1, &h_x);
    xTaskCreate(y_task, "Y_axis", 256, NULL, 1, &h_y);
    xTaskCreate(com_task, "comunication", 256, NULL, 1, &h_com);
    xTaskCreate(bluetooth_task, "Bluetooth_config", 1024, NULL, 1, NULL);
    xTaskCreate(led_task, "led", 64, NULL, 1, &h_led);
    vTaskStartScheduler();
    while (1)
    ;
}