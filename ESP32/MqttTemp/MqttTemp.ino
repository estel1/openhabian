#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <ESPmDNS.h>
#include <WebServer.h>   // Include the WebServer library
#include <esp_system.h>

// https://einstronic.com/wp-content/uploads/2017/06/NodeMCU-32S-Catalogue.pdf

// Replace the next variables with your SSID/Password combination
const char* ssid        = "Keenetic-0079" ;
const char* password    = "yoHwLp6B" ;
const char* mqtt_server = "192.168.1.54" ;

WiFiClient espClient ;
PubSubClient client(espClient) ;
DHT dht(T1,DHT11) ;
WebServer server(80) ;

long lastMsg = 0 ;
char msg[50] ;
int value = 0 ;

float temperature = 0 ;
float humidity = 0 ;


void handleRoot();              // function prototypes for HTTP handlers
void handleNotFound();

const int wdtTimeout = 15000;  //time in ms to trigger the watchdog
hw_timer_t *timer = NULL;

void IRAM_ATTR resetModule() {
  ets_printf("reboot\n") ;
  esp_restart_noos() ;
}

void setup() {

  pinMode(LED_BUILTIN, OUTPUT) ;
  pinMode(T0, OUTPUT) ;
  digitalWrite(LED_BUILTIN, HIGH) ;   // turn the LED on (HIGH is the voltage level)

  
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
  
  digitalWrite(LED_BUILTIN, LOW) ;   // turn the LED on (HIGH is the voltage level)

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

void callback(char* topic, byte* message, unsigned int length) {
  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(". Message: ");
  String messageTemp;

  if (length>0)
  {
    if (message[0]=='0')
    {
      digitalWrite(LED_BUILTIN, HIGH) ;   // turn the LED on (HIGH is the voltage level)
      digitalWrite(T0, HIGH) ;   // turn the LED on (HIGH is the voltage level)
    }
    else
    {
      digitalWrite(LED_BUILTIN, LOW) ;   // turn the LED on (HIGH is the voltage level)
      digitalWrite(T0, LOW) ;   // turn the LED on (HIGH is the voltage level)
    }
  }
  
  for (int i = 0; i < length; i++) {
    Serial.print((char)message[i]);
    messageTemp += (char)message[i];
  }
  Serial.println();

  // Feel free to add more if statements to control more GPIOs with MQTT

  // If a message is received on the topic esp32/output, you check if the message is either "on" or "off". 
  // Changes the output state according to the message
  if (String(topic) == "pump") {
    Serial.print("Changing output to ");
    if(messageTemp == "on"){
      Serial.println("on");
      digitalWrite(LED_BUILTIN, HIGH) ;   // turn the LED on (HIGH is the voltage level)
      digitalWrite(T0, HIGH) ;   // turn the LED on (HIGH is the voltage level)
    }
    else if(messageTemp == "off"){
      Serial.println("off");
      digitalWrite(LED_BUILTIN, LOW) ;   // turn the LED on (HIGH is the voltage level)
      digitalWrite(T0, LOW) ;   // turn the LED on (HIGH is the voltage level)
    }
  }
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("ESP8266Client")) {
      Serial.println("connected");
      // Subscribe
      client.subscribe("pump");
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

  long now = millis();
  if (now - lastMsg > 5000) {
    lastMsg = now;

    temperature = dht.readTemperature() ;
    //Serial.print("Temperature: ") ;
    //Serial.println(temperature) ;
    
    // Temperature in Celsius
    //temperature = bme.readTemperature();   
    // Uncomment the next line to set temperature in Fahrenheit 
    // (and comment the previous temperature line)
    //temperature = 1.8 * bme.readTemperature() + 32; // Temperature in Fahrenheit
    
    // Convert the value to a char array
    char tempString[8];
    dtostrf(temperature, 1, 2, tempString);
    Serial.print("Temperature: ");
    Serial.println(tempString);
    client.publish("esp32/temperature", tempString);

    humidity = dht.readHumidity() ;
    
    // Convert the value to a char array
    char humString[8];
    dtostrf(humidity, 1, 2, humString);
    Serial.print("Humidity: ");
    Serial.println(humString);
    client.publish("esp32/humidity", humString);
  }
}

void handleRoot() {
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
  
  server.send(200, "text/plain", message );   // Send HTTP status 200 (Ok) and send some text to the browser/client
  
}

void handleNotFound(){
  server.send(404, "text/plain", "404: Not found"); // Send HTTP status 404 (Not Found) when there's no handler for the URI in the request
}

