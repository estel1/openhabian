#include <Syslog.h>
#include <pins_arduino.h>
#include <WiFi.h>
#include <MQTT.h>
#include <DHT.h>
#include <esp_system.h>
#include <Wire.h>
#include <math.h>

#define DEVNAME "boiler" 
const char* devname                       = DEVNAME ;
#define NODENAME    "relay"
const char* nodename                      = NODENAME ;
#define PRONAME     "power"
const char* proname                       = PRONAME ;
#define DEVSTATE    "homie/"DEVNAME"/$state"
#define DEVPRO      "homie/"DEVNAME"/"NODENAME"/"PRONAME
#define DEVPROSET   "homie/"DEVNAME"/"NODENAME"/"PRONAME"/set"

struct MqttMsg
{ 
  const char* topic ;
  const char* payload ;
  boolean retained ;
} ;

const int publishQos = 2 ;

MqttMsg HomieInitMsgs[] = 
{
  {"homie/"DEVNAME"/$homie","3.0",true},
  {"homie/"DEVNAME"/$name",devname,true},  
  {"homie/"DEVNAME"/$nodes",NODENAME,true},
  {"homie/"DEVNAME"/"NODENAME"/$name",nodename,true},
  {"homie/"DEVNAME"/"NODENAME"/$properties",proname,true},
  {"homie/"DEVNAME"/"NODENAME"/"PRONAME"/$name",proname,true},
  {"homie/"DEVNAME"/"NODENAME"/"PRONAME"/$settable","true",true},
  {"homie/"DEVNAME"/"NODENAME"/"PRONAME"/$retained","true",true},
  {"homie/"DEVNAME"/"NODENAME"/"PRONAME"/$datatype","boolean",true},
} ;

// WiFi credentials
const char* ssid                        = "Keenetic-0079" ;
const char* password                    = "yoHwLp6B" ;

const char* mqtt_server                 = "192.168.1.54" ;
const int   mqtt_port                   = 1883 ;
const char* syslog_server               = "192.168.1.54" ;

// pins_arduino.h
const int RELAY_PIN                     = T1 ;

WiFiClient wifi_client ;
MQTTClient mqtt_client ;
WiFiUDP udpClient ;
Syslog syslog(udpClient, syslog_server, 514, devname, "monitor", LOG_KERN) ;

unsigned long lastMsg                   = 0 ;
unsigned long lastRegistered            = 0 ;
char msg[50] ;
int value = 0 ;

int restartCount                        = 0 ;
const int wdtTimeout                    = 15000 ;  //time in ms to trigger the watchdog
hw_timer_t *timer                       = NULL ;

void IRAM_ATTR resetModule() 
{
  ets_printf("reboot\n") ;
  restartCount++ ;
  //esp_restart_noos() ;
}

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
  log_printf( LOG_INFO, DEVSTATE" ← %s\n",state ) ;  
  if (!mqtt_client.publish(DEVSTATE,state,true, publishQos ))
  {
      log_printf( LOG_ERR, "[setMqttState]mqtt_client.publish failed.\n" ) ;  
      return (false) ;
  }
  return (true) ;
}

boolean notifyRelayState(const char* state)
{
  log_printf( LOG_INFO, DEVPRO" ← %s\n",state ) ;  
  if (!mqtt_client.publish(DEVPRO,state,true, publishQos ))
  {
      log_printf( LOG_ERR, "[notifyRelayState]mqtt_client.publish failed.\n" ) ;  
      return (false) ;
  }
  return (true) ;
}

boolean register_relay_homie_device()
{

  int num_msg = sizeof(HomieInitMsgs)/sizeof(MqttMsg) ;
  for( int i=0;i<num_msg;i++ )
  {
    log_printf( LOG_INFO, "publish %s:%s\n", HomieInitMsgs[i].topic,HomieInitMsgs[i].payload ) ;  
    if (!mqtt_client.publish(HomieInitMsgs[i].topic,HomieInitMsgs[i].payload,HomieInitMsgs[i].retained,publishQos))
    {
      log_printf( LOG_INFO, "register_relay_homie_device() failed.\n" ) ;  
      return (false) ;
    }
  }

  notifyRelayState("false") ;

  // LWT message 
  log_printf( LOG_INFO, "Set LWT: $state ← disconnected\n" ) ;  
  mqtt_client.setWill( DEVSTATE, "disconnected", true, publishQos ) ;
  
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
    digitalWrite(LED_BUILTIN, LOW) ;   // turn the LED off

    // check for MqttServer completely initialized
    if (mqtt_client.connect(devname))
    {
      setMqttState( "disconnected" ) ;      
      mqtt_client.disconnect() ;
      delay(5000) ;      
    }
    
    if (mqtt_client.connect(devname))
    {
      //mqtt_client.setOptions( 60, true, 2000 ) ;
      
      setMqttState("disconnected") ;  
      register_relay_homie_device() ;
  
      log_printf(LOG_INFO, "Wait 30s\n" ) ;      
      WaitAndKeepAlive(30) ;
            
      setMqttState("init") ;  
      if (register_relay_homie_device())
      {
        mqtt_client.subscribe( DEVPROSET ) ;
        setMqttState("ready") ;  
        digitalWrite(LED_BUILTIN, HIGH) ;   // turn the LED off
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
  pinMode(LED_BUILTIN, OUTPUT) ;
  pinMode(RELAY_PIN, OUTPUT) ;
  digitalWrite(LED_BUILTIN, LOW) ;   // turn the LED off
  digitalWrite(RELAY_PIN, LOW ) ; // turn off relay 
  
  Serial.begin(115200) ;
  WiFi.begin(ssid, password) ;
  mqtt_client.begin( mqtt_server, mqtt_port, wifi_client ) ;
  mqtt_client.onMessageAdvanced( callback ) ;
  connect() ;

  timer = timerBegin(0, 80, true) ;                  //timer 0, div 80
  timerAttachInterrupt(timer, &resetModule, true) ;  //attach callback
  timerAlarmWrite(timer, wdtTimeout * 1000, false) ; //set time in us
  timerAlarmEnable(timer) ;                          //enable interrupt
}

void loop() 
{  
  timerWrite(timer, 0) ; //reset timer
  
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
        notifyRelayState("true") ;
        digitalWrite(RELAY_PIN, HIGH ) ;   
     }
     else if (message[0]=='f')
     {
        notifyRelayState("false") ;
        digitalWrite(RELAY_PIN, LOW ) ; 
     }
     else
     {
     }
  }
}
