/* 
   Homie3 universal node

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "mqtt_client.h"
#include "udp_logging.h"
#include "dht.h"

static const char*              TAG             = "relayNode" ;
static const gpio_num_t         dht_gpio        = 4 ;
static const dht_sensor_type_t  sensor_type     = DHT_TYPE_AM2301 ;

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

homieProperty   relayPower      = {"power","true","true","boolean"} ;
homieProperty   Temperature     = {"Temperature","false","true","float"} ;
homieNode       nodeArray[]     = {
    {"relay",sizeof(relayPower)/sizeof(homieProperty),&relayPower},
    {"Thermometer",sizeof(Temperature)/sizeof(homieProperty),&Temperature}
    } ;
homieDevice     thisDevice      = {"relayNode",sizeof(nodeArray)/sizeof(homieNode),&nodeArray} ;

int subscribeToProperty( esp_mqtt_client_handle_t client, homieDevice* device, int nodeIndex, int propIndex) ;
void OnDeviceConfigurationComplete(esp_mqtt_client_handle_t client)
{
    ESP_LOGI(TAG, "OnDeviceConfigurationComplete().") ;
    
    // Subscribe to ../set to get switchon/off notification from broker
    subscribeToProperty( client, &thisDevice, 0, 0) ;
    
    // start normal operation
    setHomieDeviceState( client, &thisDevice, "ready" ) ;
}


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

char szBuffer[256] ;
int publishPropertyValue( esp_mqtt_client_handle_t client, homieDevice* device, int nodeIndex, int propIndex, const char* szValue )
{
    int msg_id ;

    sprintf(szBuffer,"homie/%s/%s/%s", 
        device->name, 
            device->nodes[nodeIndex].name, 
                device->nodes[nodeIndex].properties[propIndex].name 
                ) ;
    msg_id = mqtt_publish_str( client, szBuffer, szValue, publishQos, 1) ;

    return ( msg_id ) ;
}

char szBuffer2[256] ;
int subscribeToProperty( esp_mqtt_client_handle_t client, homieDevice* device, int nodeIndex, int propIndex)
{
    int msg_id ;

    sprintf(szBuffer2,"homie/%s/%s/%s/set", 
        device->name, 
            device->nodes[nodeIndex].name, 
                device->nodes[nodeIndex].properties[propIndex].name 
                ) ;
    msg_id = esp_mqtt_client_subscribe(client, szBuffer2, 0) ;

    ESP_LOGI(TAG, "esp_mqtt_client_subscribe: %s, msg_id=%d", szBuffer2, msg_id) ;
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

static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    //int msg_id;
    // your_context_t *context = event->context;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            last_msg_id = registerHomieDevice( client, &thisDevice ) ;
            ESP_LOGI(TAG, "Device registered");
        /*
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            msg_id = esp_mqtt_client_publish(client, "/topic/qos1", "data_3", 0, 1, 0);
            ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);

            msg_id = esp_mqtt_client_subscribe(client, "/topic/qos0", 0);
            ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

            msg_id = esp_mqtt_client_subscribe(client, "/topic/qos1", 1);
            ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

            msg_id = esp_mqtt_client_unsubscribe(client, "/topic/qos1");
            ESP_LOGI(TAG, "sent unsubscribe successful, msg_id=%d", msg_id);
        */
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            /*
            msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
            ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
            */
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            if (event->msg_id==last_msg_id)
            {
                OnDeviceConfigurationComplete(client) ;
            }
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            ESP_LOGI(TAG, "TOPIC=%.*s\r\n", event->topic_len, event->topic);
            ESP_LOGI(TAG, "DATA=%.*s\r\n", event->data_len, event->data);
            if (strnstr( event->data, "false", event->topic_len )!=NULL)
            {
                ESP_LOGI(TAG, "MQTT_EVENT from %s:Switch Off", event->topic) ;
            }
            if (strnstr( event->data, "true" )!=NULL)
            {                
                ESP_LOGI(TAG, "MQTT_EVENT from %s:Switch On", event->topic) ;
            }
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

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    mqtt_event_handler_cb(event_data);
}

char strValue[256] ;
void DHT_task(void *pvParameter)
{
    int16_t temperature = 0 ;
    int16_t humidity    = 0 ;
    esp_mqtt_client_handle_t client = (esp_mqtt_client_handle_t) pvParameter ;

    ESP_LOGI(TAG, "Starting DHT Task\n\n");

    while(1) 
    {
        if (dht_read_data(sensor_type, dht_gpio, &humidity, &temperature) == ESP_OK)
        {
            ESP_LOGI(TAG, "Hum %.1f\n", humidity / 10.0 ) ;
            ESP_LOGI(TAG, "Tmp %.1f\n", temperature / 10.0 ) ;

            sprintf( strValue,"%.1f", temperature / 10.0 ) ;
            
            publishPropertyValue( client, &thisDevice, 1,0, strValue ) ;
        }
        else
        {
            ESP_LOGI(TAG, "Could not read data from sensor\n") ;
        }

        // -- wait at least 2 sec before reading again ------------
        // The interval of whole process must be beyond 2 seconds !! 
        vTaskDelay( 15000 / portTICK_RATE_MS ) ;
    }
}

static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = CONFIG_BROKER_URL,
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
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(client);

    ESP_LOGI(TAG, "Prepare DHT task...\n") ;
    xTaskCreate( &DHT_task, "DHT_task", configMINIMAL_STACK_SIZE * 5, client, 5, NULL ) ;

}

void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
    esp_log_level_set("MQTT_EXAMPLE", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_TCP", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_SSL", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init()) ;
    ESP_ERROR_CHECK(esp_netif_init()) ;
    ESP_ERROR_CHECK(esp_event_loop_create_default()) ;

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect()) ;

    udp_logging_init( "192.168.1.54", 514, udp_logging_vprintf ) ;

    mqtt_app_start() ;
}
