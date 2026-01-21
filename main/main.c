#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/twai.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "time.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#ifndef NODE_ID
#define NODE_ID 0x101
#endif

#define TAG "CAN_HTTP_NODE"

// Configuración CAN
#define CAN_TX_GPIO GPIO_NUM_21
#define CAN_RX_GPIO GPIO_NUM_22

// Configuración WiFi - MODIFICA ESTOS VALORES
#define WIFI_SSID      "NOMBRE_DEL_HOTSPOT"
#define WIFI_PASSWORD  "CONTRASENA_DEL_HOTSPOT"
#define MAXIMUM_RETRY  5

// Configuración HTTP
#define HTTP_SERVER_IP "172.18.0.1"
#define HTTP_SERVER_PORT "3000"
#define HTTP_URL "http://" HTTP_SERVER_IP ":" HTTP_SERVER_PORT "/api/items"
#define HTTP_TIMEOUT_MS 5000

// Eventos WiFi
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int sensor_counter = 0;
static EventGroupHandle_t wifi_event_group;
static int s_retry_num = 0;

// Configuración TWAI
twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_GPIO, CAN_RX_GPIO, TWAI_MODE_NORMAL);
twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

// Manejador de eventos WiFi
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Conectando a WiFi...");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Reintentando conexión WiFi... (%d/%d)", s_retry_num, MAXIMUM_RETRY);
        } else {
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "Falló la conexión WiFi después de %d intentos", MAXIMUM_RETRY);
        }
    }
}

static void ip_event_handler(void* arg, esp_event_base_t event_base,
                             int32_t event_id, void* event_data) {
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Conectado a WiFi con IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// Inicializar WiFi
void wifi_init_sta(void) {
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    assert(netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &ip_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi iniciado. SSID: %s", WIFI_SSID);
}

// Esperar conexión WiFi
bool wait_for_wifi_connection(void) {
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi conectado exitosamente");
        return true;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Falló la conexión WiFi");
        return false;
    } else {
        ESP_LOGE(TAG, "Evento WiFi inesperado");
        return false;
    }
}

// Función para enviar datos HTTP
void send_http_data(int sensor_value) {
    char json_buffer[256];
    char sensor_id[20];
    
    // Verificar conexión WiFi
    if (!(xEventGroupGetBits(wifi_event_group) & WIFI_CONNECTED_BIT)) {
        ESP_LOGW(TAG, "WiFi no conectado, no se puede enviar datos HTTP");
        return;
    }
    
    // Generar ID del sensor
    sprintf(sensor_id, "sensor_%d", sensor_counter);
    
    // Crear JSON
    snprintf(json_buffer, sizeof(json_buffer),
             "{\"id\":\"%s\", \"value\":%d, \"unit\":\"m\", \"type\":\"distance\"}",
             sensor_id, sensor_value);
    
    ESP_LOGI(TAG, "Enviando JSON: %s", json_buffer);
    
    // Configurar cliente HTTP
    esp_http_client_config_t config = {
        .url = HTTP_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .disable_auto_redirect = false,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Error al inicializar cliente HTTP");
        return;
    }
    
    // Establecer headers
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Accept", "application/json");
    
    // Establecer datos POST
    esp_http_client_set_post_field(client, json_buffer, strlen(json_buffer));
    
    // Ejecutar petición
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP Status = %d", status_code);
        
        if (status_code >= 200 && status_code < 300) {
            ESP_LOGI(TAG, "Datos enviados exitosamente");
        } else {
            ESP_LOGW(TAG, "Respuesta HTTP no exitosa: %d", status_code);
        }
    } else {
        ESP_LOGE(TAG, "Error en petición HTTP: %s", esp_err_to_name(err));
    }
    
    // Limpiar
    esp_http_client_cleanup(client);
    sensor_counter++;
}

// Tarea para enviar datos CAN y HTTP
void sender_task(void *arg) {
    uint8_t counter = 0;
    
    // Esperar a que WiFi esté conectado antes de enviar HTTP
    if (!wait_for_wifi_connection()) {
        ESP_LOGE(TAG, "No se puede iniciar sender_task - WiFi no disponible");
        vTaskDelete(NULL);
        return;
    }
    
    while (1) {
        // Enviar mensaje CAN
        twai_message_t msg = {0};
        msg.identifier = NODE_ID;
        msg.data_length_code = 2;
        msg.data[0] = counter;
        msg.data[1] = counter + 1;
        
        esp_err_t res = twai_transmit(&msg, pdMS_TO_TICKS(1000));
        if (res == ESP_OK) {
            ESP_LOGI(TAG, "TX (ID=0x%lX): [%u, %u]", 
                     (unsigned long)msg.identifier, msg.data[0], msg.data[1]);
        } else {
            ESP_LOGE(TAG, "TX ERROR: 0x%X", res);
        }
        
        // Enviar datos HTTP (cada 5 segundos)
        if (counter % 5 == 0) {
            // Generar valor aleatorio entre 20 y 29
            int random_value = 20 + (esp_random() % 10);
            send_http_data(random_value);
        }
        
        // Monitorear estado del bus CAN
        twai_status_info_t status;
        twai_get_status_info(&status);
        ESP_LOGI(TAG, "CAN State=%u, TEC=%u, REC=%u",
                 (unsigned int)status.state,
                 (unsigned int)status.tx_error_counter,
                 (unsigned int)status.rx_error_counter);
        
        counter++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Tarea para recibir datos CAN
void receiver_task(void *arg) {
    twai_message_t msg;
    
    while (1) {
        if (twai_receive(&msg, pdMS_TO_TICKS(2000)) == ESP_OK) {
            // Ignorar mensajes propios
            if (msg.identifier == NODE_ID) {
                continue;
            }
            
            ESP_LOGI(TAG, "RX (ID=0x%lX): ", (unsigned long)msg.identifier);
            for (int i = 0; i < msg.data_length_code; i++) {
                printf("%u ", msg.data[i]);
            }
            printf("\n");
            
            // Monitorear estado del bus CAN
            twai_status_info_t status;
            twai_get_status_info(&status);
            ESP_LOGI(TAG, "CAN RX State=%u, TEC=%u, REC=%u",
                     (unsigned int)status.state,
                     (unsigned int)status.tx_error_counter,
                     (unsigned int)status.rx_error_counter);
        }
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Nodo CAN/HTTP arrancando con NODE_ID=0x%X", NODE_ID);
    ESP_LOGI(TAG, "Servidor HTTP: %s", HTTP_URL);
    
    // Inicializar NVS (necesario para WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Inicializar WiFi
    ESP_LOGI(TAG, "Iniciando WiFi...");
    wifi_init_sta();
    
    // Inicializar generador de números aleatorios
    srand(time(NULL));
    
    // Inicializar driver TWAI
    ESP_ERROR_CHECK(twai_driver_install(&g_config, &t_config, &f_config));
    ESP_ERROR_CHECK(twai_start());
    
    ESP_LOGI(TAG, "Esperando conexión WiFi...");
    
    // Crear tareas después de conectar WiFi
    if (wait_for_wifi_connection()) {
        ESP_LOGI(TAG, "WiFi conectado, creando tareas...");
        xTaskCreate(sender_task, "sender_task", 4096, NULL, 5, NULL);
        xTaskCreate(receiver_task, "receiver_task", 4096, NULL, 5, NULL);
        ESP_LOGI(TAG, "Sistema iniciado correctamente");
    } else {
        ESP_LOGE(TAG, "No se pudo conectar a WiFi. Solo funcionará CAN");
        // Solo iniciar tarea CAN si no hay WiFi
        xTaskCreate(receiver_task, "receiver_task", 4096, NULL, 5, NULL);
    }
}