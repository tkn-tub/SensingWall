/*  WiFi softAP Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "rom/ets_sys.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_private/wifi.h"
#include "freertos/event_groups.h"
#include "esp_netif.h"
#include "esp_mac.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "../../common/csi.h"

/*
 * The implementation of the SensingWall Access Point (AP)
 *
 * Note: configuration via project configuration menu.
 *
*/
#define SENSING_WALL_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define SENSING_WALL_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define SENSING_WALL_WIFI_CHANNEL   CONFIG_ESP_WIFI_CHANNEL
#define SENSING_WALL_MAX_STA_CONN       CONFIG_ESP_MAX_STA_CONN
#define SENSING_WALL_DB_HOST_MAC        CONFIG_DB_HOST_MAC
#define SENSING_WALL_DB_HOST_IP         CONFIG_DB_HOST_IP
#define SENSING_WALL_IP_MCAST_INTERVAL  CONFIG_IP_MCAST_INTERVAL

#define UDP_PORT 4950
#define UDP_PORT_HOST 4951

// queue for static_wifi_csi_info_t structs
QueueHandle_t queue;

const char *TAG = "sensing_wall_ap";

/** Get the list of connected client stations; i.e. the nodes from sensing wall */
static int connected_stations()
{
    wifi_sta_list_t list;
    esp_wifi_ap_get_sta_list(&list);

    for (int i = 0; i < list.num; i++) {
        char sta_mac[20] = {0};
        sprintf(sta_mac, MACSTR, MAC2STR(list.sta[i].mac));

        if (strcasecmp(sta_mac, SENSING_WALL_DB_HOST_MAC)) {
            continue;
        }
        return list.num > 1 ? 1 : 0;
    }
    return 0;
}

/** Called whenever a new CSI is estimated from received frame */
static void wifi_csi_rx_cb(void *ctx, wifi_csi_info_t *info)
{
    if (!info || !info->buf || !info->mac) {
        ESP_LOGW(TAG, "<%s> wifi_csi_cb", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return;
    }

    char src_mac[20] = {0}; // the source node from which CSI was estimated
    sprintf(src_mac, MACSTR, MAC2STR(info->mac));

    wifi_sta_list_t list;
    esp_wifi_ap_get_sta_list(&list);

    uint8_t store_csi = 0;
    for (int i = 0; i < list.num; i++) {
        char sta_mac[20] = {0};
        sprintf(sta_mac, MACSTR, MAC2STR(list.sta[i].mac));

        if (strcasecmp(sta_mac, src_mac)) {
            continue;
        }
        if (!strcasecmp(sta_mac, SENSING_WALL_DB_HOST_MAC)) {
            continue;
        }
        store_csi = 1;
    }

    if (!store_csi) {
        return; // in case the source was neither an associated station nor the server node
    }

    //const wifi_pkt_rx_ctrl_t *rx_ctrl = &info->rx_ctrl;

    /** Only LLTF sub-carriers are selected. */
    info->len = 128;

    // copy data to new struct
    static_wifi_csi_info_t csi;

    csi.x = *info;
    csi.x.buf = NULL;
    memcpy(csi.buf, info->buf, info->len);
    csi.len = info->len;

    if(xQueueSendToBack(queue, (void *)&csi, (TickType_t)0) != pdPASS) {
        ESP_LOGD(TAG, "queue error - full?");
    }
}

/** init CSI tracing */
static void wifi_csi_init()
{
    /**
     * @brief In order to ensure the compatibility of routers, only LLTF sub-carriers are selected.
     */
    wifi_csi_config_t csi_config = {
            .lltf_en           = true,
            .htltf_en          = false,
            .stbc_htltf2_en    = false,
            .ltf_merge_en      = true,
            .channel_filter_en = true,
            .manu_scale        = true,
            .shift             = true,
    };

    ESP_ERROR_CHECK(esp_wifi_set_csi_config(&csi_config));
    ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(wifi_csi_rx_cb, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_csi(true));
}

/** register for wifi events */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station " MACSTR " join, AID=%d", MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d", MAC2STR(event->mac), event->aid);
    }
}

/** init AP functionality */
void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = SENSING_WALL_WIFI_SSID,
            .ssid_len = strlen(SENSING_WALL_WIFI_SSID),
            .channel = SENSING_WALL_WIFI_CHANNEL,
            .password = SENSING_WALL_WIFI_PASS,
            .max_connection = SENSING_WALL_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
        },
    };
    if (strlen(SENSING_WALL_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    //ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N));
    //ESP_ERROR_CHECK(esp_wifi_config_11b_rate(WIFI_IF_AP, true));
    ESP_ERROR_CHECK(esp_wifi_start());
    // restrict AMC to MCS0
    ESP_ERROR_CHECK(esp_wifi_internal_set_fix_rate(WIFI_IF_AP, 1, WIFI_PHY_RATE_MCS0_LGI));
    // disable power saving
    esp_wifi_set_ps(WIFI_PS_NONE);

    // todo: log MAC address
    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             SENSING_WALL_WIFI_SSID, SENSING_WALL_WIFI_PASS, SENSING_WALL_WIFI_CHANNEL);
}

char *data = (char *) "1\n";

/** transmit frames in an endless loop */
void socket_transmitter_sta_loop(int (*connected_stations)()) {

    queue = xQueueCreate(50, sizeof(static_wifi_csi_info_t));
    int socket_fd = -1, socket_fd_host = -1;

    while (true) {
        close(socket_fd);
        close(socket_fd_host);

        // multicast IP address
        char *ip = (char *) "225.1.1.1";
        struct sockaddr_in caddr;
        caddr.sin_family = AF_INET;
        caddr.sin_port = htons(UDP_PORT);

        struct sockaddr_in host_addr;

        host_addr.sin_family = AF_INET;
        host_addr.sin_port = htons(UDP_PORT_HOST);

        if (inet_aton(SENSING_WALL_DB_HOST_IP, &host_addr.sin_addr) == 0) {
            ESP_LOGE(TAG, "inet_aton failed.");
            abort();
        }

        socket_fd_host = socket(PF_INET, SOCK_DGRAM, 0);
        if (socket_fd_host == -1) {
            ESP_LOGE(TAG, "Socket creation error [%s].", strerror(errno));
            abort();
        }

        int8_t connection_retires = 0;
        while (!connected_stations()) {
            // wait until a station connects
            ESP_LOGE(TAG, "No stations/server connected, waiting...");
            connection_retires++;
            if (connection_retires > 100) {
                abort();
            }
            vTaskDelay(1000 / portTICK_PERIOD_MS); // block for 1000ms
        }

        ESP_LOGD(TAG, "At least one station is connected, continue...");
        if (inet_aton(ip, &caddr.sin_addr) == 0) {
            ESP_LOGE(TAG, "inet_aton failed.");
            continue;
        }

        socket_fd = socket(PF_INET, SOCK_DGRAM, 0);
        if (socket_fd == -1) {
            ESP_LOGE(TAG, "Socket creation error [%s]", strerror(errno));
            continue;
        }

        uint8_t dnode[6];
        ESP_ERROR_CHECK(esp_read_mac(dnode, ESP_MAC_WIFI_SOFTAP));

        ESP_LOGD(TAG, "sending multicast frames.");

        while (true) { // IP multicast packet transmission
            ESP_LOGD(TAG, "checking connected stations...");

            if (!connected_stations()) {
                ESP_LOGE(TAG, "No connected stations.");
                break;
            }

            ESP_LOGD(TAG, "start sending ip mcast packets...");
            if (sendto(socket_fd, &data, strlen(data), 0, (const struct sockaddr *) &caddr, sizeof(caddr)) !=
                strlen(data)) {
                vTaskDelay(1); //todo: 1 / portTICK_PERIOD_MS // 1ms
                continue;
            }

            static_wifi_csi_info_t csi_info;

            while (xQueueReceive(queue, &csi_info, ( TickType_t ) 0)) {
                ESP_LOGD(TAG, "CSI queue is not empty");
                char buff_json[800] = {0};
                // convert CSI data to JSON
                int len_json = csi_to_json(&csi_info, (char *)buff_json,
                                           sizeof(buff_json), &dnode);

                if(len_json < 0)
                    continue;

                int to_send = len_json, sent = 0, retires = 0;
                while (to_send > 0 && retires < 5) {
                    ESP_LOGD(TAG, "Sending CSI data to DB host.");
                    if ((sent = sendto(socket_fd_host, buff_json + sent, to_send, 0,
                                       (const struct sockaddr *) &host_addr, sizeof(host_addr))) == -1) {
                        ESP_LOGE(TAG, "sendto: %d %s", socket_fd_host, strerror(errno));
                        retires++;
                        vTaskDelay(1 / portTICK_PERIOD_MS); // wait 1ms and retry
                    } else {
                        to_send -= sent;
                    }
                }
                ESP_LOGD(TAG, "%s", buff_json);
            }
            vTaskDelay(SENSING_WALL_IP_MCAST_INTERVAL / portTICK_PERIOD_MS); // in 2ms
        }
    }
}

void app_main(void)
{
    // initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "SENSING_WALL_AP");
    // start AP functionality
    wifi_init_softap();
    // register CSI feedback
    wifi_csi_init();
    // start packet transmission loop
    xTaskCreate(socket_transmitter_sta_loop, "socket_transmitter_sta_loop", 10000, (void *) &connected_stations, 5, NULL);
}
