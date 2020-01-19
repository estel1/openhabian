#include <Syslog.h>
#include <pins_arduino.h>
#include <WiFi.h>
#include <MQTT.h>
#include <DHT.h>
#include <esp_system.h>
#include <Wire.h>
#include <math.h>
#include "SSD1306.h"

#define DEVNAME     "TTGO_term" 
const char* devname                       = DEVNAME ;
#define NODENAME    "Thermometer"
const char* nodename                      = NODENAME ;
#define PRONAME     "Temperature"
const char* proname                       = PRONAME ;
#define DEVSTATE    "homie/"DEVNAME"/$state"
#define DEVPRO      "homie/"DEVNAME"/"NODENAME"/"PRONAME

const int publishQos      = 2 ;
// pins_arduino.h
const int DHT_PIN         = T0 ;
float     temperature     = 0 ;
float     humidity        = 0 ;

DHT       dht( DHT_PIN, DHT22 ) ;

#define OLED_ADDRESS 0x3c
#define I2C_SDA 14
#define I2C_SCL 13
SSD1306Wire display( OLED_ADDRESS, I2C_SDA, I2C_SCL, GEOMETRY_128_32 ) ;


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
  {"homie/"DEVNAME"/"NODENAME"/"PRONAME"/$settable","false",true},
  {"homie/"DEVNAME"/"NODENAME"/"PRONAME"/$retained","true",true},
  {"homie/"DEVNAME"/"NODENAME"/"PRONAME"/$unit","°C",true},
  {"homie/"DEVNAME"/"NODENAME"/"PRONAME"/$datatype","float",true},
} ;

// Replace the next variables with your SSID/Password combination
const char*     ssid                        = "Keenetic-0079" ;
const char*     password                    = "yoHwLp6B" ;

const char*     mqtt_server                 = "192.168.1.54" ;
const int       mqtt_port                   = 1883 ;
const char*     syslog_server               = "192.168.1.54" ;


WiFiClient wifi_client ;
MQTTClient mqtt_client ;
WiFiUDP udpClient ;
Syslog syslog(udpClient, syslog_server, 514, devname, "monitor", LOG_KERN) ;

unsigned long lastMsg                   = 0 ;
unsigned long lastRegistered            = 0 ;
char msg[50] ;
int value = 0 ;


int restartCount = 0 ;
const int wdtTimeout = 15000 ;  //time in ms to trigger the watchdog
hw_timer_t *timer = NULL ;
int bmeInitialized = 0 ;

void IRAM_ATTR resetModule() 
{
  ets_printf("reboot\n") ;
  restartCount++ ;
  esp_restart() ;
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

boolean register_dht_homie_device()
{  

  //log_printf( LOG_INFO, "Wait before initializing...\n" ) ;  
  //delay(10000) ;
  
  // Disconnect device
  log_printf( LOG_INFO, "homie/"DEVNAME"/$state ← disconnected\n" ) ;  
  if (!mqtt_client.publish("homie/"DEVNAME"/$state","disconnected",true, publishQos))
  {
      log_printf( LOG_ERR, "[register_dht_homie_device]mqtt_client.publish failed.\n" ) ;  
      return (false) ;
  }
  delay(5000) ;

  // Start initialization
  if (!mqtt_client.publish("homie/"DEVNAME"/$state","init",true, publishQos))
  {
      log_printf( LOG_ERR, "[register_dht_homie_device]mqtt_client.publish failed.\n" ) ;  
      return (false) ;
  }

  int num_msg = sizeof(HomieInitMsgs)/sizeof(MqttMsg) ;
  for( int i=0;i<num_msg;i++ )
  {
    log_printf( LOG_INFO, "publish %s:%s\n", HomieInitMsgs[i].topic,HomieInitMsgs[i].payload ) ;  
    if (!mqtt_client.publish(HomieInitMsgs[i].topic,HomieInitMsgs[i].payload,HomieInitMsgs[i].retained, publishQos))
    {
      log_printf( LOG_INFO, "register_dht_homie_device() failed.\n" ) ;  
      return (false) ; 
    }
  }

  // LWT message 
  log_printf( LOG_INFO, "Set LWT: $state ← lost\n" ) ;  
  mqtt_client.setWill( "homie/"DEVNAME"/$state","lost", true, publishQos ) ;
  
  delay(5000) ;
  // complete initialization
  log_printf( LOG_INFO, "homie/"DEVNAME"/$state ← ready\n" ) ;  
  if (!mqtt_client.publish("homie/"DEVNAME"/$state","ready",true, publishQos))
  {
      log_printf( LOG_ERR, "[register_dht_homie_device]mqtt_client.publish failed.\n" ) ;  
      return (false) ;
  }

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
      // disconnect and wait
      log_printf(LOG_INFO, "Connected. Disconnect and wait 30s\n" ) ;      
      mqtt_client.disconnect() ;
      delay(30000) ;      
    }
    
    if (mqtt_client.connect(devname))
    {
      if (register_dht_homie_device())
      {
        digitalWrite(LED_BUILTIN, HIGH) ;   // turn the LED on
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

  Serial.begin(115200) ;
  WiFi.begin(ssid, password) ;
  mqtt_client.begin( mqtt_server, mqtt_port, wifi_client ) ;
  dht.begin() ;
  connect() ;

  display.init() ;
  display.flipScreenVertically() ;
  display.setFont(ArialMT_Plain_16) ;
  display.setTextAlignment(TEXT_ALIGN_CENTER) ;
  display.drawString(128 / 2, 32 / 2, WiFi.localIP().toString()) ;
  display.display() ;

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

  unsigned long now = millis() ;
  if (now - lastMsg > 30000) 
  {
    lastMsg = now ;
    float value = dht.readTemperature() ;
    if (isnan(value))
    {
      log_printf(LOG_INFO, "DHT11 Temperature is: NaN. Skip measurement\n" ) ;      
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
    mqtt_client.publish("homie/"DEVNAME"/Hydrometer/humidity", humidityMessage.c_str(),true,publishQos ) ;
  }
}

boolean notifyTempValue(float Temp)
{
  String tempMessage( Temp, 2 ) ;
  log_printf( LOG_INFO, DEVPRO" ← %s\n",tempMessage.c_str() ) ;  
  if (!mqtt_client.publish(DEVPRO,tempMessage.c_str(),true, publishQos ))
  {
      log_printf( LOG_ERR, "[notifyRelayState]mqtt_client.publish failed.\n" ) ;  
      return (false) ;
  }

  display.init() ;
  display.flipScreenVertically() ;
  display.setFont(ArialMT_Plain_24) ;
  display.setTextAlignment(TEXT_ALIGN_CENTER) ;
  display.drawString(128 / 2, 1, tempMessage.c_str() ) ;
  display.display() ;

}
