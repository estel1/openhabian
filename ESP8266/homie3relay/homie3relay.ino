#include <ESP8266WiFi.h>
#include <MQTT.h>
#include <wifiudp.h>
#include <Syslog.h>

#define DEVNAME "ESP8266_Relay_1" 
const char* devname                     = DEVNAME ;

struct MqttMsg
{ 
  const char* topic ;
  const char* payload ;
  boolean retained ;
} ;

MqttMsg HomieInitMsgs[] = 
{
  {"homie/"DEVNAME"/$state","init",true},
  {"homie/"DEVNAME"/$homie","3.0",true},
  {"homie/"DEVNAME"/$name",devname,true},
  {"homie/"DEVNAME"/$nodes","Relay",true},  
  {"homie/"DEVNAME"/Relay/$name","Relay",true},
  {"homie/"DEVNAME"/Relay/$properties","power",true},
  {"homie/"DEVNAME"/Relay/power","false",true},
  {"homie/"DEVNAME"/Relay/power/$name","power",true},
  {"homie/"DEVNAME"/Relay/power/$settable","true",true},
  {"homie/"DEVNAME"/Relay/power/$retained","true",true},
  {"homie/"DEVNAME"/Relay/power/$datatype","boolean",true},
  {"homie/"DEVNAME"/$state","ready",true}
} ;

// Replace the next variables with your SSID/Password combination
const char*     ssid                        = "Keenetic-0079" ;
const char*     password                    = "yoHwLp6B" ;

const char*     mqtt_server                 = "192.168.1.54" ;
const int       mqtt_port                   = 1883 ;
const char*     syslog_server               = "192.168.1.54" ;
unsigned long   lastMsg                     = 0 ;

WiFiClient      wifi_client ;
MQTTClient      mqtt_client ;

const int       RELAY_PIN                   = 0 ;

WiFiUDP         udpClient ;
Syslog          syslog(udpClient, syslog_server, 514, devname, "monitor", LOG_KERN) ;

bool log_printf(uint16_t pri, const char *fmt, ...) 
{
  va_list args ;
  bool result ;
  
  va_start(args, fmt) ;
  
  char *message = NULL ;
  size_t initialLen = 0 ;
  size_t len = 0 ;

  initialLen = strlen(fmt) ;

  message = new char[initialLen + 1] ;

  len = vsnprintf( message, initialLen + 1, fmt, args) ;
  if (len > initialLen) 
  {
    delete[] message ; message = NULL ;
    message = new char[len + 1] ;

    vsnprintf(message, len + 1, fmt, args) ;
  }

  Serial.print( message ) ;
  result = syslog.log( pri, message ) ;
  delete[] message ; message = NULL ;
  
  va_end(args) ;  
  return (result) ;
}

boolean register_relay_homie_device()
{
  int qos = 2 ;
  int num_msg = sizeof(HomieInitMsgs)/sizeof(MqttMsg) ;
  for( int i=0;i<num_msg;i++ )
  {
    log_printf( LOG_INFO, "publish %s:%s\n", HomieInitMsgs[i].topic,HomieInitMsgs[i].payload ) ;  
    while (!mqtt_client.publish(HomieInitMsgs[i].topic,HomieInitMsgs[i].payload,HomieInitMsgs[i].retained,qos))
    {
      log_printf( LOG_INFO, "register_relay_homie_device() failed.\n" ) ;  
      delay(1000) ;
    }
  }
  //homie/kitchen-light/light/power/set ← "true"
  return (true) ;
}

void connect()
{
  log_printf(LOG_INFO, "Check WiFi.\n" ) ;      
  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(1000) ;
    log_printf(LOG_INFO,".") ;
  }
  log_printf( LOG_INFO, "Wifi connected. IP address: %s\n", WiFi.localIP().toString().c_str() ) ;  
  log_printf(LOG_INFO, "Connecting to Mqtt broker...\n" ) ;      
  while (!mqtt_client.connected()) 
  {
    if (mqtt_client.connect(devname))
    {
      if (register_relay_homie_device())
      {
        mqtt_client.subscribe( "homie/"DEVNAME"/Relay/power/set" ) ;
        break ;
      }
    }
    delay(1000) ;
    log_printf(LOG_INFO, ".") ;
  }
}

void setup() 
{
  delay(10) ;
  pinMode(RELAY_PIN, OUTPUT) ;  
  Serial.begin(115200) ;
  WiFi.begin(ssid, password) ;
  mqtt_client.begin( mqtt_server, mqtt_port, wifi_client ) ;
  mqtt_client.onMessageAdvanced( callback ) ;
  connect() ;
}

void loop() 
{  
  if (!mqtt_client.connected()) 
  {
    connect() ;
  }
  mqtt_client.loop() ;
}

void callback(MQTTClient *client, char topic[], char message[], int msg_len) 
{
  log_printf( LOG_INFO, "Message arrived on topic %s, length:%d\n", topic, msg_len ) ;  
  if (msg_len>0)
  {
      if (message[0]=='t')
      {
        mqtt_client.publish(   "homie/"DEVNAME"/Relay/power","true" ) ;
        log_printf( LOG_INFO, "homie/"DEVNAME"/Relay/power ← true" ) ;  
        digitalWrite(RELAY_PIN, LOW) ;   
      }
      else if (message[0]=='f')
      {
        mqtt_client.publish(   "homie/"DEVNAME"/Relay/power","false" ) ;
        log_printf( LOG_INFO, "homie/"DEVNAME"/Relay/power ← false" ) ;  
        digitalWrite(RELAY_PIN, HIGH) ; 
      }
      else
      {
      }
      
  }
}




