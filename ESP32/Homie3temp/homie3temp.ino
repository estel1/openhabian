#include <Syslog.h>
#include <pins_arduino.h>
#include <WiFi.h>
#include <MQTT.h>
#include <DHT.h>
#include <esp_system.h>
#include <Wire.h>
#include <math.h>

#define DEVNAME "DHT11_2" 
const char* devname                     = DEVNAME ;

struct MqttMsg
{ 
  const char* topic ;
  const char* payload ;
  boolean retained ;
} ;


MqttMsg HomieInitMsgs[] = 
{
  {"homie/"DEVNAME"/$state","init", true},
  {"homie/"DEVNAME"/$homie","3.0",true},
  {"homie/"DEVNAME"/$name",devname,true},
  
  {"homie/"DEVNAME"/$nodes","Thermometer,Hydrometer",true},

  {"homie/"DEVNAME"/Thermometer/$name","Termometer",true},
  {"homie/"DEVNAME"/Thermometer/$properties","temperature",true},

  {"homie/"DEVNAME"/Thermometer/temperature/$name","Temperature",true},
  {"homie/"DEVNAME"/Thermometer/temperature/$unit","°C",true},
  {"homie/"DEVNAME"/Thermometer/temperature/$datatype","float",true},

  {"homie/"DEVNAME"/Hydrometer/$name","Hydrometer1floor",true},
  {"homie/"DEVNAME"/Hydrometer/$properties","humidity",true},

  {"homie/"DEVNAME"/Hydrometer/humidity/$name","Humidity",true},
  {"homie/"DEVNAME"/Hydrometer/humidity/$unit","mm Hg",true},
  {"homie/"DEVNAME"/Hydrometer/humidity/$datatype","float",true},

  {"homie/"DEVNAME"/$state","ready",true}
  
} ;

// WiFi credentials
const char* ssid                        = "Keenetic-0079" ;
const char* password                    = "yoHwLp6B" ;

const char* mqtt_server                 = "192.168.1.54" ;
const int   mqtt_port                   = 1883 ;
const char* syslog_server               = "192.168.1.54" ;

// pins_arduino.h
const int DHT_PIN                       = T1 ;

WiFiClient wifi_client ;
MQTTClient mqtt_client ;
DHT dht(DHT_PIN,DHT11) ;
WiFiUDP udpClient ;
Syslog syslog(udpClient, syslog_server, 514, devname, "monitor", LOG_KERN) ;

unsigned long lastMsg                   = 0 ;
unsigned long lastRegistered            = 0 ;
char msg[50] ;
int value = 0 ;

float temperature = 0 ;
float humidity = 0 ;

int restartCount = 0 ;
const int wdtTimeout = 15000 ;  //time in ms to trigger the watchdog
hw_timer_t *timer = NULL ;
int bmeInitialized = 0 ;

void IRAM_ATTR resetModule() 
{
  ets_printf("reboot\n") ;
  restartCount++ ;
  esp_restart_noos() ;
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
  int qos = 2 ;
  int num_msg = sizeof(HomieInitMsgs)/sizeof(MqttMsg) ;
  for( int i=0;i<num_msg;i++ )
  {
    log_printf( LOG_INFO, "publish %s:%s\n", HomieInitMsgs[i].topic,HomieInitMsgs[i].payload ) ;  
    while (!mqtt_client.publish(HomieInitMsgs[i].topic,HomieInitMsgs[i].payload,HomieInitMsgs[i].retained, qos))
    {
      log_printf( LOG_INFO, "register_dht_homie_device() failed.\n" ) ;  
      return (false) ; 
    }
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
    if (mqtt_client.connect(devname))
    {
      if (register_dht_homie_device())
      {
        digitalWrite(LED_BUILTIN, HIGH) ;   // turn the LED on
        break ;
      }
    }
    delay(1000);
    log_printf(LOG_INFO, ".") ;
  }
}

void setup() 
{
  delay(10) ;
  pinMode(LED_BUILTIN, OUTPUT) ;
  digitalWrite(LED_BUILTIN, LOW) ;   // turn the LED off
  
  Serial.begin(115200) ;
  WiFi.begin(ssid, password) ;
  mqtt_client.begin( mqtt_server, mqtt_port, wifi_client ) ;
  dht.begin() ;
  connect() ;

  timer = timerBegin(0, 80, true) ;                  //timer 0, div 80
  timerAttachInterrupt(timer, &resetModule, true) ;  //attach callback
  timerAlarmWrite(timer, wdtTimeout * 1000, false) ; //set time in us
  timerAlarmEnable(timer) ;                          //enable interrupt
}

void loop() 
{  
  int qos = 2 ;

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
    String tempMessage( temperature, 2 ) ;
    mqtt_client.publish("homie/"DEVNAME"/Thermometer/temperature", tempMessage.c_str(), true, qos ) ;
    log_printf(LOG_INFO, "DHT11 Temperature is: %s °C\n", tempMessage.c_str() ) ;

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
    mqtt_client.publish("homie/"DEVNAME"/Hydrometer/humidity", humidityMessage.c_str(),true,qos ) ;
  }
}


