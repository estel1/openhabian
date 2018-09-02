#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <PubSubClient.h>

// Replace the next variables with your SSID/Password combination
const char* ssid        = "Keenetic-0079" ;
const char* password    = "yoHwLp6B" ;
const char* mqtt_server = "192.168.1.54";
const char* mqtt_topic  = "ext_light" ;
int mqtt_port           = 1883 ;
const char* mqtt_client = "OuterLightClient" ;

String httpPage ;

ESP8266WebServer  httpServer(80) ;
WiFiClient espClient ;
PubSubClient client(espClient) ;

void handleRoot() ;              // function prototypes for HTTP handlers
void handleNotFound() ;

const int RELAY_PIN         = 0 ;

void setup() 
{

    pinMode(RELAY_PIN, OUTPUT) ;

    Serial.begin(115200) ;

    setup_wifi() ;
    client.setServer(mqtt_server, mqtt_port ) ;
    client.setCallback( callback ) ;

    httpServer.on("/", handleRoot) ;               // Call the 'handleRoot' function when a client requests URI "/"
    httpServer.onNotFound(handleNotFound) ;        // When a client requests an unknown URI (i.e. something other than "/"), call function "handleNotFound"
    httpServer.begin() ;                           // Actually start the server
    Serial.println("HTTP server started") ;
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

void loop() 
{  
  if (!client.connected()) 
  {
    reconnect() ;
  }
  client.loop() ;
  // Handle HTTP requests
  httpServer.handleClient() ;
}

void reconnect() 
{
  // Loop until we're reconnected
  while (!client.connected()) 
  {
    Serial.print("Attempting MQTT connection...") ;
    // Attempt to connect
    if (client.connect(mqtt_client)) 
    {
      Serial.println("connected") ;
      // Subscribe
      client.subscribe(mqtt_topic) ;
    } 
    else 
    {
      Serial.print("failed, rc=") ;
      Serial.print(client.state()) ;
      Serial.println(" try again in 5 seconds") ;
      // Wait 5 seconds before retrying
      delay(5000) ;
    }
  }
}

void callback( char* topic, byte* message, unsigned int length ) 
{
  String Topic( topic ) ;
  Serial.print("Message arrived on topic: ") ;
  Serial.print(topic) ;
  httpPage = "ESP01 Mqtt_Relay\n" ;
  if (length>0)
  {
    //if (Topic.compareTo(mqtt_topic)==0)
    //{
      if (message[0]=='0')
      {
        digitalWrite(RELAY_PIN, HIGH) ;   // turn the LED on (HIGH is the voltage level)
        Serial.println(". Message: OFF") ;
        httpPage += "Light Off\n" ;
      }
      else
      {
        digitalWrite(RELAY_PIN, LOW) ;   // turn the LED on (HIGH is the voltage level)
        Serial.println(". Message: ON") ;
        httpPage += "Light On\n" ;
      }
    //}
  }
}

void handleRoot() 
{
    httpServer.send(200, "text/plain", httpPage ) ;   // Send HTTP status 200 (Ok) and send some text to the browser/client  
}

void handleNotFound()
{
    httpServer.send(404, "text/plain", "404: Not found") ; // Send HTTP status 404 (Not Found) when there's no handler for the URI in the request
}

