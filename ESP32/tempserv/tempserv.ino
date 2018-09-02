#include <WiFi.h>
#include <WebServer.h>   // Include the WebServer library
#include <DHT.h>

// https://github.com/espressif/arduino-esp32/blob/master/docs/arduino-ide/windows.md
// http://esp32.net/images/Ai-Thinker/NodeMCU-32S/Ai-Thinker_NodeMCU-32S_DiagramSchematic.png
// http://esp32.net/images/Ai-Thinker/NodeMCU-32S/Ai-Thinker_NodeMCU-32S_DiagramPinout.png
// https://einstronic.com/wp-content/uploads/2017/06/NodeMCU-32S-Catalogue.pdf
// Add interrupt: https://techtutorialsx.com/2017/10/07/esp32-arduino-timer-interrupts/


// Replace the next variables with your SSID/Password combination
const char* ssid        = "emg" ;
const char* password    = "emg" ;

long lastMsg = 0 ;

WebServer httpServer(80) ;
void handleRoot() ;              // function prototypes for HTTP handlers
void handleNotFound() ;

const int DHT_PIN         = T1 ;
DHT dht(DHT_PIN,DHT11) ;

float temperature = 0 ;
float humidity = 0 ;

void setup() 
{
    pinMode(LED_BUILTIN, OUTPUT) ;
    digitalWrite(LED_BUILTIN, LOW) ;   // turn the LED on (HIGH is the voltage level)

    Serial.begin(115200) ;

    dht.begin() ;

    setup_wifi() ;

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
    Serial.print("Starting softAP...\n") ;
    Serial.println(ssid) ;

    WiFi.softAP(ssid, password) ;

    Serial.print("IP address: ") ;
    Serial.println(WiFi.softAPIP()) ;

}

void loop() 
{  
     unsigned long now = millis() ;

    if (now - lastMsg > 5000) 
    {
      lastMsg = now ;
      temperature = dht.readTemperature() ;
      humidity = dht.readHumidity() ;
    }
    // Handle HTTP requests
    httpServer.handleClient() ;    
}

void handleRoot() 
{
    String message ;
    message += "Temperature: " ;
    message += temperature ;
    message += " C\nHumidity: " ;
    message += humidity ;
    message += " %\n" ;  
    httpServer.send(200, "text/plain", message );   // Send HTTP status 200 (Ok) and send some text to the browser/client  
}

void handleNotFound()
{
    httpServer.send(404, "text/plain", "404: Not found"); // Send HTTP status 404 (Not Found) when there's no handler for the URI in the request
}
