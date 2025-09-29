#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_http_server.h"

static const char *TAG = "esp_ap_http";

/* Ajuste aqui SSID e senha do AP */
#define EXAMPLE_ESP_WIFI_SSID      "MeuESP32_AP"
#define EXAMPLE_ESP_WIFI_PASS      "esp32senha"
#define EXAMPLE_MAX_STA_CONN       4

/* --- Small WiFi event handler for logging --- */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_AP_START:
                ESP_LOGI(TAG, "WIFI_EVENT_AP_START");
                break;
            case WIFI_EVENT_AP_STACONNECTED: {
                wifi_event_ap_staconnected_t* evt = (wifi_event_ap_staconnected_t*) event_data;
                ESP_LOGI(TAG, "Station connected: MAC=%02x:%02x:%02x:%02x:%02x:%02x, AID=%d",
                         evt->mac[0], evt->mac[1], evt->mac[2],
                         evt->mac[3], evt->mac[4], evt->mac[5],
                         evt->aid);
                break;
            }
            case WIFI_EVENT_AP_STADISCONNECTED: {
                wifi_event_ap_stadisconnected_t* evt = (wifi_event_ap_stadisconnected_t*) event_data;
                ESP_LOGI(TAG, "Station disconnected: MAC=%02x:%02x:%02x:%02x:%02x:%02x, AID=%d",
                         evt->mac[0], evt->mac[1], evt->mac[2],
                         evt->mac[3], evt->mac[4], evt->mac[5],
                         evt->aid);
                break;
            }
            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_AP_STAIPASSIGNED) {
            ip_event_ap_staipassigned_t* event = (ip_event_ap_staipassigned_t*) event_data;
            ESP_LOGI(TAG, "Assigned IP to station: " IPSTR, IP2STR(&event->ip));
        }
    }
}

/* --- HTTP handlers --- */
static esp_err_t index_get_handler(httpd_req_t *req)
{
    const char* resp = "<!DOCTYPE html>"
                       "<html><head><meta charset='utf-8'><title>ESP32 AP</title></head>"
                       "<body>"
                       "<h1>Olá do ESP32!</h1>"
                       "<p>Modo: Access Point (SoftAP). IP do AP: 192.168.4.1</p>"
                       "<p><a href=\"/status\">Ver status</a></p>"
                       "</body></html>";
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t status_get_handler(httpd_req_t *req)
{
    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"ssid\":\"%s\",\"max_conn\":%d}", EXAMPLE_ESP_WIFI_SSID, EXAMPLE_MAX_STA_CONN);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.stack_size = 4096;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t index_uri = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = index_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &index_uri);

        httpd_uri_t status_uri = {
            .uri      = "/status",
            .method   = HTTP_GET,
            .handler  = status_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &status_uri);
    } else {
        ESP_LOGE(TAG, "Failed to start HTTP server");
    }
    return server;
}

/* --- Initialize Wi-Fi as SoftAP --- */
static void wifi_init_softap(void)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Create default AP netif */
    esp_netif_create_default_wifi_ap();

    /* Prepare wifi_config and copy strings safely */
    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.ap.ssid, EXAMPLE_ESP_WIFI_SSID, sizeof(wifi_config.ap.ssid) - 1);
    strncpy((char *)wifi_config.ap.password, EXAMPLE_ESP_WIFI_PASS, sizeof(wifi_config.ap.password) - 1);
    wifi_config.ap.ssid_len = 0;
    wifi_config.ap.max_connection = EXAMPLE_MAX_STA_CONN;
    wifi_config.ap.channel = 1;
    wifi_config.ap.ssid_hidden = 0;
    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    } else {
        wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP iniciado. SSID:%s password:%s", EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
}

/* --- app_main --- */
void app_main(void)
{
    esp_err_t ret;

    /* NVS */
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* TCP/IP stack & default event loop */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Register event handlers for WiFi and IP events */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, &wifi_event_handler, NULL));

    /* Start Wi-Fi AP and the webserver */
    wifi_init_softap();

    httpd_handle_t server = start_webserver();
    if (server == NULL) {
        ESP_LOGW(TAG, "Servidor HTTP não iniciado");
    }

    ESP_LOGI(TAG, "AP IP: 192.168.4.1 (DHCP para clientes)");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
