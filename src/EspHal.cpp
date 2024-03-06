#ifndef ARDUINO
#include "EspHal.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp32/rom/gpio.h"
#include "soc/rtc.h"
#include "soc/dport_reg.h"
#include "soc/spi_reg.h"
#include "soc/spi_struct.h"
#include "hal/gpio_hal.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

#include <esp_private/periph_ctrl.h>

#define MATRIX_DETACH_OUT_SIG (0x100)
#define MATRIX_DETACH_IN_LOW_PIN (0x30)

#define HOST_ID SPI2_HOST

// all of the following is needed to calculate SPI clock divider
#define ClkRegToFreq(reg) (apb_freq / (((reg)->clkdiv_pre + 1) * ((reg)->clkcnt_n + 1)))

typedef union {
    uint32_t value;
    struct {
        uint32_t clkcnt_l : 6;
        uint32_t clkcnt_h : 6;
        uint32_t clkcnt_n : 6;
        uint32_t clkdiv_pre : 13;
        uint32_t clk_equ_sysclk : 1;
    };
} spiClk_t;

EspHal::EspHal(int8_t sck, int8_t miso, int8_t mosi)
    : RadioLibHal(INPUT, OUTPUT, LOW, HIGH, RISING, FALLING),
    spiSCK(sck), spiMISO(miso), spiMOSI(mosi) {
    gpio_install_isr_service((int) ESP_INTR_FLAG_IRAM);
}

void EspHal::init() {
    static const int SPI_Frequency = 2000000;
    spi_bus_config_t spi_bus_config = {
        .mosi_io_num = this->spiMOSI,
        .miso_io_num = this->spiMISO,
        .sclk_io_num = this->spiSCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .data4_io_num = -1,
        .data5_io_num = -1,
        .data6_io_num = -1,
        .data7_io_num = -1,
        .max_transfer_sz = 0,
        .flags = 0,
        .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO, // INTR_CPU_ID_AUTO,
        .intr_flags = 0};
    ESP_ERROR_CHECK_WITHOUT_ABORT(spi_bus_initialize(HOST_ID, &spi_bus_config, SPI_DMA_CH_AUTO));
    spi_device_interface_config_t devcfg;
    memset(&devcfg, 0, sizeof(spi_device_interface_config_t));
    devcfg.clock_speed_hz = SPI_Frequency;
    devcfg.spics_io_num = -1;
    devcfg.queue_size = 7;
    devcfg.mode = 0;
    devcfg.flags = SPI_DEVICE_NO_DUMMY;
    std::lock_guard guard(_mutex);
    ESP_ERROR_CHECK_WITHOUT_ABORT(spi_bus_add_device(HOST_ID, &devcfg, &_handle));
}

void EspHal::term() {
    std::lock_guard guard(_mutex);
    spi_bus_remove_device(_handle);
}

// GPIO-related methods (pinMode, digitalWrite etc.) should check
// RADIOLIB_NC as an alias for non-connected pins
void EspHal::pinMode(uint32_t pin, uint32_t mode) {
    if (pin == RADIOLIB_NC)
        return;

    gpio_hal_context_t gpiohal;
    gpiohal.dev = GPIO_LL_GET_HW(GPIO_PORT_0);

    gpio_config_t conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = (gpio_mode_t) mode,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = (gpio_int_type_t) gpiohal.dev->pin[pin].int_type,
    };
    gpio_config(&conf);
}

void EspHal::digitalWrite(uint32_t pin, uint32_t value) {
    if (pin == RADIOLIB_NC)
        return;

    gpio_set_level((gpio_num_t) pin, value);
}

uint32_t EspHal::digitalRead(uint32_t pin) {
    if (pin == RADIOLIB_NC)
        return 0;

    return gpio_get_level((gpio_num_t) pin);
}

void EspHal::attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void), uint32_t mode) {
    if (interruptNum == RADIOLIB_NC)
        return;

    gpio_set_intr_type((gpio_num_t) interruptNum, (gpio_int_type_t) (mode & 0x7));

    // this uses function typecasting, which is not defined when the functions have different signatures
    // untested and might not work
    gpio_isr_handler_add((gpio_num_t) interruptNum, (void (*)(void*))interruptCb, NULL);
}

void EspHal::detachInterrupt(uint32_t interruptNum) {
    if (interruptNum == RADIOLIB_NC)
        return;

    gpio_isr_handler_remove((gpio_num_t) interruptNum);
    gpio_wakeup_disable((gpio_num_t) interruptNum);
    gpio_set_intr_type((gpio_num_t) interruptNum, GPIO_INTR_DISABLE);
}

void EspHal::delay(unsigned long ms) {
    vTaskDelay(ms / portTICK_PERIOD_MS);
}

#define NOP() asm volatile("nop")

void EspHal::delayMicroseconds(unsigned long us) {
    uint64_t m = (uint64_t) esp_timer_get_time();
    if (us) {
        uint64_t e = (m + us);
        if (m > e) { // overflow
            while ((uint64_t) esp_timer_get_time() > e) {
                NOP();
            }
        }
        while ((uint64_t) esp_timer_get_time() < e) {
            NOP();
        }
    }
}

unsigned long EspHal::millis() {
    return ((unsigned long) (esp_timer_get_time() / 1000ULL));
}

unsigned long EspHal::micros() {
    return ((unsigned long) (esp_timer_get_time()));
}

long EspHal::pulseIn(uint32_t pin, uint32_t state, unsigned long timeout) {
    if (pin == RADIOLIB_NC) {
        return (0);
    }

    this->pinMode(pin, INPUT);
    uint32_t start = this->micros();
    uint32_t curtick = this->micros();

    while (this->digitalRead(pin) == state) {
        if ((this->micros() - curtick) > timeout) {
            return (0);
        }
    }

    return (this->micros() - start);
}

void EspHal::spiTransfer(uint8_t* out, size_t len, uint8_t* in) {
    std::lock_guard guard(_mutex);
    spi_transaction_t SPITransaction;
    memset(&SPITransaction, 0, sizeof(spi_transaction_t));
    SPITransaction.length = len * 8;
    SPITransaction.tx_buffer = out;
    SPITransaction.rx_buffer = in;
    spi_device_transmit(_handle, &SPITransaction);
}

#endif