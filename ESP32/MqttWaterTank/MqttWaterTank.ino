#include <Syslog.h>
#include <pins_arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <ESPmDNS.h>
#include <WebServer.h>   // Include the WebServer library
#include <esp_system.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>

// this code is based on example:
// https://www.instructables.com/id/Distance-Measurement-Using-HC-SR04-Via-NodeMCU/

// other resources:
// http://esp32.net/images/Ai-Thinker/NodeMCU-32S/Ai-Thinker_NodeMCU-32S_DiagramSchematic.png
// http://esp32.net/images/Ai-Thinker/NodeMCU-32S/Ai-Thinker_NodeMCU-32S_DiagramPinout.png
// https://einstronic.com/wp-content/uploads/2017/06/NodeMCU-32S-Catalogue.pdf

// Replace the next variables with your SSID/Password combination
const char* ssid                        = "Keenetic-0079" ;
const char* password                    = "yoHwLp6B" ;
const char* mqtt_server                 = "192.168.1.54" ;
const char* syslog_server               = "192.168.1.54" ;
const char* mqtt_client                 = "watertank" ;
const int   tank_height                 = 80 ; // Tank height, cm

// MQTT values
const char* WATERTANK_WATER_LEVEL       = "watertank/level" ;
const char* WATERTANK_EMPTY_SPACE       = "watertank/empty_space" ;
const char* WATERTANK_DHT11_TEMPERATURE = "watertank/temperature" ;
const char* WATERTANK_DHT11_HUMIDITY    = "watertank/humidity" ;
const char* WATERTANK_BME_TEMPERATURE   = "watertank/bme_temp" ;
const char* WATERTANK_BME_PRESSURE      = "watertank/pressure" ;
const char* WATERTANK_BME_ALTITUDE      = "watertank/altitude" ;
const char* WATERTANK_BME_HUMIDITY      = "watertank/bme_humidity" ;

// pins_arduino.h
const int DHT_PIN         = T1 ;
const int trigPin         = T8 ;
const int echoPin         = T9 ;

WiFiClient espClient ;
PubSubClient client(espClient) ;
DHT dht(DHT_PIN,DHT11) ;
Adafruit_BMP280 bme ; // I2C
WebServer server(80) ;

unsigned long lastMsg = 0 ;
unsigned long lastMsgBme = 0 ;
char msg[50] ;
int value = 0 ;

float temperature = 0 ;
float humidity = 0 ;

void handleRoot() ;              // function prototypes for HTTP handlers
void handleNotFound() ;

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
Syslog syslog(udpClient, syslog_server, 514, "watertank", "monitor", LOG_KERN) ;

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
  pinMode(trigPin,OUTPUT) ;
  pinMode(echoPin,INPUT) ;
  digitalWrite(LED_BUILTIN, LOW) ;   // turn the LED off
  
  Serial.begin(115200) ;

  timer = timerBegin(0, 80, true) ;                  //timer 0, div 80
  timerAttachInterrupt(timer, &resetModule, true) ;  //attach callback
  timerAlarmWrite(timer, wdtTimeout * 1000, false) ; //set time in us
  timerAlarmEnable(timer) ;                          //enable interrupt
  
  setup_wifi() ;
  client.setServer(mqtt_server, 1883) ;
  //client.setCallback(callback) ;

  if (MDNS.begin("watertank")) 
  {
    Serial.println("mDNS responder started") ;
  } 
  else 
  {
    Serial.println("Error setting up MDNS responder!") ;
  }  
  
  server.on("/", handleRoot) ;               // Call the 'handleRoot' function when a client requests URI "/"
  server.onNotFound(handleNotFound) ;        // When a client requests an unknown URI (i.e. something other than "/"), call function "handleNotFound"
  server.begin() ;                           // Actually start the server
  Serial.println("HTTP server started") ;
  
  dht.begin() ;

  if (bme.begin())
  {
    bmeInitialized = 1 ;
    log_printf(LOG_INFO, "BME initialized Ok!\n" ) ;
  } 
  else
  {
    log_printf(LOG_ERR, "Could not find a valid BME/BMP280 sensor, check wiring!\n" ) ;
  }
  
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

  log_printf( LOG_INFO, "Wifi connected. IP address: %s\n", WiFi.localIP() ) ;  
  
}

void reconnect() 
{
  // Loop until we're reconnected
  while (!client.connected()) 
  {
    digitalWrite(LED_BUILTIN, LOW) ;   // turn the LED off
    Serial.print("Attempting MQTT connection...") ;
    // Attempt to connect
    if (client.connect(mqtt_client)) 
    {
      digitalWrite(LED_BUILTIN, HIGH) ;   // turn the LED off
      Serial.println("connected") ;
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
  server.handleClient() ;                    // Listen for HTTP requests from clients

  unsigned long now = millis() ;
  if (now - lastMsg > 5000) 
  {
    lastMsg = now ;

    // https://www.instructables.com/id/Distance-Measurement-Using-HC-SR04-Via-NodeMCU/

    float fdist = 0.0 ;
    for (int i=0;i<5;i++)
    {
      // Ultrasonic Sensor
      // Clears the trigPin
      digitalWrite(trigPin, LOW) ;
      delayMicroseconds(2) ;
  
      // Sets the trigPin on HIGH state for 10 micro seconds
      digitalWrite(trigPin, HIGH) ;
      delayMicroseconds(10) ;
      digitalWrite(trigPin, LOW) ;
  
      // Reads the echoPin, returns the sound wave travel time in microseconds
      long duration = pulseIn(echoPin, HIGH) ;
  
      // Calculating the distance
      fdist += duration*0.034/2 ;

      delay(100) ;
      
    }
    int distance = fdist/5.0 ;
    distance -= 3 ; //remove 
    
    // Prints the distance on the Serial Monitor
    log_printf(LOG_INFO, "Distance is: %d cm\n", distance ) ;
    int water_level = 100.0*(tank_height-distance)/tank_height ;
    if (water_level<0)
    {
      water_level = 0 ;
    }
    else if (water_level>100)
    {
      water_level = 100 ;
    }
     
    String message ( water_level, 0 ) ;
    log_printf(LOG_INFO, "Water level is: %s cm\n", message.c_str() ) ;
    client.publish(WATERTANK_WATER_LEVEL, message.c_str() ) ;

    message = "" ; message += distance ;
    log_printf(LOG_INFO, "Distance is: %s cm\n", message.c_str() ) ;
    client.publish(WATERTANK_EMPTY_SPACE, message.c_str() ) ;
    
    // DHT11 Sensor

    temperature = dht.readTemperature() ;
    String tempMessage( temperature, 2 ) ;
    log_printf(LOG_INFO, "DHT11 Temperature is: %s C\n", tempMessage.c_str() ) ;
    client.publish(WATERTANK_DHT11_TEMPERATURE, tempMessage.c_str() ) ;

    humidity = dht.readHumidity() ;
    String humidityMessage( humidity, 2 ) ;
    log_printf(LOG_INFO, "DHT11 Humidity is: %s %\n", humidityMessage.c_str() ) ;    
    client.publish(WATERTANK_DHT11_HUMIDITY "watertank/humidity", humidityMessage.c_str() ) ;

  }
  
  if ( (now - lastMsgBme)>10000 )
  {
    lastMsgBme = now ;    
    if (bmeInitialized)
    {
      float bmeTemp = bme.readTemperature() ; 
      float bmePress = bme.readPressure() ;
      float bmeAlt = bme.readAltitude(1013.25) ;
      float bmeHumidity = bme.readHumidity() ;
      
      if (bmeAlt<400) // check for valid measures
      {
        
        String strTemp (bmeTemp,2) ;
        log_printf(LOG_INFO, "WATERTANK_BME_TEMPERATURE is: %s C\n", tempMessage.c_str() ) ;
        client.publish(WATERTANK_BME_TEMPERATURE, strTemp.c_str() ) ;

        String strPressure (bmePress/133.322,1) ;
        log_printf(LOG_INFO, "WATERTANK_BME_PRESSURE is: %s mmHg\n", strPressure.c_str() ) ;
        client.publish(WATERTANK_BME_PRESSURE, strPressure.c_str() ) ;

        String strAlt ( bmeAlt,2 ) ;
        log_printf(LOG_INFO, "WATERTANK_BME_ALTITUDE is: %s m\n", strAlt.c_str() ) ;
        client.publish(WATERTANK_BME_ALTITUDE, strAlt.c_str() ) ;
        
        String strHumidity ( bmeHumidity,1 ) ;
        log_printf(LOG_INFO, "BME Humidity is: %s %\n", strHumidity.c_str() ) ;
        client.publish(WATERTANK_BME_HUMIDITY, strHumidity.c_str() ) ;
        
      }
      else
      {
        // not valid measurements
        log_printf(LOG_ERR, "***********\n* BME provides wrong measurement. Reboot monitor...\n--*\n", tempMessage.c_str() ) ;
        esp_restart_noos() ; 
      }
    }
    else
    {
      log_printf(LOG_INFO, "Try to initialize BME...\n" ) ;
      if (bme.begin())
      {
        bmeInitialized = 1 ;
        log_printf(LOG_INFO, "BME initialized Ok!\n" ) ;
      } 
      else
      {
        log_printf(LOG_ERR, "Could not find a valid BME280 sensor, check wiring!\n" ) ;
      }
    }
  }

}

void handleRoot() 
{
  unsigned long now = millis() ;
  String message ;
  char tempString[8];
  dtostrf(temperature, 1, 2, tempString) ;
  char humString[8];
  dtostrf(humidity, 1, 2, humString) ;
  message += "WATERTANK\n" ;
  message += "Temperature: " ;
  message += tempString ;
  message += " C\n" ;
  message += "Humidity: " ;
  message += humString ;
  message += " %\n" ;
  message += "restart count: " ;
  message += restartCount ;
  message += "\n" ;
  message += "pump_ctl:" ;
  //message += pump_ctl_state ;
  message += "\n" ;
  message += "pump_ctl_time:" ;
  //message += (now-pumpStarted)*1000 ;
  message += "s\n" ;
  
  server.send(200, "text/plain", message );   // Send HTTP status 200 (Ok) and send some text to the browser/client
  
}

void handleNotFound()
{
  server.send(404, "text/plain", "404: Not found"); // Send HTTP status 404 (Not Found) when there's no handler for the URI in the request
}

