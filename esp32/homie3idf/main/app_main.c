#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event_loop.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "mqtt_client.h"

static const char *TAG = "MQTT_EXAMPLE";

static EventGroupHandle_t wifi_event_group;
const static int CONNECTED_BIT = BIT0;

typedef struct 
{
    const char*     name ;
    const char*     settable ;
    const char*     retained ;
    const char*     datatype ;
} homieProperty ;
typedef struct 
{
    const char*     name ;
    int             numProperties ;
    homieProperty*  properties ;
} homieNode ;
typedef struct 
{
    const char*     name ;
    int             numNodes ;
    homieNode*      nodes ;
} homieDevice ;

const int publishQos = 2 ;

homieProperty   lampPower       = {"power","true","true","boolean"} ;
homieNode       lampRelay       = {"relay",sizeof(lampPower)/sizeof(homieProperty),&lampPower} ;
homieDevice     thisDevice      = {"lamp",sizeof(lampRelay)/sizeof(homieNode),&lampRelay} ;

char str[ 256 ] ;
char str2[ 256 ] ;
int mqtt_publish_str(esp_mqtt_client_handle_t client, const char *topic, const char *data, int qos, int retain)
{
    int msg_id ;
    msg_id      =   esp_mqtt_client_publish( client, topic, data, 0, qos, retain) ;
    if (msg_id>0)
    {
        ESP_LOGI(TAG, "mqtt_publish_str: %s ← %s, msg_id=%d", topic, data, msg_id) ;
    }
    else
    {
        /* code */
        ESP_LOGI(TAG, "mqtt_publish_str: %s ← %s, FAILED!", topic, data) ;
    }

    return (msg_id) ;
    
}

int setHomieDeviceState(esp_mqtt_client_handle_t client, homieDevice* device, const char* state)
{
    int msg_id ;

    sprintf(str,"homie/%s/$state", device->name ) ;
    msg_id      =   mqtt_publish_str( client, str, state, publishQos, 1) ;

    return ( msg_id ) ;

}

int registerHomieDevice(esp_mqtt_client_handle_t client, homieDevice* device)
{
    int msg_id ;
    int i,j ;

    if (setHomieDeviceState( client, device, "init" )<0)
    {
        return (-1) ;
    }

    sprintf(str,"homie/%s/$homie", device->name ) ;
    msg_id      =   mqtt_publish_str( client, str, "3.0", publishQos, 1) ;

    sprintf(str,"homie/%s/$name", device->name ) ;
    msg_id      =   mqtt_publish_str( client, str, device->name, publishQos, 1) ;

    str2[0] = 0 ;
    for(i=0;i<device->numNodes;i++)
    {
        strcat( str2, device->nodes[i].name ) ;
        if (i<device->numNodes-1)
        {
            strcat( str2, "," ) ;
        }
    }
    sprintf(str,"homie/%s/$nodes", device->name ) ;
    msg_id      =   mqtt_publish_str( client, str, str2, publishQos, 1) ;

    for(i=0;i<device->numNodes;i++)
    {
        sprintf(str,"homie/%s/%s/$name", device->name, device->nodes[i].name ) ;
        msg_id      =   mqtt_publish_str( client, str, device->nodes[i].name, publishQos, 1) ;
        str2[0] = 0 ;
        for (j=0;j<device->nodes[i].numProperties;j++)
        {
            strcat(str2,device->nodes[i].properties[j].name) ;
            if (j<device->nodes[i].numProperties)
            {
                strcat( str2, "," ) ;
            }
        }
        sprintf(str,"homie/%s/%s/$properties", device->name, device->nodes[i].name ) ;
        msg_id      =   mqtt_publish_str( client, str, str2, publishQos, 1) ;

        for (j=0;j<device->nodes[i].numProperties;j++)
        {
            sprintf(str,"homie/%s/%s/%s/$name", device->name, device->nodes[i].name, device->nodes[i].properties[j].name ) ;
            msg_id      =   mqtt_publish_str( client, str, device->nodes[i].properties[j].name, publishQos, 1) ;

            sprintf(str,"homie/%s/%s/%s/$settable", device->name, device->nodes[i].name, device->nodes[i].properties[j].name ) ;
            msg_id      =   mqtt_publish_str( client, str, device->nodes[i].properties[j].settable, publishQos, 1) ;
            
            sprintf(str,"homie/%s/%s/%s/$retained", device->name, device->nodes[i].name, device->nodes[i].properties[j].name ) ;
            msg_id      =   mqtt_publish_str( client, str, device->nodes[i].properties[j].retained, publishQos, 1) ;
            
            sprintf(str,"homie/%s/%s/%s/$datatype", device->name, device->nodes[i].name, device->nodes[i].properties[j].name ) ;
            msg_id      =   mqtt_publish_str( client, str, device->nodes[i].properties[j].datatype, publishQos, 1) ;
            
        }
    }

    return (msg_id) ;

}

int last_msg_id = 0 ;
static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    // your_context_t *context = event->context;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:

            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            last_msg_id = registerHomieDevice( client, &thisDevice ) ;
            ESP_LOGI(TAG, "Device registered");
/*
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            msg_id = esp_mqtt_client_publish(client, "topic/qos1", "data_3", 0, 1, 0);
            ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);

            msg_id = esp_mqtt_client_subscribe(client, "topic/qos0", 0);
            ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

            msg_id = esp_mqtt_client_subscribe(client, "topic/qos1", 1);
            ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

            msg_id = esp_mqtt_client_unsubscribe(client, "topic/qos1");
            ESP_LOGI(TAG, "sent unsubscribe successful, msg_id=%d", msg_id);
*/
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            //msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
            //ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            if (event->msg_id==last_msg_id)
            {
                setHomieDeviceState( client, &thisDevice, "ready" ) ;
            }
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;
        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
    return ESP_OK;
}

static esp_err_t wifi_event_handler(void *ctx, system_event_t *event)
{
    switch (event->event_id) {
        case SYSTEM_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);

            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            esp_wifi_connect();
            xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
            break;
        default:
            break;
    }
    return ESP_OK;
}

static void wifi_init(void)
{
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_init(wifi_event_handler, NULL));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_LOGI(TAG, "start the WIFI SSID:[%s]", CONFIG_WIFI_SSID);
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Waiting for wifi");
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
}

static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = CONFIG_BROKER_URL,
        .event_handle = mqtt_event_handler,
        // .user_context = (void *)your_context
    };

#if CONFIG_BROKER_URL_FROM_STDIN
    char line[128];

    if (strcmp(mqtt_cfg.uri, "FROM_STDIN") == 0) {
        int count = 0;
        printf("Please enter url of mqtt broker\n");
        while (count < 128) {
            int c = fgetc(stdin);
            if (c == '\n') {
                line[count] = '\0';
                break;
            } else if (c > 0 && c < 127) {
                line[count] = c;
                ++count;
            }
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
        mqtt_cfg.uri = line;
        printf("Broker url: %s\n", line);
    } else {
        ESP_LOGE(TAG, "Configuration mismatch: wrong broker url");
        abort();
    }
#endif /* CONFIG_BROKER_URL_FROM_STDIN */

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_start(client);
}

void app_main()
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_TCP", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_SSL", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);

    nvs_flash_init();
    wifi_init();
    mqtt_app_start();
}
