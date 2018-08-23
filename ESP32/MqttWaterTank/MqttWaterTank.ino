#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <ESPmDNS.h>
#include <WebServer.h>   // Include the WebServer library
#include <esp_system.h>

// https://einstronic.com/wp-content/uploads/2017/06/NodeMCU-32S-Catalogue.pdf
// https://www.instructables.com/id/Distance-Measurement-Using-HC-SR04-Via-NodeMCU/
// http://esp32.net/images/Ai-Thinker/NodeMCU-32S/Ai-Thinker_NodeMCU-32S_DiagramSchematic.png

// Replace the next variables with your SSID/Password combination
const char* ssid        = "Keenetic-0079" ;
const char* password    = "yoHwLp6B" ;
const char* mqtt_server = "192.168.1.54" ;
const char* mqtt_client = "WaterTankClient" ;

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

void setup_wifi() {
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

  Serial.println("") ;
  Serial.println("WiFi connected") ;
  Serial.println("IP address: ") ;
  Serial.println(WiFi.localIP()) ;
}


void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(mqtt_client)) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}
void loop() {
  
  timerWrite(timer, 0) ; //reset timer (feed watchdog)
  
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  server.handleClient() ;                    // Listen for HTTP requests from clients

  int btnOn = digitalRead( BTN_ON_PIN ) ;
  int btnOff = digitalRead( BTN_OFF_PIN ) ;
  if (btnOn==0 && btnOn==0)
  { 
    // both btn pressed - do nothing   
    Serial.println("Disabled button combination") ;
  }
  else if (btnOn==0)
  {
    if ( pump_ctl_state!=1 )
    {
      pump_ctl_state  = 1 ;
      client.publish( "pump", "1" ) ;
      Serial.println("BTN_ON pressed") ;
      pumpStarted = millis() ;
    }  
  }
  else if (btnOff==0)
  {
    if ( pump_ctl_state!=0 )
    {
      pump_ctl_state  = 0 ;
      client.publish( "pump", "0" ) ;
      Serial.println("BTN_OFF pressed") ;
    }  
  }

  unsigned long now = millis() ;
  if (pump_ctl_state==1 && now-pumpStarted>1000*60*10)
  {    
      Serial.println("Switch off pump due to timeout") ;
      pump_ctl_state  = 0 ;
      client.publish( "pump", "0" ) ;
  }

  if (now - lastMsg > 5000) {
    lastMsg = now;

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

void handleRoot() {
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

void handleNotFound(){
  server.send(404, "text/plain", "404: Not found"); // Send HTTP status 404 (Not Found) when there's no handler for the URI in the request
}

