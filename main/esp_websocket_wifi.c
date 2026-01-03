#include "cJSON.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "portmacro.h"
#include "servo.c"
#include <esp_websocket_client.h>
#include <stdint.h>
#include <string.h>

#define WIFI_SSID "CoontazNew"
#define WIFI_PASS "10minutesinSeoul"

#define WIFI_SUCCESS 1 << 0
#define WIFI_FAILURE 1 << 1
#define TCP_SUCCESS 1 << 0
#define TCP_FAILURE 1 << 1
#define MAX_FAILURES 10

static int s_retry_num = 0;

#define WEBSOCKET_URI "ws://192.168.1.158:8080"

// event groups and client handle
static esp_websocket_client_handle_t client;
static EventGroupHandle_t network_event_group;
// websocket shit
#define NO_DATA_TIMELIMIT 120

static TimerHandle_t shutdown_signal_timer;
static SemaphoreHandle_t shutdown_sema;

extern char *TAG;

static void shutdown_signaler(TimerHandle_t xTimer) {
  ESP_LOGI(TAG, "No data received for %d seconds, signaling shutdown",
           NO_DATA_TIMELIMIT);
  xSemaphoreGive(shutdown_sema);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    ESP_LOGI(TAG, "Connecting to AP...");
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    if (s_retry_num < MAX_FAILURES) {
      ESP_LOGI(TAG, "Reconnecting to AP...");
      esp_wifi_connect();
      s_retry_num++;
    } else {
      xEventGroupSetBits(network_event_group, WIFI_FAILURE);
    }
  }
}

// event handler for ip events
static void ip_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data) {
  if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "STA IP: " IPSTR, IP2STR(&event->ip_info.ip));
    s_retry_num = 0;
    xEventGroupSetBits(network_event_group, WIFI_SUCCESS);
  }
}

static void websocket_event_handler(void *handler_args, esp_event_base_t base,
                                    int32_t event_id, void *event_data) {
  esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
  switch (event_id) {
  case WEBSOCKET_EVENT_CONNECTED:
    ESP_LOGI(TAG, "WEBSOCKET_EVENT_CONNECTED");
    char msg[32];
    if (esp_websocket_client_is_connected(client)) {
      int len = sprintf(msg, "ID_ESP");
      esp_websocket_client_send_text(client, msg, len, portMAX_DELAY);
    }
    break;
  case WEBSOCKET_EVENT_DISCONNECTED:
    ESP_LOGI(TAG, "WEBSOCKET_EVENT_DISCONNECTED");
    break;
  case WEBSOCKET_EVENT_DATA:
    ESP_LOGI(TAG, "WEBSOCKET_EVENT_DATA");
    ESP_LOGI(TAG, "Received opcode=%d", data->op_code);
    ESP_LOGW(TAG, "Received=%.*s", data->data_len, (char *)data->data_ptr);
    ESP_LOGW(
        TAG,
        "Total payload length=%d, data_len=%d, current payload offset=%d\r\n",
        data->payload_len, data->data_len, data->payload_offset);

    // format as json and read data
    if (data->data_len != 0) {
      cJSON *payload = cJSON_Parse((char *)data->data_ptr);
      char *controller = cJSON_GetObjectItem(payload, "control")->valuestring;
      if (strcmp(controller, "SERVO1") == 0) {
        run_servo(cJSON_GetObjectItem(payload, "value")->valueint, 0);
        LOGI(TAG, "RAN SERVO1");
      }
      if (strcmp(controller, "SERVO2") == 0) {
        run_servo(cJSON_GetObjectItem(payload, "value")->valueint, 1);
        LOGI(TAG, "RAN SERVO2");
      }
      // and if the id is DAC than the speakers should play the correct audio
    }
    xTimerReset(shutdown_signal_timer, portMAX_DELAY);
    break;
  case WEBSOCKET_EVENT_ERROR:
    ESP_LOGI(TAG, "WEBSOCKET_EVENT_ERROR");
    break;
  }
}

esp_err_t setup_wifi(void) {

  esp_err_t status = nvs_flash_init();
  if (status == ESP_ERR_NVS_NO_FREE_PAGES ||
      status == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    status = nvs_flash_init();
  }
  ESP_ERROR_CHECK(status);

  esp_netif_init();
  esp_event_loop_create_default();
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t init_conf = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&init_conf));
  status = WIFI_FAILURE;
  wifi_config_t conf = {.sta = {
                            .ssid = WIFI_SSID,
                            .password = WIFI_PASS,
                        }};
  ESP_LOGI(TAG, "configuring...");
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &conf));

  network_event_group = xEventGroupCreate();
  esp_event_handler_instance_t wifi_handler_event_instance;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL,
      &wifi_handler_event_instance));

  esp_event_handler_instance_t got_ip_event_instance;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL,
      &got_ip_event_instance));

  // start driver
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_LOGI(TAG, "STA initialization complete");

  EventBits_t bits =
      xEventGroupWaitBits(network_event_group, WIFI_SUCCESS | WIFI_FAILURE,
                          pdFALSE, pdFALSE, portMAX_DELAY);

  if (bits & WIFI_SUCCESS) {
    ESP_LOGI(TAG, "Connected to ap");
    status = WIFI_SUCCESS;
  } else if (bits & WIFI_FAILURE) {
    ESP_LOGI(TAG, "Failed to connect to ap");
    status = WIFI_FAILURE;
  } else {
    ESP_LOGE(TAG, "UNEXPECTED EVENT");
    status = WIFI_FAILURE;
  }
  /*  ESP_ERROR_CHECK(esp_event_handler_instance_unregister(
        IP_EVENT, IP_EVENT_STA_GOT_IP, got_ip_event_instance));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_handler_event_instance));
    vEventGroupDelete(network_event_group);*/

  return status;
}

void setup_websocket_client(void) {
  const esp_websocket_client_config_t ws_cfg = {
      .uri = WEBSOCKET_URI,
  };

  shutdown_signal_timer = xTimerCreate(
      "Websocket shutdown timer", NO_DATA_TIMELIMIT * 1000 / portTICK_PERIOD_MS,
      pdFALSE, NULL, shutdown_signaler);
  shutdown_sema = xSemaphoreCreateBinary();
  ESP_LOGI(TAG, "Connecting to %s...", ws_cfg.uri);

  client = esp_websocket_client_init(&ws_cfg);
  esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY,
                                websocket_event_handler, (void *)client);

  esp_websocket_client_start(client);

  xTimerStart(shutdown_signal_timer, portMAX_DELAY);
  vTaskDelay(1000 / portTICK_PERIOD_MS);
  ESP_LOGI(TAG, "websocket setup complete");

  /*
  xSemaphoreTake(shutdown_sema, portMAX_DELAY);
  esp_websocket_client_stop(client);
  ESP_LOGI(TAG, "Websocket Stopped");
  esp_websocket_client_destroy(client);*/
}

void network_start(esp_err_t status) {
  // System initialization
  ESP_LOGI(TAG, "Setting up WIFI...");
  status = setup_wifi();
  if (status != WIFI_SUCCESS) {
    ESP_LOGI(TAG, "WIFI FAILED");
    return;
  }

  ESP_LOGI(TAG, "Setting up websocket...");
  setup_websocket_client();
  esp_log_level_set("*", ESP_LOG_INFO);
  esp_log_level_set("WEBSOCKET_CLIENT", ESP_LOG_DEBUG);
}

void send_binary(char *data, int len) {
  ESP_ERROR_CHECK(
      esp_websocket_client_send_bin(client, data, len, portMAX_DELAY));
}
