#include <Syslog.h>
#include <pins_arduino.h>
#include <WiFi.h>
#include <MQTT.h>
#include <DHT.h>
#include <esp_system.h>
#include <Wire.h>
#include <math.h>

#define DEVNAME "esp32postrelay" 
const char* devname                       = DEVNAME ;
#define NODENAME    "relay"
const char* nodename                      = NODENAME ;
#define NODENAME2   "switch"
const char* nodename2                     = NODENAME2 ;
#define NODENAME3   "Thermometer"
const char* nodename3                     = NODENAME3 ;
#define PRONAME     "power"
const char* proname                       = PRONAME ;
#define PRONAME2    "state"
const char* proname2                      = PRONAME2 ;
#define PRONAME3    "Temperature"
const char* proname3                      = PRONAME3 ;
#define DEVSTATE    "homie/"DEVNAME"/$state"
#define DEVPRO      "homie/"DEVNAME"/"NODENAME"/"PRONAME
#define DEVPRO2     "homie/"DEVNAME"/"NODENAME2"/"PRONAME2
#define DEVPROSET   "homie/"DEVNAME"/"NODENAME"/"PRONAME"/set"
#define DEVPRO3     "homie/"DEVNAME"/"NODENAME3"/"PRONAME3

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
  {"homie/"DEVNAME"/$nodes",NODENAME","NODENAME2","NODENAME3,true},
  {"homie/"DEVNAME"/"NODENAME"/$name",nodename,true},
  {"homie/"DEVNAME"/"NODENAME"/$properties",proname,true},
  {"homie/"DEVNAME"/"NODENAME"/"PRONAME"/$name",proname,true},
  {"homie/"DEVNAME"/"NODENAME"/"PRONAME"/$settable","true",true},
  {"homie/"DEVNAME"/"NODENAME"/"PRONAME"/$retained","true",true},
  {"homie/"DEVNAME"/"NODENAME"/"PRONAME"/$datatype","boolean",true},
  {"homie/"DEVNAME"/"NODENAME2"/$name",nodename2,true},
  {"homie/"DEVNAME"/"NODENAME2"/$properties",proname2,true},
  {"homie/"DEVNAME"/"NODENAME2"/"PRONAME2"/$name",proname2,true},
  {"homie/"DEVNAME"/"NODENAME2"/"PRONAME2"/$settable","false",true},
  {"homie/"DEVNAME"/"NODENAME2"/"PRONAME2"/$retained","true",true},
  {"homie/"DEVNAME"/"NODENAME2"/"PRONAME2"/$datatype","boolean",true},
  {"homie/"DEVNAME"/"NODENAME3"/$name",nodename3,true},
  {"homie/"DEVNAME"/"NODENAME3"/$properties",proname3,true},
  {"homie/"DEVNAME"/"NODENAME3"/"PRONAME3"/$name",proname3,true},
  {"homie/"DEVNAME"/"NODENAME3"/"PRONAME3"/$settable","false",true},
  {"homie/"DEVNAME"/"NODENAME3"/"PRONAME3"/$retained","true",true},
  {"homie/"DEVNAME"/"NODENAME3"/"PRONAME3"/$unit","°C",true},
  {"homie/"DEVNAME"/"NODENAME3"/"PRONAME3"/$datatype","float",true},
} ;

// WiFi credentials
const char* ssid                        = "Keenetic-0079" ;
const char* password                    = "yoHwLp6B" ;

const char* mqtt_server                 = "192.168.1.54" ;
const int   mqtt_port                   = 1883 ;
const char* syslog_server               = "192.168.1.54" ;

// pins_arduino.h
const int RELAY_PIN                     = T0 ;
const int DHT_PIN                       = T1 ;
const int BTN_ON_PIN                    = T8 ;
const int BTN_OFF_PIN                   = T9 ;

const int KEYB_ON_PRESS_TIME            = 250 ; // Time to detect on btn pressed
const int KEYB_OFF_PRESS_TIME           = 150 ; // Time to detect off btn pressed

WiFiClient wifi_client ;
MQTTClient mqtt_client ;
DHT dht(DHT_PIN,DHT22) ;
WiFiUDP udpClient ;
Syslog syslog(udpClient, syslog_server, 514, devname, "monitor", LOG_KERN) ;

unsigned long lastMsg                   = 0 ;
unsigned long lastRegistered            = 0 ;
char msg[50] ;
int value = 0 ;

float temperature                       = 0 ;
float humidity                          = 0 ;
int   relayState                        = -1 ;

int restartCount                        = 0 ;
const int wdtTimeout                    = 15000 ;  //time in ms to trigger the watchdog
hw_timer_t *timer                       = NULL ;
int bmeInitialized                      = 0 ;

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

boolean notifySwitchState(const char* state)
{
  log_printf( LOG_INFO, DEVPRO2" ← %s\n",state ) ;  
  if (!mqtt_client.publish(DEVPRO2,state,true, publishQos ))
  {
      log_printf( LOG_ERR, "[notifySwitchState]mqtt_client.publish failed.\n" ) ;  
      return (false) ;
  }
  return (true) ;
}

boolean notifyTempValue(float Temp)
{
  String tempMessage( Temp, 2 ) ;
  log_printf( LOG_INFO, DEVPRO3" ← %s\n",tempMessage.c_str() ) ;  
  if (!mqtt_client.publish(DEVPRO3,tempMessage.c_str(),true, publishQos ))
  {
      log_printf( LOG_ERR, "[notifyTempValue]mqtt_client.publish failed.\n" ) ;  
      return (false) ;
  }
}

boolean sendRelayCmd(const char* cmd)
{
  log_printf( LOG_INFO, DEVPROSET" ← %s\n",cmd ) ;  
  if (!mqtt_client.publish(DEVPROSET,cmd,true, publishQos ))
  {
      log_printf( LOG_ERR, "[sendRelayCmd]mqtt_client.publish failed.\n" ) ;  
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
  pinMode(BTN_ON_PIN,INPUT_PULLUP) ;
  pinMode(BTN_OFF_PIN,INPUT_PULLUP) ;
  digitalWrite(LED_BUILTIN, LOW) ;   // turn the LED off
  digitalWrite(RELAY_PIN, HIGH) ; // turn off relay 
  
  Serial.begin(115200) ;
  WiFi.begin(ssid, password) ;
  mqtt_client.begin( mqtt_server, mqtt_port, wifi_client ) ;
  mqtt_client.onMessageAdvanced( callback ) ;
  dht.begin() ;
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


  // keyboard processing
  bool btnOn    = false ;
  bool btnOff   = false ;

  int msPressed = 0 ;
  while( digitalRead( BTN_ON_PIN )==LOW )
  {        
    delay( 10 ) ; msPressed += 10 ;
    mqtt_client.loop() ;
    if (msPressed>KEYB_ON_PRESS_TIME)
    {
      break ;        
    }
  }
  btnOn = msPressed>KEYB_ON_PRESS_TIME ;

  msPressed = 0 ;
  while( digitalRead( BTN_OFF_PIN )==HIGH )
  {        
    delay( 10 ) ; msPressed += 10 ;
    mqtt_client.loop() ;
    if (msPressed>KEYB_OFF_PRESS_TIME)
    {
      break ;        
    }
  }
  btnOff = msPressed>KEYB_OFF_PRESS_TIME ;

  if (btnOn && btnOff)
  { 
    // both btn pressed - do nothing   
    log_printf( LOG_INFO, "Disabled button combination.\n" ) ;  
  }
  else if (btnOn)
  {
    if ( relayState!=1 )
    {
      sendRelayCmd("true") ;
      log_printf( LOG_INFO, "BTN_ON pressed\n." ) ;  
    }  
  }
  else if (btnOff)
  {
    if ( relayState!=0 )
    {
      sendRelayCmd("false") ;
      log_printf( LOG_INFO, "BTN_OFF pressed\n." ) ;  
    }  
  }

  unsigned long now = millis() ;
  if (now - lastMsg > 30000) 
  {
    lastMsg = now ;
    float value = dht.readTemperature() ;
    if (isnan(value))
    {
      log_printf(LOG_INFO, "DHT Temperature is: NaN. Skip measurement\n" ) ;      
    }
    else
    {
      temperature = value ;
    }
    notifyTempValue(temperature) ;

    value = dht.readHumidity() ;
    if (isnan(value))
    {
      log_printf(LOG_INFO, "DHT11 Humidity is: NaN. Skip measurement\n" ) ;      
    }
    else
    {
      humidity = value ;
    }
    String humidityMessage( humidity, 2 ) ;
    log_printf(LOG_INFO, "DHT11 Humidity is: %s %%\n", humidityMessage.c_str() ) ;    
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
        notifySwitchState("true") ;
        digitalWrite(RELAY_PIN, LOW) ;   
        relayState = 1 ;   // somebody switch relay on
     }
      else if (message[0]=='f')
      {
        notifyRelayState("false") ;
        notifySwitchState("false") ;
        digitalWrite(RELAY_PIN, HIGH) ; 
        relayState = 0 ;   // somebody switch relay on
      }
      else
      {
      }
      
  }
}
