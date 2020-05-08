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

//HOMIE3_DEV_NAME

static const char*              TAG                 = CONFIG_HOMIE3_DEV_NAME ;

static const int                KEYB_ON_PRESS_TIME  = 250 ; // Time to detect on btn pressed
static const int                KEYB_OFF_PRESS_TIME = 150 ; // Time to detect off btn pressed
static const int                KEYB_PAUSE_AFTER_DN = 500 ; // Keyboard pause after keydown 

static const int                POLL_GRANULARITY    = 10 ;  // polling granularity
static const int                USONIC_MAX_US       = 200 ; // Maximum ultrasonic echo interval, microseconds

static const int                publishQos          = 2 ;

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

homieProperty   relayPower      = {"power","true","true","boolean"} ;
homieProperty   Temperature[]   = {
                                    {"Temperature","false","true","float"},
                                    {"Humidity","false","true","float"},
                                } ;
homieProperty   Watertank[]     = {
                                    {"FloatLevel","false","true","float"},
                                    {"UpLevel","false","true","float"},
                                    {"uSonicLevel","false","true","float"},
                                  } ;
homieNode       nodeArray[]     = {
#if CONFIG_HOMIE3_RELAY    
    {"relay",sizeof(relayPower)/sizeof(homieProperty),&relayPower},
#endif
#if CONFIG_HOMIE3_THERMOMETER    
    {"Thermometer",sizeof(Temperature)/sizeof(homieProperty),Temperature},
#endif    
#if CONFIG_HOMIE3_WATERTANK_SENSORS    
    {"Watertank",sizeof(Watertank)/sizeof(homieProperty),Watertank},
#endif    
    } ;

    enum nodeArrayIndices {
#if CONFIG_HOMIE3_RELAY
    relayNodeIndex,    
#endif
#if CONFIG_HOMIE3_THERMOMETER    
    thermometerNodeIndex,    
#endif    
#if CONFIG_HOMIE3_WATERTANK_SENSORS    
    watertankNodeIndex,
#endif    
    } ;

homieDevice     thisDevice      = {CONFIG_HOMIE3_DEV_NAME,sizeof(nodeArray)/sizeof(homieNode),nodeArray} ;

int subscribeToProperty( esp_mqtt_client_handle_t client, homieDevice* device, int nodeIndex, int propIndex) ;
int setHomieDeviceState(esp_mqtt_client_handle_t client, homieDevice* device, const char* state) ;
void OnDeviceConfigurationComplete(esp_mqtt_client_handle_t client)
{
    ESP_LOGI(TAG, "OnDeviceConfigurationComplete().") ;
    
    // Subscribe to ../set to get switchon/off notification from broker
#if CONFIG_HOMIE3_RELAY    
    subscribeToProperty( client, &thisDevice, relayNodeIndex, 0) ;
#endif

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

char szBuffer3[256] ;
int notifyToSetProperty( esp_mqtt_client_handle_t client, homieDevice* device, int nodeIndex, int propIndex, const char* szValue )
{
    int msg_id ;

    sprintf(szBuffer3,"homie/%s/%s/%s/set", 
        device->name, 
            device->nodes[nodeIndex].name, 
                device->nodes[nodeIndex].properties[propIndex].name 
                ) ;
    msg_id = mqtt_publish_str( client, szBuffer3, szValue, publishQos, 1) ;

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
#if CONFIG_HOMIE3_RELAY    
            if (strnstr( event->data, "false", event->topic_len )!=NULL)
            {
                ESP_LOGI(TAG, "MQTT_EVENT from %.*s:Switch Off", event->topic_len, event->topic) ;
                publishPropertyValue( client, &thisDevice, relayNodeIndex,0, "false" ) ;
                gpio_set_level(CONFIG_HOMIE3_RELAY_GPIO_NUM, 1 ) ;
            }
            if (strnstr( event->data, "true", event->data_len )!=NULL)
            {                
                ESP_LOGI(TAG, "MQTT_EVENT from %.*s:Switch On", event->topic_len, event->topic) ;
                publishPropertyValue( client, &thisDevice, relayNodeIndex,0, "true" ) ;
                gpio_set_level(CONFIG_HOMIE3_RELAY_GPIO_NUM, 0 ) ;
            }
#endif            
            break ;
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

#if CONFIG_HOMIE3_THERMOMETER    
static const dht_sensor_type_t  sensor_type         = DHT_TYPE_AM2301 ;
char strValue[256] ;
void DHT_task(void *pvParameter)
{
    int16_t temperature = 0 ;
    int16_t humidity    = 0 ;
    esp_mqtt_client_handle_t client = (esp_mqtt_client_handle_t) pvParameter ;

    ESP_LOGI(TAG, "Starting DHT Task") ;
    ESP_LOGI(TAG, "Thermometer node index: %d", thermometerNodeIndex ) ;

    while(1) 
    {
        if (dht_read_data(sensor_type, CONFIG_HOMIE3_THERMOMETER_GPIO_NUM, &humidity, &temperature) == ESP_OK)
        {
            ESP_LOGI(TAG, "Hum %.1f\n", humidity / 10.0 ) ;
            ESP_LOGI(TAG, "Tmp %.1f\n", temperature / 10.0 ) ;

            sprintf( strValue,"%.1f", temperature / 10.0 ) ;            
            publishPropertyValue( client, &thisDevice, thermometerNodeIndex,0, strValue ) ;

            sprintf( strValue,"%.1f", humidity / 10.0 ) ;            
            publishPropertyValue( client, &thisDevice, thermometerNodeIndex,1, strValue ) ;
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
#endif

void keyb_task(void *pvParameter)
{
    bool btnOn    = false ;
    bool btnOff   = false ;
    int msPressed = 0 ;
    esp_mqtt_client_handle_t client = (esp_mqtt_client_handle_t) pvParameter ;

    ESP_LOGI(TAG, "Starting keyboard task\n\n") ;

    // initialize gpio pins
    gpio_reset_pin( CONFIG_HOMIE3_BTN_ON_GPIO_NUM ) ;
    gpio_set_direction( CONFIG_HOMIE3_BTN_ON_GPIO_NUM, GPIO_MODE_INPUT ) ;
    gpio_set_pull_mode( CONFIG_HOMIE3_BTN_ON_GPIO_NUM, GPIO_PULLUP_ONLY ) ;

    gpio_reset_pin( CONFIG_HOMIE3_BTN_OFF_GPIO_NUM ) ;
    gpio_set_direction( CONFIG_HOMIE3_BTN_OFF_GPIO_NUM, GPIO_MODE_INPUT ) ;
    gpio_set_pull_mode( CONFIG_HOMIE3_BTN_OFF_GPIO_NUM, GPIO_PULLUP_ONLY ) ;

    while(1) 
    {
        // keyboard processing
        btnOn    = false ;
        btnOff   = false ;

        msPressed = 0 ;
        while( gpio_get_level(CONFIG_HOMIE3_BTN_ON_GPIO_NUM)==CONFIG_HOMIE3_BTN_ON_ACTIVE_LEVEL )
        {        
            vTaskDelay( 40 / portTICK_RATE_MS ) ;
            msPressed += 40 ;
            if (msPressed>KEYB_ON_PRESS_TIME)
            {
                break ;        
            }
        }
        btnOn = msPressed>KEYB_ON_PRESS_TIME ;

        msPressed = 0 ;
        while( gpio_get_level(CONFIG_HOMIE3_BTN_OFF_GPIO_NUM)==CONFIG_HOMIE3_BTN_OFF_ACTIVE_LEVEL )
        {        
            vTaskDelay( 40 / portTICK_RATE_MS ) ;
            msPressed += 40 ;
            if (msPressed>KEYB_OFF_PRESS_TIME)
            {
                break ;        
            }
        }
        btnOff = msPressed>KEYB_OFF_PRESS_TIME ;

        if (btnOn && btnOff)
        { 
            // both btn pressed - do nothing   
            ESP_LOGI(TAG, "Disabled button combination.\n" ) ;  
            vTaskDelay( 100 / portTICK_RATE_MS ) ;
        }
        else if (btnOn)
        {
                //notifyToSetProperty(client,&thisDevice,0,0,"true") ;
                mqtt_publish_str( client, CONFIG_HOMIE3_KEYBOARD_RELAY_ADDRESS, "true", publishQos, 1) ;

                ESP_LOGI(TAG, "BTN_ON pressed\n." ) ;
                // pause keyboard processing  
                vTaskDelay( KEYB_PAUSE_AFTER_DN / portTICK_RATE_MS ) ;
        }
        else if (btnOff)
        {
                //notifyToSetProperty(client,&thisDevice,0,0,"false") ;
                mqtt_publish_str( client, CONFIG_HOMIE3_KEYBOARD_RELAY_ADDRESS, "false", publishQos, 1) ;
                ESP_LOGI(TAG, "BTN_OFF pressed\n." ) ;  
                // pause keyboard processing  
                vTaskDelay( KEYB_PAUSE_AFTER_DN / portTICK_RATE_MS ) ;
        }
        else
        {
            /* nothing pressed */
            vTaskDelay( 100 / portTICK_RATE_MS ) ;
        }
        
    }
}

static portMUX_TYPE usonicMux = portMUX_INITIALIZER_UNLOCKED ;
char strValueWt[256] ;
void watertank_task(void *pvParameter)
{
    int usWait = 0 ;
    esp_mqtt_client_handle_t client = (esp_mqtt_client_handle_t) pvParameter ;

    ESP_LOGI(TAG, "Starting watertank task\n\n") ;
    ESP_LOGI(TAG, "Watertank node index: %d\n\n", watertankNodeIndex ) ;

    // initialize gpio pins
    if (CONFIG_HOMIE3_FLOAT_LEVEL_GPIO_NUM>=0)
    {
        gpio_reset_pin( CONFIG_HOMIE3_FLOAT_LEVEL_GPIO_NUM ) ;
        gpio_set_direction( CONFIG_HOMIE3_FLOAT_LEVEL_GPIO_NUM, GPIO_MODE_INPUT ) ;
        gpio_set_pull_mode( CONFIG_HOMIE3_FLOAT_LEVEL_GPIO_NUM, GPIO_PULLUP_ONLY ) ;
    }

    if (CONFIG_HOMIE3_UP_LEVEL_GPIO_NUM>=0)
    {
        gpio_reset_pin( CONFIG_HOMIE3_UP_LEVEL_GPIO_NUM ) ;
        gpio_set_direction( CONFIG_HOMIE3_UP_LEVEL_GPIO_NUM, GPIO_MODE_INPUT ) ;
        gpio_set_pull_mode( CONFIG_HOMIE3_UP_LEVEL_GPIO_NUM, GPIO_PULLUP_ONLY ) ;
    }

    if (CONFIG_HOMIE3_USONIC_TRIGGER_GPIO_NUM>=0)
    {
        gpio_reset_pin( CONFIG_HOMIE3_USONIC_TRIGGER_GPIO_NUM ) ;
        gpio_set_direction( CONFIG_HOMIE3_USONIC_TRIGGER_GPIO_NUM, GPIO_MODE_OUTPUT ) ;
    }

    if (CONFIG_HOMIE3_USONIC_ECHO_GPIO_NUM>=0)
    {
        gpio_reset_pin( CONFIG_HOMIE3_USONIC_ECHO_GPIO_NUM ) ;
        gpio_set_direction( CONFIG_HOMIE3_USONIC_ECHO_GPIO_NUM, GPIO_MODE_INPUT ) ;
        gpio_set_pull_mode( CONFIG_HOMIE3_USONIC_ECHO_GPIO_NUM, GPIO_PULLUP_ONLY ) ;
        publishPropertyValue( client, &thisDevice, watertankNodeIndex, 2, "0" ) ;
    }

    while(1) 
    {
        if (CONFIG_HOMIE3_FLOAT_LEVEL_GPIO_NUM>=0)
        {
            if (gpio_get_level(CONFIG_HOMIE3_FLOAT_LEVEL_GPIO_NUM)==0)
            {
                publishPropertyValue( client, &thisDevice, watertankNodeIndex, 0, "0" ) ;
            }
            else
            {
                publishPropertyValue( client, &thisDevice, watertankNodeIndex, 0, "1" ) ;
            }
        }        

        if (CONFIG_HOMIE3_UP_LEVEL_GPIO_NUM>=0)
        {
            if (gpio_get_level(CONFIG_HOMIE3_UP_LEVEL_GPIO_NUM)==0)
            {
                publishPropertyValue( client, &thisDevice, watertankNodeIndex, 1, "0" ) ;
            }
            else
            {
                publishPropertyValue( client, &thisDevice, watertankNodeIndex, 1, "1" ) ;
            }
        }

        if (CONFIG_HOMIE3_USONIC_TRIGGER_GPIO_NUM>=0)
        {
            portENTER_CRITICAL(&usonicMux) ;
            gpio_set_level(CONFIG_HOMIE3_USONIC_TRIGGER_GPIO_NUM,0) ;
            ets_delay_us(20) ;
            gpio_set_level(CONFIG_HOMIE3_USONIC_TRIGGER_GPIO_NUM,1) ;
            ets_delay_us(10) ;
            gpio_set_level(CONFIG_HOMIE3_USONIC_TRIGGER_GPIO_NUM,0) ;
            usWait          = 0 ; 
            while(usWait<USONIC_MAX_US)
            {
                if (gpio_get_level(CONFIG_HOMIE3_USONIC_TRIGGER_GPIO_NUM))
                {
                    break ;
                }
                ets_delay_us(POLL_GRANULARITY) ;
                usWait      += POLL_GRANULARITY ;
            }
            portEXIT_CRITICAL(&usonicMux) ;               

            float distanceTo = usWait/58.0 ;
            if (usWait<USONIC_MAX_US)
            {
                sprintf( strValueWt, "%5.2f", distanceTo ) ;
                publishPropertyValue( client, &thisDevice, watertankNodeIndex, 2, strValueWt ) ;
            }
        }

        vTaskDelay( 10000 / portTICK_RATE_MS ) ;
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

#if CONFIG_HOMIE3_THERMOMETER    
    xTaskCreate( &DHT_task, "DHT_task", configMINIMAL_STACK_SIZE * 5, client, 5, NULL ) ;
#endif
#if CONFIG_HOMIE3_KEYBOARD    
    xTaskCreate( &keyb_task, "keyb_task", configMINIMAL_STACK_SIZE * 3, client, 5, NULL ) ;
#endif
#if CONFIG_HOMIE3_WATERTANK_SENSORS    
    xTaskCreate( &watertank_task, "watertank_task", configMINIMAL_STACK_SIZE * 3, client, 5, NULL ) ;
#endif

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

#if CONFIG_HOMIE3_RELAY    
    // initialize gpio pins
    gpio_reset_pin( CONFIG_HOMIE3_RELAY_GPIO_NUM ) ;
    gpio_set_direction( CONFIG_HOMIE3_RELAY_GPIO_NUM, GPIO_MODE_OUTPUT ) ;
    gpio_set_level(CONFIG_HOMIE3_RELAY_GPIO_NUM, 1 ) ;
#endif

    mqtt_app_start() ;
}
