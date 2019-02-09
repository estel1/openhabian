#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <wifiudp.h>
#include <Syslog.h>

// Replace the next variables with your SSID/Password combination
const char* ssid        = "Keenetic-0079" ;
const char* password    = "yoHwLp6B" ;

#define DEVNAME "ESP8266_1" 
const char* devname                     = DEVNAME ;

const char* mqtt_server                 = "192.168.1.54" ;
const char* syslog_server               = "192.168.1.54" ;
int mqtt_port                           = 1883 ;
unsigned long lastMsg                   = 0 ;


WiFiClient espClient ;
PubSubClient client(espClient) ;

const int RELAY_PIN         = 0 ;

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

    pinMode(RELAY_PIN, OUTPUT) ;

    Serial.begin(115200) ;

    setup_wifi() ;
    client.setServer( mqtt_server, mqtt_port ) ;
    client.setCallback( callback ) ;

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

  log_printf( LOG_INFO, "Wifi connected. IP address: %s\n", WiFi.localIP().toString().c_str(  ) ) ;  
}

boolean register_relay_homie_device()
{
  if (!client.publish("homie/"DEVNAME"/$state","init",true))
  {
    log_printf(LOG_ERR, "register_homie_device() failed. - can't publish homie version.\n" ) ;
    return (false) ; 
  }

  client.publish("homie/"DEVNAME"/$homie","3.0",true) ;
  client.publish("homie/"DEVNAME"/$name",devname,true) ;
  client.publish("homie/"DEVNAME"/$nodes","Relay",true) ;
  
  client.publish("homie/"DEVNAME"/Relay/$name","Relay",true) ;
  client.publish("homie/"DEVNAME"/Relay/$properties","power",true) ;

  client.publish("homie/"DEVNAME"/Relay/power","false",true) ;
  client.publish("homie/"DEVNAME"/Relay/power/$name","power",true) ;
  client.publish("homie/"DEVNAME"/Relay/power/$settable","true",true) ;
  client.publish("homie/"DEVNAME"/Relay/power/$retained","true",true) ;
  client.publish("homie/"DEVNAME"/Relay/power/$datatype","boolean",true) ;
  //client.publish("homie/"DEVNAME"/Relay/power/$format","ON,OFF",true) ;
  client.publish("homie/"DEVNAME"/$state","ready",true) ;

  //homie/kitchen-light/light/power/set ← "true"

  return (true) ;
}

void loop() 
{  
  if (!client.connected())
  {
    reconnect() ;
  }
  /*
  unsigned long now = millis() ;
  if (now - lastMsg > 30000) 
  {
    lastMsg = now ;
    //register_relay_homie_device() ;
  }
  */
  client.loop() ;
}

void reconnect() 
{
  // Loop until we're reconnected
  while (!client.connected()) 
  {
    log_printf( LOG_INFO, "Attempting MQTT connection...") ;
    // Attempt to connect
    if (client.connect(devname)) 
    {
      log_printf( LOG_INFO, "connected\n") ;
      // Subscribe
      register_relay_homie_device() ;
      //client.publish(   "homie/"DEVNAME"/Relay/power/set","false" ) ;
      client.subscribe( "homie/"DEVNAME"/Relay/power/set" ) ;
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

void callback( char* topic, byte* message, unsigned int msg_len ) 
{
  log_printf( LOG_INFO, "Message arrived on topic %s, length:%d\n", topic, msg_len ) ;  
  if (msg_len>0)
  {
      if (message[0]=='t')
      {
        client.publish(   "homie/"DEVNAME"/Relay/power","true" ) ;
        log_printf( LOG_INFO, "homie/"DEVNAME"/Relay/power ← true" ) ;  
        digitalWrite(RELAY_PIN, LOW) ;   
      }
      else if (message[0]=='f')
      {
        client.publish(   "homie/"DEVNAME"/Relay/power","false" ) ;
        log_printf( LOG_INFO, "homie/"DEVNAME"/Relay/power ← false" ) ;  
        digitalWrite(RELAY_PIN, HIGH) ; 
      }
      else
      {
      }
      
  }
}


