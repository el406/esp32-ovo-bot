#include "cJSON.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/i2c_master.h"
#include "driver/i2s_common.h"
#include "driver/i2s_std.h"
//  #include "driver/dac_continous.h"
#include "../components/i2c_parallel/include/i2s_parallel.h"
#include "driver/i2s_types.h"
#include "driver/ledc.h"
#include "esp_camera.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_websocket_wifi.c"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "hal/i2c_types.h"
#include "hal/ledc_types.h"
#include "lwip/err.h"
#include "portmacro.h"
#include "sensor.h"
#include "soc/clk_tree_defs.h"
#include "soc/gpio_num.h"
#include <stdint.h>
#include <string.h>

char *TAG = "Hallowcam";

// servo pins and constraints
#define SERVO_PIN_1 GPIO_NUM_5
#define SERVO_PIN_2 GPIO_NUM_15

#define SERVO_MIN_PULSEWIDTH                                                   \
  500 // minimum pulse width in microseconds (corresponds to 0 degrees)
#define SERVO_MAX_PULSEWIDTH                                                   \
  2500 // maximum pulse width in microseconds (corresponds to 180 degrees)
#define SERVO_MAX_DEGREE 180 // maximum angle in degrees

// i2c bullshit
#define SDA_PIN GPIO_NUM_21
#define SCL_PIN GPIO_NUM_22

// CAMERA GPIOS
#define D0 GPIO_NUM_12
#define D1 GPIO_NUM_13
#define D2 GPIO_NUM_19
#define D3 GPIO_NUM_18
#define D4 GPIO_NUM_4
#define D5 GPIO_NUM_33
#define D6 GPIO_NUM_2
#define D7 GPIO_NUM_34

#define PCLK_PIN GPIO_NUM_35
#define VSYNC_PIN GPIO_NUM_27
#define HREF_PIN GPIO_NUM_14
#define XCLK_PIN GPIO_NUM_32
// 10 Mhz freq
#define XCLK_FREQ 10000000

// for speaker
#define DAC_1 GPIO_NUM_25
#define DAC_2 GPIO_NUM_26
#define FRAME_WIDTH 160
#define FRAME_HEIGHT 120
#define FRAME_BUF_SIZE (FRAME_WIDTH * FRAME_HEIGHT)

// new camera stuff goes here

#define CAMERA_ADDR 0x21

static i2c_master_bus_handle_t i2c_bus;
static i2c_master_dev_handle_t cam_handle;

static void cam_write(uint8_t addr, uint8_t value) {
  uint8_t buf[2] = {addr, value};
  esp_err_t status = i2c_master_transmit(cam_handle, buf, sizeof(buf),
                                         1000 / portTICK_PERIOD_MS);
  if (status != ESP_OK) {
    ESP_LOGI(TAG, "ESP_LOG ERROR");
  }
}

void cam_init(void) {
  ESP_LOGI("CAM", "Resetting OV7670...");
  cam_write(0x12, 0x80);
  vTaskDelay(pdMS_TO_TICKS(100));

  ESP_LOGI("CAM", "Applying configuration...");
  cam_write(0x12, 0x14); // RGB mode
  cam_write(0x11, 0x01); // Clock prescaler
  cam_write(0x40, 0x10); // RGB565
  cam_write(0x3A, 0x04); // Enable UYVY output
}

static inline void wait_for_vsync_start(void) {
  while (gpio_get_level(VSYNC_PIN) == 0) {
    ;
  }
  while (gpio_get_level(VSYNC_PIN) == 1) {
    ;
  }
}

static lldesc_t dma_desc;
static uint8_t *frame_buf;

static void frame_capture(void *arg) {
  i2s_dev_t *dev = i2s_parallel_get_dev(I2S_NUM_0);

  while (1) {

    wait_for_vsync();

    dev->int_clr.out_eof = 1;

    i2s_parallel_send_dma(I2S_NUM_0, &dma_desc);

    while (dev->int_raw.out_eof == 0) {
      taskYIELD();
    }

    dev->conf.tx_start = 0;
    dev->out_link.stop = 1;

    // send to websocket ig

    vTaskDelay(1);
  }
}

// MAIN PROGRAM
void app_main(void) {

  ESP_LOGI(TAG, "[APP] Startup..");
  ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
  ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

  esp_err_t status = ESP_OK;

  ledc_timer_config_t camera_timer = {.timer_num = LEDC_TIMER_2,
                                      .freq_hz = XCLK_FREQ,
                                      .clk_cfg = LEDC_AUTO_CLK,
                                      .duty_resolution = LEDC_TIMER_1_BIT,
                                      .speed_mode = LEDC_HIGH_SPEED_MODE

  };
  ledc_channel_config_t xclk = {.speed_mode = LEDC_HIGH_SPEED_MODE,
                                .channel = LEDC_CHANNEL_2,
                                .gpio_num = XCLK_PIN,
                                .timer_sel = LEDC_TIMER_2,
                                .hpoint = 0,
                                .duty = 1

  };

  // camera stuff intended of ov7670
  // instead of using esp_camera, use i2c chain and i2s manually

  i2c_master_bus_config_t master_conf = {
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .i2c_port = I2C_NUM_0,
      .sda_io_num = SDA_PIN,
      .scl_io_num = SCL_PIN,
      .glitch_ignore_cnt = 7,
      .flags.enable_internal_pullup = true,
  };
  ESP_ERROR_CHECK(i2c_new_master_bus(&master_conf, &i2c_bus));

  i2c_device_config_t camera_conf = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = CAMERA_ADDR,
      .scl_speed_hz = 100000,
  };
  ESP_ERROR_CHECK(
      i2c_master_bus_add_device(i2c_bus, &camera_conf, &cam_handle));

  ESP_LOGI(TAG, "I2C bus initialized, addr=0x%02X", CAMERA_ADDR);

  // Configure I2S parallel camera mode pins
  i2s_parallel_config_t parallel_cfg = {
      .gpios_bus = {D0, D1, D2, D3, D4, D5, D6, D7, -1, -1, -1, -1,
                    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      .gpio_clk = PCLK_PIN,
      .sample_rate = 0,
      .sample_width = I2S_PARALLEL_WIDTH_8,
  };

  i2s_parallel_driver_install(I2S_NUM_0, &parallel_cfg, 0, NULL, NULL);

  ESP_LOGI("CAM", "I2S camera mode initialized");

  ledc_timer_config(&camera_timer);
  ledc_channel_config(&xclk);

  // set internal pullup
  gpio_set_pull_mode(SDA_PIN, GPIO_PULLUP_ONLY);
  gpio_set_pull_mode(SCL_PIN, GPIO_PULLUP_ONLY);

  // set dma descriptor (i2s parallel part 2)
  frame_buf = heap_caps_malloc(FRAME_BUF_SIZE, MALLOC_CAP_DMA);

  memset(frame_buf, 0, FRAME_BUF_SIZE);

  dma_desc.length = FRAME_BUF_SIZE;
  dma_desc.size = FRAME_BUF_SIZE;
  dma_desc.owner = 1; // DMA owns the buffer
  dma_desc.sosf = 0;
  dma_desc.buf = frame_buf;
  dma_desc.next = NULL;

  vTaskDelay(pdMS_TO_TICKS(200));

  gpio_config_t cam_gpio = {
      .pin_bit_mask =
          (1ULL << VSYNC_PIN) | (1ULL << HREF_PIN) | (1ULL << PCLK_PIN),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&cam_gpio);

  // servo setup
  setup_servo(SERVO_PIN_1, SERVO_PIN_2);
  // ledc timer
  network_start(status);
  // event loop
  xTaskCreate(frame_capture, "capture frame", 4096, NULL, 1, NULL);
}
