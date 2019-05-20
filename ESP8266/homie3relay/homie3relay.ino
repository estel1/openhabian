#include <ESP8266WiFi.h>
#include <MQTT.h>
#include <wifiudp.h>
#include <Syslog.h>

#define DEVNAME "esp8266relay2" 
const char* devname                       = DEVNAME ;
#define NODENAME "relay"
const char* nodename                      = NODENAME ;
#define PRONAME "power"
const char* proname                       = PRONAME ;

struct MqttMsg
{ 
  const char* topic ;
  const char* payload ;
  boolean retained ;
} ;

MqttMsg HomieInitMsgs[] = 
{
  {"homie/"DEVNAME"/$homie","3.0",true},
  {"homie/"DEVNAME"/$name",devname,true},
  {"homie/"DEVNAME"/$nodes",nodename,true},  
  {"homie/"DEVNAME"/"NODENAME"/$name",nodename,true},
  {"homie/"DEVNAME"/"NODENAME"/$properties",proname,true},
  {"homie/"DEVNAME"/"NODENAME"/"PRONAME"/$name",proname,true},
  {"homie/"DEVNAME"/"NODENAME"/"PRONAME"/$settable","true",true},
  {"homie/"DEVNAME"/"NODENAME"/"PRONAME"/$retained","true",true},
  {"homie/"DEVNAME"/"NODENAME"/"PRONAME"/$datatype","boolean",true},
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

void WaitAndKeepAlive(int sec)
{
  for (int i=0;i<sec*10;i++)
  {
    mqtt_client.loop() ;
    delay(100) ;
  }
}

boolean setMqttState(const char* state)
{
  int qos = 2 ;
  log_printf( LOG_INFO, "homie/"DEVNAME"/$state ← %s\n",state ) ;  
  if (!mqtt_client.publish("homie/"DEVNAME"/$state",state,true, qos))
  {
      log_printf( LOG_ERR, "[setMqttState]mqtt_client.publish failed.\n" ) ;  
      return (false) ;
  }
}

boolean notifyRelayState(const char* state)
{
  int qos = 2 ;
  log_printf( LOG_INFO, "homie/"DEVNAME"/"NODENAME"/"PRONAME" ← %s\n",state ) ;  
  if (!mqtt_client.publish("homie/"DEVNAME"/"NODENAME"/"PRONAME,state,true, qos))
  {
      log_printf( LOG_ERR, "[notifyRelayState]mqtt_client.publish failed.\n" ) ;  
      return (false) ;
  }
}

boolean register_relay_homie_device()
{
  int qos = 2 ;

  int num_msg = sizeof(HomieInitMsgs)/sizeof(MqttMsg) ;
  for( int i=0;i<num_msg;i++ )
  {
    log_printf( LOG_INFO, "publish %s:%s\n", HomieInitMsgs[i].topic,HomieInitMsgs[i].payload ) ;  
    if (!mqtt_client.publish(HomieInitMsgs[i].topic,HomieInitMsgs[i].payload,HomieInitMsgs[i].retained,qos))
    {
      log_printf( LOG_INFO, "register_relay_homie_device() failed.\n" ) ;  
      return (false) ;
    }
  }

  notifyRelayState("false") ;

  // LWT message 
  log_printf( LOG_INFO, "Set LWT: $state ← disconnected\n" ) ;  
  mqtt_client.setWill( "homie/"DEVNAME"/$state","disconnected", true, qos ) ;
  
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

    // check for MqttServer completely initialized
//    if (mqtt_client.connect(devname))
//    {
      // disconnect and wait
//      log_printf(LOG_INFO, "Connected. Disconnect and wait 30s\n" ) ;      
//      mqtt_client.disconnect() ;
//      delay(30000) ;      
//    }
    
    if (mqtt_client.connect(devname))
    {
        
      mqtt_client.setOptions( 60, true, 2000 ) ;
      
      setMqttState("disconnected") ;  
      register_relay_homie_device() ;
  
      log_printf(LOG_INFO, "Wait 40s\n" ) ;      
      WaitAndKeepAlive(40) ;
            
      setMqttState("init") ;  
      if (register_relay_homie_device())
      {
        mqtt_client.subscribe( "homie/"DEVNAME"/"NODENAME"/"PRONAME"/set" ) ;
        setMqttState("ready") ;        
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
  if(!mqtt_client.loop())
  {
    log_printf( LOG_ERR, "loop failed\n" ) ;
    mqtt_client.disconnect() ;    
  }
}

void callback(MQTTClient *client, char topic[], char message[], int msg_len) 
{
  log_printf( LOG_INFO, "Message arrived on topic %s, length:%d\n", topic, msg_len ) ;  
  if (msg_len>0)
  {
      if (message[0]=='t')
      {
        notifyRelayState("true") ;
        digitalWrite(RELAY_PIN, LOW) ;   
      }
      else if (message[0]=='f')
      {
        notifyRelayState("false") ;
        digitalWrite(RELAY_PIN, HIGH) ; 
      }
      else
      {
      }
      
  }
}
