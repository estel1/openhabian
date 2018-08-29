#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DHT.h>

// Replace the next variables with your SSID/Password combination
const char* ssid        = "yandex" ;
const char* password    = "yandex" ;

long lastMsg = 0 ;

ESP8266WebServer  httpServer(80) ;

void handleRoot() ;              // function prototypes for HTTP handlers
void handleNotFound() ;

const int DHT_PIN         = 2 ;
DHT dht(DHT_PIN,DHT11) ;
float temperature = 0 ;
float humidity = 0 ;

void setup() 
{

    pinMode(LED_BUILTIN, OUTPUT) ;
    digitalWrite(LED_BUILTIN, LOW ) ;

    Serial.begin(115200) ;

    setup_wifi() ;

    httpServer.on("/", handleRoot) ;               // Call the 'handleRoot' function when a client requests URI "/"
    httpServer.onNotFound(handleNotFound) ;        // When a client requests an unknown URI (i.e. something other than "/"), call function "handleNotFound"
    httpServer.begin() ;                           // Actually start the server
    Serial.println("HTTP server started") ;
    
    Serial.println("TCP server started") ;

    dht.begin() ;
}

void setup_wifi() 
{
    delay(10) ;
    // We start by connecting to a WiFi network
    Serial.println() ;
    Serial.print("Starting softAP...\n") ;
    Serial.println(ssid) ;

    WiFi.softAP(ssid, password) ;

    Serial.print("IP address: ") ;
    Serial.println(WiFi.softAPIP()) ;

}

void loop() 
{  
    
    // Handle HTTP requests
    httpServer.handleClient() ;
    
}

void handleRoot() 
{
    String message ;
    message += "ESP32S EMG platform\n" ;  
    temperature = dht.readTemperature() ;
    humidity = dht.readHumidity() ;
    message += "Temperature:" ;
    message += temperature ;
    message += " C\n" ;
    message += "Humidity:" ;
    message += humidity ;    
    message += " %\n" ;
    httpServer.send(200, "text/plain", message );   // Send HTTP status 200 (Ok) and send some text to the browser/client  
}

void handleNotFound()
{
    httpServer.send(404, "text/plain", "404: Not found"); // Send HTTP status 404 (Not Found) when there's no handler for the URI in the request
}

