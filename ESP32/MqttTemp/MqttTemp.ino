#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <ESPmDNS.h>
#include <WebServer.h>   // Include the WebServer library
#include <esp_system.h>
#include <Syslog.h>

// https://einstronic.com/wp-content/uploads/2017/06/NodeMCU-32S-Catalogue.pdf

// Replace the next variables with your SSID/Password combination
const char* ssid        = "Keenetic-0079" ;
const char* password    = "yoHwLp6B" ;
const char* mqtt_server = "192.168.1.54" ;
const char* mqtt_client = "PumpClient" ;

const char* PUMPSTATION_DHT11_TEMPERATURE  = "esp32/temperature" ;
const char* PUMPSTATION_DHT11_HUMIDITY     = "esp32/humidity" ;

const int RELAY_PUMP_PIN  = T0 ;
const int DHT_PIN         = T1 ;
const int BTN_ON_PIN      = T8 ;
const int BTN_OFF_PIN     = T9 ;
 

WiFiClient espClient ;
PubSubClient client(espClient) ;
DHT dht(DHT_PIN,DHT11) ;
WebServer server(80) ;

unsigned long lastMsg = 0 ;
unsigned long pumpStarted = 0 ;
char msg[50] ;
int value = 0 ;

int pump_ctl_state = -1 ;
float temperature = 0 ;
float humidity = 0 ;

void handleRoot();              // function prototypes for HTTP handlers
void handleNotFound();

int restartCount = 0 ;
const int wdtTimeout = 15000;  //time in ms to trigger the watchdog
hw_timer_t *timer = NULL;

void IRAM_ATTR resetModule() 
{
  ets_printf("reboot\n") ;
  restartCount++ ;
  esp_restart_noos() ;
}

WiFiUDP udpClient ;
Syslog syslog(udpClient, syslog_server, 514, "pumpstation", "station", LOG_KERN) ;

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
  pinMode(RELAY_PUMP_PIN, OUTPUT) ;
  pinMode(BTN_ON_PIN,INPUT_PULLUP) ;
  pinMode(BTN_OFF_PIN,INPUT_PULLUP) ;
  digitalWrite(LED_BUILTIN, LOW) ;   // turn the LED on (HIGH is the voltage level)

  // check this
  digitalWrite(RELAY_PUMP_PIN, HIGH) ;   // turn the pump off

  
  Serial.begin(115200) ;
  Serial.print("LED_BUILTIN=" ) ;
  Serial.println(LED_BUILTIN) ;
  
  timer = timerBegin(0, 80, true);                  //timer 0, div 80
  timerAttachInterrupt(timer, &resetModule, true);  //attach callback
  timerAlarmWrite(timer, wdtTimeout * 1000, false); //set time in us
  timerAlarmEnable(timer);                          //enable interrupt
  
  setup_wifi() ;
  client.setServer(mqtt_server, 1883) ;
  client.setCallback(callback) ;

  if (MDNS.begin("esp32s")) 
  {
    Serial.println("mDNS responder started") ;
  } 
  else 
  {
    Serial.println("Error setting up MDNS responder!") ;
  }  
  
  server.on("/", handleRoot);               // Call the 'handleRoot' function when a client requests URI "/"
  server.onNotFound(handleNotFound);        // When a client requests an unknown URI (i.e. something other than "/"), call function "handleNotFound"
  server.begin() ;                           // Actually start the server
  Serial.println("HTTP server started") ;
  
  dht.begin() ;
  

}

void setup_wifi() 
{
  delay(10) ;
  // We start by connecting to a WiFi network
  Serial.println() ;
  Serial.print("Connecting to ") ;
  Serial.println(ssid) ;

  WiFi.begin(ssid, password) ;

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".") ;
  }

  log_printf( LOG_INFO, "Wifi connected. IP address: %s\n", WiFi.localIP().toString().c_str(  ) ) ;  
}

void callback(char* topic, byte* message, unsigned int length) 
{
  log_printf( LOG_INFO, "Message arrived on topic: %s. ", topic ) ;  
  
  if (length>0)
  {
    if (message[0]=='0')
    {
      digitalWrite(RELAY_PUMP_PIN, HIGH) ;   // turn the LED on (HIGH is the voltage level)
      log_printf( LOG_INFO, "Off.\n" ) ;  
    }
    else
    {
      digitalWrite(RELAY_PUMP_PIN, LOW) ;   // turn the LED on (HIGH is the voltage level)
      log_printf( LOG_INFO, "On.\n" ) ;  
    }
  }
}

void reconnect() 
{
  // Loop until we're reconnected
  while (!client.connected()) 
  {
    digitalWrite(LED_BUILTIN, LOW) ;   // turn the LED on (HIGH is the voltage level)
    Serial.print("Attempting MQTT connection...") ;
    // Attempt to connect
    if (client.connect(mqtt_client)) {
      Serial.println("connected") ;
      // Subscribe
      client.subscribe("pump") ;
      digitalWrite(LED_BUILTIN, HIGH) ;   // turn the LED on (HIGH is the voltage level)
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds") ;
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void loop() 
{
  
  timerWrite(timer, 0) ; //reset timer (feed watchdog)
  
  if (!client.connected()) 
  {
    reconnect() ;
  }

  server.handleClient() ;                    // Listen for HTTP requests from clients

  int btnOn = digitalRead( BTN_ON_PIN ) ;
  int btnOff = digitalRead( BTN_OFF_PIN ) ;
  if (btnOn==LOW && btnOff==HIGH)
  { 
    // both btn pressed - do nothing   
    log_printf( LOG_INFO, "Disabled button combination.\n" ) ;  
  }
  else if (btnOn==LOW)
  {
    if ( pump_ctl_state!=1 )
    {
      pump_ctl_state  = 1 ;
      client.publish( "pump", "1" ) ;
      log_printf( LOG_INFO, "BTN_ON pressed\n." ) ;  
      pumpStarted = millis() ;
    }  
  }
  else if (btnOff==HIGH)
  {
    if ( pump_ctl_state!=0 )
    {
      pump_ctl_state  = 0 ;
      client.publish( "pump", "0" ) ;
      log_printf( LOG_INFO, "BTN_OFF pressed\n." ) ;  
    }  
  }

  client.loop() ;
  
  unsigned long now = millis() ;
  /*
  if (pump_ctl_state==1 && now-pumpStarted>1000*60*10)
  {    
      Serial.println("Switch off pump due to timeout") ;
      pump_ctl_state  = 0 ;
      client.publish( "pump", "0" ) ;
  }
  */

  if (now - lastMsg > 5000) 
  {
    lastMsg = now ;

    temperature = dht.readTemperature() ;
    humidity = dht.readHumidity() ;
    
    // Convert the value to a char array
    String tempMessage( temperature, 2 ) ;
    log_printf(LOG_INFO, "DHT11 Temperature is: %s C\n", tempMessage.c_str() ) ;
    client.publish(PUMPSTATION_DHT11_TEMPERATURE, tempMessage.c_str() ) ;   
   
    String humidityMessage( humidity, 2 ) ;
    log_printf(LOG_INFO, "DHT11 Humidity is: %s %%\n", humidityMessage.c_str() ) ;    
    client.publish(PUMPSTATION_DHT11_HUMIDITY, humidityMessage.c_str() ) ;   
  }
}

void handleRoot() {
  unsigned long now = millis() ;
  String message ;
  char tempString[8];
  dtostrf(temperature, 1, 2, tempString) ;
  char humString[8];
  dtostrf(humidity, 1, 2, humString) ;
  message += "ESP32S\n" ;
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
  message += pump_ctl_state ;
  message += "\n" ;
  message += "pump_ctl_time:" ;
  message += (now-pumpStarted)*1000 ;
  message += "s\n" ;
  
  server.send(200, "text/plain", message );   // Send HTTP status 200 (Ok) and send some text to the browser/client
  
}

void handleNotFound()
{
  server.send(404, "text/plain", "404: Not found"); // Send HTTP status 404 (Not Found) when there's no handler for the URI in the request
}

