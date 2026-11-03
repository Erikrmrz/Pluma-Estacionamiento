#include "rc522.h"
#include <string.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "rc522";

#define CommandReg      0x01
#define ComIrqReg       0x04
#define DivIrqReg       0x05
#define ErrorReg        0x06
#define FIFODataReg     0x09
#define FIFOLevelReg    0x0A
#define ControlReg      0x0C
#define BitFramingReg   0x0D
#define CollReg         0x0E
#define ModeReg         0x11
#define TxControlReg    0x14
#define TxASKReg        0x15
#define CRCResultRegH   0x21
#define CRCResultRegL   0x22
#define TModeReg        0x2A
#define TPrescalerReg   0x2B
#define TReloadRegH     0x2C
#define TReloadRegL     0x2D
#define VersionReg      0x37

#define CMD_IDLE        0x00
#define CMD_CALCCRC     0x03
#define CMD_TRANSCEIVE  0x0C
#define CMD_SOFTRESET   0x0F

static spi_device_handle_t s_spi;

static uint8_t rd(uint8_t reg)
{
    spi_transaction_t t = { .flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA, .length = 16 };
    t.tx_data[0] = (uint8_t)(((reg << 1) & 0x7E) | 0x80);
    t.tx_data[1] = 0x00;
    spi_device_polling_transmit(s_spi, &t);
    return t.rx_data[1];
}

static void wr(uint8_t reg, uint8_t val)
{
    spi_transaction_t t = { .flags = SPI_TRANS_USE_TXDATA, .length = 16 };
    t.tx_data[0] = (uint8_t)((reg << 1) & 0x7E);
    t.tx_data[1] = val;
    spi_device_polling_transmit(s_spi, &t);
}

static void set_bits(uint8_t reg, uint8_t mask)   { wr(reg, rd(reg) | mask); }
static void clear_bits(uint8_t reg, uint8_t mask) { wr(reg, rd(reg) & (uint8_t)~mask); }

static esp_err_t calc_crc(const uint8_t *data, size_t len, uint8_t out[2])
{
    wr(CommandReg, CMD_IDLE);
    wr(DivIrqReg, 0x04);            
    wr(FIFOLevelReg, 0x80);         
    for (size_t i = 0; i < len; i++) wr(FIFODataReg, data[i]);
    wr(CommandReg, CMD_CALCCRC);

    int64_t deadline = esp_timer_get_time() + 90000;
    while (esp_timer_get_time() < deadline) {
        if (rd(DivIrqReg) & 0x04) {
            wr(CommandReg, CMD_IDLE);
            out[0] = rd(CRCResultRegL);
            out[1] = rd(CRCResultRegH);
            return ESP_OK;
        }
    }
    wr(CommandReg, CMD_IDLE);
    return ESP_ERR_TIMEOUT;
}

static esp_err_t transceive(const uint8_t *tx, size_t tx_len,
                            uint8_t *rx, size_t *rx_len,
                            uint8_t tx_last_bits, uint8_t *rx_last_bits)
{
    wr(CommandReg, CMD_IDLE);
    wr(ComIrqReg, 0x7F);            
    wr(FIFOLevelReg, 0x80);
    for (size_t i = 0; i < tx_len; i++) wr(FIFODataReg, tx[i]);
    wr(BitFramingReg, tx_last_bits & 0x07);
    wr(CommandReg, CMD_TRANSCEIVE);
    set_bits(BitFramingReg, 0x80);  

    int64_t deadline = esp_timer_get_time() + 40000;
    uint8_t irq = 0;
    while (esp_timer_get_time() < deadline) {
        irq = rd(ComIrqReg);
        if (irq & 0x31) break;     
    }
    clear_bits(BitFramingReg, 0x80);

    if (!(irq & 0x30)) return ESP_ERR_TIMEOUT;    

    uint8_t err = rd(ErrorReg);
    if (err & 0x08) return ESP_ERR_INVALID_STATE; 
    if (err & 0x13) return ESP_FAIL;               

    uint8_t n = rd(FIFOLevelReg);
    if (n > *rx_len) {
        *rx_len = n;
        return ESP_ERR_INVALID_SIZE;
    }
    for (uint8_t i = 0; i < n; i++) rx[i] = rd(FIFODataReg);
    *rx_len = n;
    if (rx_last_bits) *rx_last_bits = rd(ControlReg) & 0x07;
    return ESP_OK;
}

static esp_err_t request_a(void)
{
    clear_bits(CollReg, 0x80);
    uint8_t cmd = 0x26;             
    uint8_t atqa[2];
    size_t n = sizeof atqa;
    uint8_t last = 0;
    esp_err_t e = transceive(&cmd, 1, atqa, &n, 7, &last);
    if (e != ESP_OK) return e;
    return (n == 2 && last == 0) ? ESP_OK : ESP_FAIL;
}

static void halt_a(void)
{
    uint8_t h[4] = { 0x50, 0x00, 0, 0 };
    if (calc_crc(h, 2, &h[2]) != ESP_OK) return;
    uint8_t dummy[4];
    size_t n = sizeof dummy;
    (void)transceive(h, 4, dummy, &n, 0, NULL);   
}

esp_err_t rc522_read_uid(rc522_uid_t *uid)
{
    esp_err_t e = request_a();
    if (e == ESP_ERR_TIMEOUT) return ESP_ERR_NOT_FOUND;
    if (e != ESP_OK) return e;

    static const uint8_t sel_cmd[2] = { 0x93, 0x95 };   
    uint8_t collected[7];
    size_t clen = 0;
    bool done = false;

    for (int level = 0; level < 2 && !done; level++) {
        uint8_t ac[2] = { sel_cmd[level], 0x20 };
        uint8_t resp[5];
        size_t n = sizeof resp;
        e = transceive(ac, sizeof ac, resp, &n, 0, NULL);
        if (e != ESP_OK || n != 5) return ESP_FAIL;
        if ((uint8_t)(resp[0] ^ resp[1] ^ resp[2] ^ resp[3]) != resp[4]) return ESP_FAIL;

        uint8_t sel[9] = { sel_cmd[level], 0x70, resp[0], resp[1], resp[2], resp[3], resp[4], 0, 0 };
        if (calc_crc(sel, 7, &sel[7]) != ESP_OK) return ESP_FAIL;
        uint8_t sak[3];
        n = sizeof sak;
        e = transceive(sel, sizeof sel, sak, &n, 0, NULL);
        if (e != ESP_OK || n != 3) return ESP_FAIL;

        if (resp[0] == 0x88) {                      
            memcpy(&collected[clen], &resp[1], 3);
            clen += 3;
        } else {
            memcpy(&collected[clen], resp, 4);
            clen += 4;
            done = true;
        }
    }
    if (!done) return ESP_FAIL;                     

    memcpy(uid->bytes, collected, clen);
    uid->len = (uint8_t)clen;
    halt_a();                                      
    return ESP_OK;
}

uint8_t rc522_version(void) { return rd(VersionReg); }

esp_err_t rc522_init(const rc522_config_t *cfg)
{
    /* Pin RST */
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << cfg->rst,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "gpio rst");
    gpio_set_level(cfg->rst, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(cfg->rst, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    /* Bus y dispositivo SPI */
    spi_bus_config_t bus = {
        .mosi_io_num = cfg->mosi,
        .miso_io_num = cfg->miso,
        .sclk_io_num = cfg->sck,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(cfg->host, &bus, SPI_DMA_DISABLED), TAG, "spi bus");

    spi_device_interface_config_t dev = {
        .clock_speed_hz = cfg->clock_hz ? cfg->clock_hz : 4 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = cfg->cs,
        .queue_size = 2,
    };
    ESP_RETURN_ON_ERROR(spi_bus_add_device(cfg->host, &dev, &s_spi), TAG, "spi dev");

    /* Reset por software y configuracion estandar */
    wr(CommandReg, CMD_SOFTRESET);
    vTaskDelay(pdMS_TO_TICKS(50));

    wr(TModeReg, 0x80);             
    wr(TPrescalerReg, 0xA9);
    wr(TReloadRegH, 0x03);          
    wr(TReloadRegL, 0xE8);
    wr(TxASKReg, 0x40);            
    wr(ModeReg, 0x3D);             
    set_bits(TxControlReg, 0x03);   

    uint8_t v = rc522_version();
    ESP_LOGI(TAG, "VersionReg = 0x%02X", v);
    if (v == 0x00 || v == 0xFF) {
        ESP_LOGE(TAG, "RC522 no responde: revisa cableado SPI, 3.3V, GND y CS");
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}
