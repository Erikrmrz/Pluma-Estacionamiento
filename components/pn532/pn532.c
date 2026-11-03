#include "pn532.h"
#include <string.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "pn532";

#define SPI_DATAWRITE   0x01
#define SPI_STATREAD    0x02
#define SPI_DATAREAD    0x03

#define TFI_HOST_TO_PN  0xD4
#define TFI_PN_TO_HOST  0xD5

#define CMD_GETFIRMWARE     0x02
#define CMD_RFCONFIGURATION 0x32
#define CMD_SAMCONFIG       0x14
#define CMD_INLISTPASSIVE   0x4A
#define CMD_INRELEASE       0x52

#define XFER_MAX        64      
#define READ_LEN        40      

static spi_device_handle_t s_spi;
static gpio_num_t s_cs = GPIO_NUM_NC;
static uint32_t s_fw;

static inline uint8_t rev8(uint8_t b)
{
    b = (uint8_t)(((b & 0xF0) >> 4) | ((b & 0x0F) << 4));
    b = (uint8_t)(((b & 0xCC) >> 2) | ((b & 0x33) << 2));
    b = (uint8_t)(((b & 0xAA) >> 1) | ((b & 0x55) << 1));
    return b;
}

static esp_err_t xfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
    uint8_t t[XFER_MAX], r[XFER_MAX];
    if (len == 0 || len > XFER_MAX) return ESP_ERR_INVALID_SIZE;
    for (size_t i = 0; i < len; i++) t[i] = rev8(tx[i]);

    spi_transaction_t tr = {
        .length = len * 8,
        .tx_buffer = t,
        .rx_buffer = r,
    };
    gpio_set_level(s_cs, 0);
    esp_rom_delay_us(20);
    esp_err_t e = spi_device_polling_transmit(s_spi, &tr);
    esp_rom_delay_us(5);
    gpio_set_level(s_cs, 1);
    if (e != ESP_OK) return e;

    if (rx) for (size_t i = 0; i < len; i++) rx[i] = rev8(r[i]);
    return ESP_OK;
}

static bool is_ready(void)
{
    uint8_t tx[2] = { SPI_STATREAD, 0x00 }, rx[2] = { 0, 0 };
    if (xfer(tx, rx, 2) != ESP_OK) return false;
    return (rx[1] & 0x01) != 0;
}

static esp_err_t wait_ready(int timeout_ms)
{
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (esp_timer_get_time() < deadline) {
        if (is_ready()) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t read_data(uint8_t *out, size_t n)
{
    uint8_t tx[XFER_MAX] = { 0 }, rx[XFER_MAX];
    if (n + 1 > XFER_MAX) return ESP_ERR_INVALID_SIZE;
    tx[0] = SPI_DATAREAD;
    ESP_RETURN_ON_ERROR(xfer(tx, rx, n + 1), TAG, "read");
    memcpy(out, &rx[1], n);
    return ESP_OK;
}

static esp_err_t write_command(uint8_t cmd, const uint8_t *params, size_t plen)
{
    uint8_t buf[XFER_MAX];
    if (plen + 11 > sizeof buf) return ESP_ERR_INVALID_SIZE;
    uint8_t len = (uint8_t)(plen + 2);              
    size_t n = 0;
    buf[n++] = SPI_DATAWRITE;
    buf[n++] = 0x00;                               
    buf[n++] = 0x00;                                
    buf[n++] = 0xFF;
    buf[n++] = len;
    buf[n++] = (uint8_t)(0x100 - len);             
    buf[n++] = TFI_HOST_TO_PN;
    buf[n++] = cmd;
    uint8_t sum = (uint8_t)(TFI_HOST_TO_PN + cmd);
    for (size_t i = 0; i < plen; i++) {
        buf[n++] = params[i];
        sum = (uint8_t)(sum + params[i]);
    }
    buf[n++] = (uint8_t)(0x100 - sum);             
    buf[n++] = 0x00;                               
    return xfer(buf, NULL, n);
}

static esp_err_t read_ack(void)
{
    static const uint8_t ack[6] = { 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00 };
    uint8_t got[6];
    ESP_RETURN_ON_ERROR(wait_ready(100), TAG, "ack timeout");
    ESP_RETURN_ON_ERROR(read_data(got, sizeof got), TAG, "ack read");
    return memcmp(got, ack, sizeof ack) == 0 ? ESP_OK : ESP_FAIL;
}

static esp_err_t send_cmd(uint8_t cmd, const uint8_t *params, size_t plen,
                          uint8_t *resp, size_t *resp_len, int timeout_ms)
{
    ESP_RETURN_ON_ERROR(write_command(cmd, params, plen), TAG, "write");
    ESP_RETURN_ON_ERROR(read_ack(), TAG, "ack");
    ESP_RETURN_ON_ERROR(wait_ready(timeout_ms), TAG, "resp timeout");

    uint8_t raw[READ_LEN];
    ESP_RETURN_ON_ERROR(read_data(raw, sizeof raw), TAG, "resp read");

    int i = 0;
    while (i < 6 && !(raw[i] == 0x00 && raw[i + 1] == 0xFF)) i++;
    if (i >= 6) return ESP_FAIL;

    uint8_t len = raw[i + 2];
    uint8_t lcs = raw[i + 3];
    if ((uint8_t)(len + lcs) != 0 || len < 2) return ESP_FAIL;
    if ((size_t)i + 4 + len + 1 > sizeof raw) return ESP_ERR_INVALID_SIZE;

    const uint8_t *body = &raw[i + 4];              
    uint8_t dcs = body[len];
    uint8_t sum = 0;
    for (int k = 0; k < len; k++) sum = (uint8_t)(sum + body[k]);
    if ((uint8_t)(sum + dcs) != 0) return ESP_FAIL;
    if (body[0] != TFI_PN_TO_HOST || body[1] != (uint8_t)(cmd + 1)) return ESP_FAIL;

    size_t dlen = (size_t)len - 2;
    if (resp && resp_len) {
        if (dlen > *resp_len) return ESP_ERR_INVALID_SIZE;
        memcpy(resp, &body[2], dlen);
        *resp_len = dlen;
    }
    return ESP_OK;
}

static void wakeup(void)
{
    gpio_set_level(s_cs, 0);
    vTaskDelay(pdMS_TO_TICKS(3));
    gpio_set_level(s_cs, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
}

uint32_t pn532_firmware(void) { return s_fw; }

esp_err_t pn532_init(const pn532_config_t *cfg)
{
    s_cs = cfg->cs;

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << cfg->cs) | (1ULL << cfg->rst),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "gpio");
    gpio_set_level(cfg->cs, 1);

    gpio_set_level(cfg->rst, 0);
    vTaskDelay(pdMS_TO_TICKS(400));
    gpio_set_level(cfg->rst, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    spi_bus_config_t bus = {
        .mosi_io_num = cfg->mosi,
        .miso_io_num = cfg->miso,
        .sclk_io_num = cfg->sck,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = XFER_MAX,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(cfg->host, &bus, SPI_DMA_DISABLED), TAG, "spi bus");

    spi_device_interface_config_t dev = {
        .clock_speed_hz = cfg->clock_hz ? cfg->clock_hz : 1000 * 1000,
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 2,
    };
    ESP_RETURN_ON_ERROR(spi_bus_add_device(cfg->host, &dev, &s_spi), TAG, "spi dev");

    esp_err_t e = ESP_FAIL;
    for (int attempt = 0; attempt < 3; attempt++) {
        wakeup();
        uint8_t fw[4];
        size_t fl = sizeof fw;
        e = send_cmd(CMD_GETFIRMWARE, NULL, 0, fw, &fl, 200);
        if (e == ESP_OK && fl == 4) {
            s_fw = ((uint32_t)fw[0] << 24) | ((uint32_t)fw[1] << 16) | ((uint32_t)fw[2] << 8) | fw[3];
            ESP_LOGI(TAG, "Firmware: IC=0x%02X v%u.%u soporte=0x%02X", fw[0], fw[1], fw[2], fw[3]);
            if (fw[0] != 0x32) {
                ESP_LOGE(TAG, "El chip no es un PN532 (IC=0x%02X)", fw[0]);
                return ESP_ERR_NOT_FOUND;
            }
            break;
        }
        ESP_LOGW(TAG, "Intento %d sin respuesta (%s)", attempt + 1, esp_err_to_name(e));
        e = ESP_ERR_NOT_FOUND;
    }
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "PN532 no responde: revisa SET0=L / SET1=H (SPI), cableado y alimentacion");
        return ESP_ERR_NOT_FOUND;
    }

    const uint8_t sam[3] = { 0x01, 0x14, 0x01 };
    ESP_RETURN_ON_ERROR(send_cmd(CMD_SAMCONFIG, sam, sizeof sam, NULL, NULL, 200), TAG, "SAM");

    const uint8_t retries[4] = { 0x05, 0xFF, 0x01, 0x01 };
    ESP_RETURN_ON_ERROR(send_cmd(CMD_RFCONFIGURATION, retries, sizeof retries, NULL, NULL, 200), TAG, "RFCfg");

    return ESP_OK;
}

esp_err_t pn532_read_uid(pn532_uid_t *uid)
{
    const uint8_t p[2] = { 0x01, 0x00 };           
    uint8_t resp[32];
    size_t rl = sizeof resp;

    esp_err_t e = send_cmd(CMD_INLISTPASSIVE, p, sizeof p, resp, &rl, 300);
    if (e != ESP_OK) return e;
    if (rl < 1) return ESP_FAIL;
    if (resp[0] == 0) return ESP_ERR_NOT_FOUND;    

    if (rl < 6) return ESP_FAIL;
    uint8_t idlen = resp[5];
    if (idlen == 0 || idlen > sizeof uid->bytes || rl < (size_t)6 + idlen) return ESP_FAIL;

    memcpy(uid->bytes, &resp[6], idlen);
    uid->len = idlen;

    const uint8_t rel[1] = { 0x00 };
    (void)send_cmd(CMD_INRELEASE, rel, sizeof rel, NULL, NULL, 100);
    return ESP_OK;
}
