#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <ESPmDNS.h>
#include <WebServer.h>   // Include the WebServer library
#include <esp_system.h>

// this code is based on example:
// https://www.instructables.com/id/Distance-Measurement-Using-HC-SR04-Via-NodeMCU/

// other resources:
// http://esp32.net/images/Ai-Thinker/NodeMCU-32S/Ai-Thinker_NodeMCU-32S_DiagramSchematic.png
// http://esp32.net/images/Ai-Thinker/NodeMCU-32S/Ai-Thinker_NodeMCU-32S_DiagramPinout.png

// Replace the next variables with your SSID/Password combination
const char* ssid        = "Keenetic-0079" ;
const char* password    = "yoHwLp6B" ;
const char* mqtt_server = "192.168.1.54" ;
const char* mqtt_client = "WaterTankClient" ;
const int   tank_height = 120 ; // Tank height, cm

const int DHT_PIN         = T1 ;
const int trigPin         = T8 ;
const int echoPin         = T9 ;  

WiFiClient espClient ;
PubSubClient client(espClient) ;
DHT dht(DHT_PIN,DHT11) ;
WebServer server(80) ;

unsigned long lastMsg = 0 ;
char msg[50] ;
int value = 0 ;

float temperature = 0 ;
float humidity = 0 ;

void handleRoot() ;              // function prototypes for HTTP handlers
void handleNotFound() ;

int restartCount = 0 ;
const int wdtTimeout = 15000 ;  //time in ms to trigger the watchdog
hw_timer_t *timer = NULL;

void IRAM_ATTR resetModule() 
{
  ets_printf("reboot\n") ;
  restartCount++ ;
  esp_restart_noos() ;
}

void setup() 
{
  pinMode(LED_BUILTIN, OUTPUT) ;
  pinMode(trigPin,OUTPUT) ;
  pinMode(echoPin,OUTPUT) ;
  digitalWrite(LED_BUILTIN, LOW) ;   // turn the LED off
  
  Serial.begin(115200) ;
  
  timer = timerBegin(0, 80, true) ;                  //timer 0, div 80
  timerAttachInterrupt(timer, &resetModule, true) ;  //attach callback
  timerAlarmWrite(timer, wdtTimeout * 1000, false) ; //set time in us
  timerAlarmEnable(timer) ;                          //enable interrupt
  
  setup_wifi() ;
  client.setServer(mqtt_server, 1883) ;
  client.setCallback(callback) ;

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
}

void setup_wifi() 
{
  delay(10) ;
  // We start by connecting to a WiFi network
  Serial.println() ;
  Serial.print("Connecting to ") ;
  Serial.println(ssid) ;

  WiFi.begin(ssid, password) ;

  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500) ;
    Serial.print(".") ;
  }

  Serial.println("") ;
  Serial.println("WiFi connected") ;
  Serial.println("IP address: ") ;
  Serial.println(WiFi.localIP()) ;
}

void reconnect() 
{
  // Loop until we're reconnected
  while (!client.connected()) 
  {
    digitalWrite(LED_BUILTIN, LOW) ;   // turn the LED off
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(mqtt_client)) 
    {
      digitalWrite(LED_BUILTIN, HIGH) ;   // turn the LED off
      Serial.println("connected") ;
    } 
    else 
    {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
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
    int distance = duration*0.034/2 ;
    // Prints the distance on the Serial Monitor
    Serial.print("Distance is: ") ;
    Serial.print(distance) ;
    Serial.println("cm") ;
    int water_level = 100.0*(tank_height-distance)/tank_height ;
    char levelString[8] ;
    dtostrf(water_level, 1, 2, levelString) ;
    
    temperature = dht.readTemperature() ;
    char tempString[8];
    dtostrf(temperature, 1, 2, tempString);
    Serial.print("Temperature: ");
    Serial.println(tempString);
    client.publish("watertank/temperature", tempString);

    humidity = dht.readHumidity() ;
    
    // Convert the value to a char array
    char humString[8];
    dtostrf(humidity, 1, 2, humString);
    Serial.print("Humidity: ");
    Serial.println(humString);
    client.publish("watertank/humidity", humString);
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

