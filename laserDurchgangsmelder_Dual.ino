//VL53LXX-V2  lasersensor AZDelivery
// https://www.az-delivery.de/products/vl53l0x-time-of-flight-tof-laser-abstandssensor
// https://github.com/adafruit/Adafruit_VL53L0X/blob/master/src/vl53l0x_api.h

/*

anbauen, normal betrieb calibrieren 
je sensor
nach oben und unten im Leerlauf zone erkennen
bei Bewegung Durchgang melden
Bewegung: 
- sichere Erkennung je sensor
- plausible Folgebewegung am anderen sensor in kurzem Zeitfenser
- Richtung ausgeben 1 -> 2 oder umgekehrt
Bewegungsrichtung je sensor als Zusatz
Regenerkennung (Schnee, Hagel) ggf. deaktivieren der sinnlosen Funktion

1. mqtt und Auswertung in iobroker
2. lokale Umsetzung auf esp, ggf. 485 Sender wegen Entfernung

*/


// todo OTA

#ifdef ESP8266
  #include "ESP8266WiFi.h"
#else 
  #include "WiFi.h"
#endif
#include <MQTT.h>     //joel Gaehwiler
#include "Adafruit_VL53L0X.h"



#define LED LED_BUILTIN
// address we will assign if dual sensor is present
#define LOX1_ADDRESS 0x30
#define LOX2_ADDRESS 0x31

// set the pins to shutdown
#define SHT_LOX1 D6//7
#define SHT_LOX2 D5 //6

const char* hostname = "esp_tof_melder";
const  char* ssid = "rheinsberg.mesh";
const  char* password = "1234567899";
long lastwifibegin = 0 - 30000;

IPAddress local_IP(192, 168, 10, 43);
IPAddress gateway(192, 168, 10, 1);
IPAddress subnet(255, 255, 255, 0);

const char* mqtt_path_connection;
const char* mqtt_path_m1;
const char* mqtt_path_m2;
const char* mqtt_user;
const char* mqtt_server = "192.168.10.51";
const int mport = 1883;
WiFiClient client;
MQTTClient mclient;
boolean bInitmqtt = true;

// objects for the vl53l0x
Adafruit_VL53L0X lox1 = Adafruit_VL53L0X();
Adafruit_VL53L0X lox2 = Adafruit_VL53L0X();

// this holds the measurement
VL53L0X_RangingMeasurementData_t measure1;
VL53L0X_RangingMeasurementData_t measure2;


void initMQTT(){
  Serial.print("initMQTT\n");  
  mqtt_path_m1 = "tof/1";    
  mqtt_path_m2 = "tof/2";  
  mqtt_path_connection = "tof";
  mclient.begin(mqtt_server, mport, client);  
  //mclient.onMessage(messageReceived);
  mclient.setWill(mqtt_path_connection, "disconnected", true, 0); 
  if (!mclient.connected()) {
     Serial.print("initMQTT not connected!\n");  
  } else{
     Serial.print("initMQTT connected.\n");  
  }
}



const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 0;//3600;
unsigned long previousMillisConnectMsg = 0;
const long intervalConnectMsg = 1000 * 60 * 10;  // interval at which to send a connected-message

void publishConnectionTime(){
  #define MY_TZ "GMT-1GMT-2,M3.5.0,M10.5.0/3" //"GMT+1GMT,M3.5.0,M10.5.0/03:00:00"  
  //"CET-1CEST,M3.5.0,M10.5.0/3"
  configTime(0, 0, ntpServer);
  setenv("TZ", MY_TZ, 1);            // Set environment variable with your time zone
  tzset();
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
   mclient.publish(mqtt_path_connection, "Error NTP",true,2);
    return;
  }
  char IP[] = "000.000.000.000";
  WiFi.localIP().toString().toCharArray(IP, 16);
  char buffer[50];
  char tag[][3] = {"So","Mo", "Di", "Mi", "Do", "Fr","Sa"};
  //sprintf(buffer, "connected %s, %02d.%02d.%02d %02d:%02d:%02d, IP:%s",tag[timeinfo.tm_wday],timeinfo.tm_mday,timeinfo.tm_mon+1,timeinfo.tm_year+1900-2000,timeinfo.tm_hour,timeinfo.tm_min,timeinfo.tm_sec,IP);    
  sprintf(buffer, "connected %s, %02d:%02d, IP:%s",tag[timeinfo.tm_wday],timeinfo.tm_hour,timeinfo.tm_min,IP);
  //Serial.println(buffer);
  mclient.publish(mqtt_path_connection, buffer,true,2);
}

void connect() {
  Serial.print("checking WiFi\n");  
  while (WiFi.status() != WL_CONNECTED) {    
    if (millis() - lastwifibegin > 15000) {  // alle 15 sec
      WiFi.persistent(false); 
      WiFi.disconnect();
      delay(100);
      WiFi.mode(WIFI_OFF);
      delay(100);
      WiFi.mode(WIFI_STA);
      lastwifibegin = millis();
      Serial.print("\nretry WiFi begin...");
      Serial.println(millis()-lastwifibegin);
 //local_IP
  //gateway
  //subnet
  //INADDR_NONE

      WiFi.config(local_IP, gateway, subnet, gateway);
      // WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
      WiFi.setHostname(hostname);
      WiFi.begin(ssid, password);
      while (WiFi.status() != WL_CONNECTED) {  
        Serial.print(".");
        delay(1000); //warten auf Verbindung  
      }
      delay(1000);
      Serial.println("\n-------------------------");      
      Serial.print("hostname: ");
      Serial.println(hostname);
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      Serial.print("ESP Mac Address: ");
      Serial.println(WiFi.macAddress());
      Serial.print("Subnet Mask: ");
      Serial.println(WiFi.subnetMask());
      Serial.print("Gateway IP: ");
      Serial.println(WiFi.gatewayIP());
      Serial.print("DNS: ");
      Serial.println(WiFi.dnsIP());  
      Serial.println("-------------------------");      
      Serial.print("\nWiFi connected!\n");      
  
      ESP.wdtFeed();

      for (int i=0; i<7; i++){
        digitalWrite(LED_BUILTIN, 1); // Status   
        delay(100);
        digitalWrite(LED_BUILTIN, 0); // Status   
        delay(100);
      }
      break;      
    }
  }
    if (WiFi.status() == WL_CONNECTED){ 
      if(bInitmqtt){
        bInitmqtt = false;
        initMQTT(); 
      }
       
    bool erfolg = false;
    for (int i=0; i<30; i++){
      Serial.println("connecting to MQTT-Server...");
      digitalWrite(LED_BUILTIN, 1); // Status   
      if(mclient.connect(mqtt_user, "", "")){
        Serial.println("MQTT connected.\n");
        //mclient.publish(mqtt_path_connection, "reconnect");
        mclient.subscribe(mqtt_path_m1);
        mclient.subscribe(mqtt_path_m2);
        mclient.publish(mqtt_path_m1, "0");
        mclient.publish(mqtt_path_m2, "0");
        publishConnectionTime();
        erfolg = true;
        break;     
      }
      Serial.print("not connecting to MQTT-Server");
      digitalWrite(LED_BUILTIN, 0); // Status   
      delay(1000);        
    }
    if (!erfolg){
      ESP.restart();
    }
    Serial.println();
  }

}


/*
    Reset all sensors by setting all of their XSHUT pins low for delay(10), then set all XSHUT high to bring out of reset
    Keep sensor #1 awake by keeping XSHUT pin high
    Put all other sensors into shutdown by pulling XSHUT pins low
    Initialize sensor #1 with lox.begin(new_i2c_address) Pick any number but 0x29 and it must be under 0x7F. Going with 0x30 to 0x3F is probably OK.
    Keep sensor #1 awake, and now bring sensor #2 out of reset by setting its XSHUT pin high.
    Initialize sensor #2 with lox.begin(new_i2c_address) Pick any number but 0x29 and whatever you set the first sensor to
 */
void setID() {
  // all reset
  digitalWrite(SHT_LOX1, LOW);    
  digitalWrite(SHT_LOX2, LOW);
  delay(10);
  // all unreset
  digitalWrite(SHT_LOX1, HIGH);
  digitalWrite(SHT_LOX2, HIGH);
  delay(10);

  // activating LOX1 and resetting LOX2
  digitalWrite(SHT_LOX1, HIGH);
  digitalWrite(SHT_LOX2, LOW);

  // initing LOX1
  if(!lox1.begin(LOX1_ADDRESS)) {
    Serial.println(F("Failed to boot first VL53L0X"));
    while(1);
  }
  delay(10);

  // activating LOX2
  digitalWrite(SHT_LOX2, HIGH);
  delay(10);

  //initing LOX2
  if(!lox2.begin(LOX2_ADDRESS)) {
    Serial.println(F("Failed to boot second VL53L0X"));
    while(1);
  }

lox1.configSensor(Adafruit_VL53L0X::VL53L0X_SENSE_LONG_RANGE);
lox2.configSensor(Adafruit_VL53L0X::VL53L0X_SENSE_LONG_RANGE);

}

void read_dual_sensors() {
  
  lox1.rangingTest(&measure1, false); // pass in 'true' to get debug data printout!
  lox2.rangingTest(&measure2, false); // pass in 'true' to get debug data printout!

  // print sensor one reading
  Serial.print(F("1: "));
  if(measure1.RangeStatus != 4) {     // if not out of range
    Serial.print(measure1.RangeMilliMeter);
    mclient.publish(mqtt_path_m1, String(measure1.RangeMilliMeter) );
  } else {
    Serial.print(F("----"));
    mclient.publish(mqtt_path_m1,String(-1));
  }
  
  Serial.print(F(" "));

  // print sensor two reading
  Serial.print(F("2: "));
  if(measure2.RangeStatus != 4) {
    Serial.print(measure2.RangeMilliMeter);
    mclient.publish(mqtt_path_m2,String(measure2.RangeMilliMeter));
  } else {
    Serial.print(F("----"));
    mclient.publish(mqtt_path_m2,String(-1));
  }
  
  
  // Geschwindigkeit ermitteln und drucken. 
  // daraus positiv negativ richtung bestimmen, plausi, reaktion ableiten und sperren für einige Sekunden
  // aus beiden (einer zeigt nach oben, einer nach unten) reaktion ala vorhandenem
  // erkennen, wenn ein sensor nicht mehr arbeitet, nur noch identische werte liefert, melden und reset auslösen
  // mqtt status, signal, ansagen steuern

  Serial.println();
}

void setup() {
  Serial.begin(115200);

  // wait until serial port opens for native USB devices
  while (! Serial) { delay(1); }

  pinMode(SHT_LOX1, OUTPUT);
  pinMode(SHT_LOX2, OUTPUT);

  Serial.println(F("Shutdown pins inited..."));

  digitalWrite(SHT_LOX1, LOW);
  digitalWrite(SHT_LOX2, LOW);

  Serial.println(F("Both in reset mode...(pins are low)"));
   
  ESP.wdtDisable();
  ESP.wdtEnable(1000);
  //ESP.wdtFeed();

  Serial.println(F("Starting..."));
  setID();

  connect();
 
}

void loop() {  
  if (!mclient.connected()) {
    connect();
  }  
  unsigned long currentMillis = millis();
  // connected-Message regelmässig
  if (currentMillis - previousMillisConnectMsg >= intervalConnectMsg) {    
    publishConnectionTime();
    previousMillisConnectMsg = currentMillis;
  }
  read_dual_sensors();  
  delay(200);
}