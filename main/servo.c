#include "cJSON.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "hal/ledc_types.h"
#include "soc/clk_tree_defs.h"
#include "soc/gpio_num.h"
#include <stdint.h>
#include <string.h>

#define SERVO_MIN_PULSEWIDTH                                                   \
  500 // minimum pulse width in microseconds (corresponds to 0 degrees)
#define SERVO_MAX_PULSEWIDTH                                                   \
  2500 // maximum pulse width in microseconds (corresponds to 180 degrees)
#define SERVO_MAX_DEGREE 180 // maximum angle in degrees

// servo pins and constraints
void setup_servo(gpio_num_t servo_gpio1, gpio_num_t servo_gpio2) {
  // servo setup
  // ledc timer
  ledc_timer_config_t timer_cfg = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .duty_resolution = LEDC_TIMER_16_BIT,
      .timer_num = LEDC_TIMER_0,
      .freq_hz = 50,
      .clk_cfg = LEDC_AUTO_CLK,
  };

  // servo one
  ledc_channel_config_t servo1 = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .channel = LEDC_CHANNEL_0,
      .timer_sel = LEDC_TIMER_0,
      .intr_type = LEDC_INTR_DISABLE,
      .gpio_num = servo_gpio1,
      .duty = 0,
      .hpoint = 0,
  };
  // servo one
  ledc_channel_config_t servo2 = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .channel = LEDC_CHANNEL_1,
      .timer_sel = LEDC_TIMER_0,
      .intr_type = LEDC_INTR_DISABLE,
      .gpio_num = servo_gpio2,
      .duty = 0,
      .hpoint = 0,
  };

  ledc_timer_config(&timer_cfg);
  ledc_channel_config(&servo1);
  ledc_channel_config(&servo2);
}

// motor functions
//
void run_servo(int angle, int ledChannel) {
  uint32_t duty_us = (uint32_t)(SERVO_MIN_PULSEWIDTH +
                                (angle / 180.0) * (SERVO_MAX_PULSEWIDTH -
                                                   SERVO_MIN_PULSEWIDTH));
  uint32_t duty =
      (uint32_t)(((float)duty_us / 20000.0) * (1 << LEDC_TIMER_16_BIT));
  // 4. Set the new PWM duty cycle
  ledc_set_duty(LEDC_LOW_SPEED_MODE, ledChannel, duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, ledChannel);
}
