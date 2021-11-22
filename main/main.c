/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "esp_camera.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "drawfont.h"

#define BOARD_ESP32CAM_AITHINKER

/* Values from menuconfig */
#define WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define WIFI_MAX_RETRY CONFIG_ESP_MAXIMUM_RETRY
#define MQTT_URI       CONFIG_ESP_MQTT_HOST_URI
#define MQTT_TOPIC     CONFIG_ESP_MQTT_TOPIC

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "cam2mqtt";

static int s_retry_num = 0;

RTC_DATA_ATTR static int seq_num = 0;
RTC_DATA_ATTR static int countdown = 0;

static void
wifi_event_handler(void *arg, esp_event_base_t event_base,
           int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        }
        else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "connect to the AP fail");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

bool wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                ESP_EVENT_ANY_ID,
                                &wifi_event_handler,
                                NULL,
                                &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                IP_EVENT_STA_GOT_IP,
                                &wifi_event_handler,
                                NULL,
                                &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. Incase your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,

            .pmf_cfg = {
                    .capable = true,
                    .required = false},
            },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                           WIFI_CONNECTED_BIT |
                           WIFI_FAIL_BIT,
                           pdFALSE,
                           pdFALSE,
                           portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    bool result = false;
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s", WIFI_SSID, WIFI_PASS);
        result = true;
    }
    else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s", WIFI_SSID, WIFI_PASS);
    }
    else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    /* The event will not be processed after unregister */
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    vEventGroupDelete(s_wifi_event_group);

    return result;
}

bool init_wifi(void)
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    return wifi_init_sta();
}

/* ---- */

const esp_mqtt_client_config_t mqtt_cfg = {
    .uri = MQTT_URI,
};

static esp_mqtt_client_handle_t mqtt_client;

void init_mqtt(void)
{
    ESP_LOGI(TAG, "Init MQTT");
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    // esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(mqtt_client);
}

/* ---- */

// ESP32Cam (AiThinker) PIN Map
#ifdef BOARD_ESP32CAM_AITHINKER

#define BLINK_GPIO 33
#define FLASHLIGHT_GPIO 4

#define CAM_PIN_PWDN 32
#define CAM_PIN_RESET -1    //software reset will be performed
#define CAM_PIN_XCLK 0
#define CAM_PIN_SIOD 26
#define CAM_PIN_SIOC 27

#define CAM_PIN_D7 35
#define CAM_PIN_D6 34
#define CAM_PIN_D5 39
#define CAM_PIN_D4 36
#define CAM_PIN_D3 21
#define CAM_PIN_D2 19
#define CAM_PIN_D1 18
#define CAM_PIN_D0 5
#define CAM_PIN_VSYNC 25
#define CAM_PIN_HREF 23
#define CAM_PIN_PCLK 22

#endif

static camera_config_t camera_config = {
    .pin_pwdn = CAM_PIN_PWDN,
    .pin_reset = CAM_PIN_RESET,
    .pin_xclk = CAM_PIN_XCLK,
    .pin_sscb_sda = CAM_PIN_SIOD,
    .pin_sscb_scl = CAM_PIN_SIOC,

    .pin_d7 = CAM_PIN_D7,
    .pin_d6 = CAM_PIN_D6,
    .pin_d5 = CAM_PIN_D5,
    .pin_d4 = CAM_PIN_D4,
    .pin_d3 = CAM_PIN_D3,
    .pin_d2 = CAM_PIN_D2,
    .pin_d1 = CAM_PIN_D1,
    .pin_d0 = CAM_PIN_D0,
    .pin_vsync = CAM_PIN_VSYNC,
    .pin_href = CAM_PIN_HREF,
    .pin_pclk = CAM_PIN_PCLK,

    //XCLK 20MHz or 10MHz for OV2640 double FPS (Experimental)
    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_GRAYSCALE,    //YUV422,GRAYSCALE,RGB565,JPEG
    .frame_size = FRAMESIZE_VGA,    //QQVGA-UXGA Do not use sizes above QVGA when not JPEG

    .jpeg_quality = 12,    //0-63 lower number means higher quality
    .fb_count = 1        //if more than one, i2s runs in continuous mode. Use only with JPEG
};

static esp_err_t init_camera()
{
    //initialize the camera
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera Init Failed");
        return err;
    }

    // sensor_t *s = esp_camera_sensor_get();
    // s->set_exposure_ctrl(s, 1);

    return ESP_OK;
}

void init_gpio()
{
    gpio_pad_select_gpio(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);

    // gpio_pad_select_gpio(FLASHLIGHT_GPIO);
    // gpio_set_direction(FLASHLIGHT_GPIO, GPIO_MODE_OUTPUT);
    // gpio_set_level(FLASHLIGHT_GPIO, 0);  // off
}

void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Notification of a time synchronization event");
}

static void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    sntp_init();
}

static void sync_time(void)
{
    initialize_sntp();

    // wait for time to be set
    // time_t now = 0;
    // struct tm timeinfo = { 0 };
    int retry = 0;
    const int retry_count = 20;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
    // time(&now);
    // setenv("TZ", "UTC-9", 1);
    // tzset();
    // localtime_r(&now, &timeinfo);
}

struct tm get_current_time(void)
{
    setenv("TZ", "UTC-9", 1);    // means UTC+9
    tzset();

    sync_time();

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    return timeinfo;
}

void draw_info_string(uint8_t * fb_buf, const char *str)
{
    // VGA
    int cam_w = 640;
    int cam_h = 480;

    set_defaultfont();
    draw_string(fb_buf, cam_w, cam_h, 8 - 1, 8, str, FONTCOLOR_BLACK);
    draw_string(fb_buf, cam_w, cam_h, 8 + 1, 8, str, FONTCOLOR_BLACK);
    draw_string(fb_buf, cam_w, cam_h, 8, 8 - 1, str, FONTCOLOR_BLACK);
    draw_string(fb_buf, cam_w, cam_h, 8, 8 + 1, str, FONTCOLOR_BLACK);
    draw_string(fb_buf, cam_w, cam_h, 8, 8, str, FONTCOLOR_WHITE);
}

int next_countdown(int current_hour)
{
    return 23 - current_hour + 8;
}

size_t capture_frame(uint8_t** buf, struct tm timeinfo)
{
    char strftime_buf[32];
    strftime(strftime_buf, sizeof(strftime_buf), "%Y/%m/%d(%a) %T", &timeinfo);
    ESP_LOGI(TAG, "The current date/time is: %s", strftime_buf);

    // gpio_set_level(FLASHLIGHT_GPIO, 1);  // on
    ESP_LOGI(TAG, "Taking picture...");
    init_camera();
    vTaskDelay(5000 / portTICK_PERIOD_MS);

    camera_fb_t *fb;
    for (int i = 0; i < 5; ++i) {
        fb = esp_camera_fb_get();
        esp_camera_fb_return(fb);
        vTaskDelay(30 / portTICK_PERIOD_MS);
    }
    fb = esp_camera_fb_get();

    int nextcd = next_countdown(timeinfo.tm_hour);
    char strinfo_buf[92];
    sprintf(strinfo_buf, "%s seq: %03d cd: %02d", strftime_buf, ++seq_num, nextcd);
    draw_info_string(fb->buf, strinfo_buf);

    size_t buf_len = 0;
    bool converted = frame2jpg(fb, 80, buf, &buf_len);
    esp_camera_fb_return(fb);
    esp_camera_deinit();

    if (!converted) {
        buf_len = 0;
    }

    return buf_len;
}

void app_main(void)
{
    bool is_wifi_connected = false;

    init_gpio();
    gpio_set_level(BLINK_GPIO, 0);

    //ESP_LOGI(TAG, "Initialized with countdown %d", countdown);
    if (countdown > 0) {
        countdown -= 1;
        goto deepsleep;
    }

    ESP_LOGI(TAG, "Connecting to WiFi and getting time over NTP.");
    is_wifi_connected = init_wifi();
    if (is_wifi_connected == false) {
        goto deepsleep;
    }

    struct tm timeinfo = get_current_time();

    ESP_LOGI(TAG, "Taking picture...");
    uint8_t *buf = NULL;
    size_t buf_len = capture_frame(&buf, timeinfo);

    if (buf_len == 0) {
        goto deepsleep;
    }

    init_mqtt();
    ESP_LOGI(TAG, "Sending it to MQTT topic %s ...", MQTT_TOPIC);
    esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC, (const char *)(buf), buf_len, 0, 0);

    countdown = next_countdown(timeinfo.tm_hour);

 deepsleep:
    gpio_set_level(BLINK_GPIO, 1);

    esp_mqtt_client_destroy(mqtt_client);
    if (is_wifi_connected) {
        esp_wifi_stop();
        is_wifi_connected = false;
    }

    const uint32_t deep_sleep_sec = 1 * 60 * 60;
    ESP_LOGI(TAG, "Entering deep sleep for %d seconds, countdown: %d", deep_sleep_sec, countdown);
    esp_deep_sleep(1000000ULL * deep_sleep_sec);
}
