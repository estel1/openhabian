#include <Syslog.h>
#include <pins_arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <esp_system.h>
#include <Wire.h>
#include <math.h>

// WiFi credentials
const char* ssid                        = "Keenetic-0079" ;
const char* password                    = "yoHwLp6B" ;

#define DEVNAME "DHT11_2" 
const char* devname                     = DEVNAME ;

const char* mqtt_server                 = "192.168.1.54" ;
const char* syslog_server               = "192.168.1.54" ;

// pins_arduino.h
const int DHT_PIN         = T1 ;

WiFiClient espClient ;
PubSubClient client(espClient) ;
DHT dht(DHT_PIN,DHT11) ;

unsigned long lastMsg = 0 ;
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

WiFiUDP udpClient ;
Syslog syslog(udpClient, syslog_server, 514, devname, "monitor", LOG_KERN) ;

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

void setup() 
{
  pinMode(LED_BUILTIN, OUTPUT) ;
  digitalWrite(LED_BUILTIN, LOW) ;   // turn the LED off
  
  Serial.begin(115200) ;

  timer = timerBegin(0, 80, true) ;                  //timer 0, div 80
  timerAttachInterrupt(timer, &resetModule, true) ;  //attach callback
  timerAlarmWrite(timer, wdtTimeout * 1000, false) ; //set time in us
  timerAlarmEnable(timer) ;                          //enable interrupt
  
  setup_wifi() ;
  log_printf(LOG_INFO, "WiFi started!\n" ) ;
  client.setServer(mqtt_server, 1883) ;
  //client.setCallback(callback) ;

  dht.begin() ;
  
}

void setup_wifi() 
{
  delay(10) ;
  // We start by connecting to a WiFi network
  log_printf(LOG_INFO, "Connecting to %s", ssid ) ;

  WiFi.begin(ssid, password) ;

  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500) ;
    Serial.print(".") ;
  }

  log_printf( LOG_INFO, "Wifi connected. IP address: %s\n", WiFi.localIP().toString().c_str(  ) ) ;  
}

boolean register_dht_homie_device()
{  
  if (!client.publish("homie/"DEVNAME"/$state","init", true ))
  {
    log_printf(LOG_ERR, "register_homie_device() failed. - can't publish homie state.\n" ) ;
    return (false) ; 
  }
  client.publish("homie/"DEVNAME"/$homie","3.0",true) ;
  client.publish("homie/"DEVNAME"/$name",devname,true) ;
  client.publish("homie/"DEVNAME"/$nodes","Thermometer,Hydrometer",true) ;
  
  client.publish("homie/"DEVNAME"/Thermometer/$name","Thermometer1floor",true) ;
  client.publish("homie/"DEVNAME"/Thermometer/$properties","temperature",true) ;

  client.publish("homie/"DEVNAME"/Thermometer/temperature/$name","Temperature",true) ;
  client.publish("homie/"DEVNAME"/Thermometer/temperature/$unit","°C",true) ;
  client.publish("homie/"DEVNAME"/Thermometer/temperature/$datatype","float",true) ;

  client.publish("homie/"DEVNAME"/Hydrometer/$name","Hydrometer1floor",true) ;
  client.publish("homie/"DEVNAME"/Hydrometer/$properties","humidity",true) ;

  client.publish("homie/"DEVNAME"/Hydrometer/humidity/$name","Humidity",true) ;
  client.publish("homie/"DEVNAME"/Hydrometer/humidity/$unit","mm Hg",true) ;
  client.publish("homie/"DEVNAME"/Hydrometer/humidity/$datatype","float",true) ;

  client.publish("homie/"DEVNAME"/$state","ready",true) ;
  
  log_printf( LOG_INFO, "Homie device %s registered\n", DEVNAME ) ;  
  return (true) ;
}

void reconnect() 
{
  // Loop until we're reconnected
  while (!client.connected()) 
  {
    digitalWrite(LED_BUILTIN, LOW) ;   // turn the LED off
    Serial.print("Attempting MQTT connection...") ;
    // Attempt to connect
    if (client.connect(devname)) 
    {
      digitalWrite(LED_BUILTIN, HIGH) ;   // turn the LED off
      Serial.println("connected") ;
      register_dht_homie_device() ;
    } 
    else 
    {
      Serial.print("failed, rc=") ;
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds") ;
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void loop() 
{  

  timerWrite(timer, 0) ; //reset timer

  if (!client.connected()) 
  {
    reconnect() ;
  }
  client.loop() ;

  unsigned long now = millis() ;
  if (now - lastMsg > 30000) 
  {
    lastMsg = now ;

    // DHT11 Sensor

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
    client.publish("homie/"DEVNAME"/Thermometer/temperature", tempMessage.c_str(), true ) ;
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
    client.publish("homie/"DEVNAME"/Hydrometer/humidity", humidityMessage.c_str(),true ) ;
  }
}


