#include <Wire.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <WiFiUdp.h>    
#include <ArduinoOTA.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <FS.h>

//#define DEBUG

#ifdef DEBUG
 #define DEBUG_PRINT(x)  Serial.print(x)
 #define DEBUG_PRINTLN(x) Serial.println(x)
 #define DEBUG_BEGIN Serial.begin(115200)
#else
 #define DEBUG_PRINT(x)
 #define DEBUG_PRINTLN(x)
 #define DEBUG_BEGIN
#endif

#define ONE_HOUR 3600000UL
#define bufferLength 100
MAX30105 max30102Sensor;
//Setup to sense a nice looking saw tooth on the plotter
  byte ledBrightness = 0x1F; //Options: 0=Off to 255=50mA
  byte sampleAverage = 8; //Options: 1, 2, 4, 8, 16, 32
  byte ledMode = 3; //Options: 1 = Red only, 2 = Red + IR, 3 = Red + IR + Green
  int sampleRate = 100; //Options: 50, 100, 200, 400, 800, 1000, 1600, 3200
  int pulseWidth = 411; //Options: 69, 118, 215, 411
  int adcRange = 4096; //Options: 2048, 4096, 8192, 16384

/*_____________________________________________WiFi, HTTP, OTA, mDNS, NTP(by UDP___________________________________________________*/
ESP8266WiFiMulti wifiMulti;      // Create an instance of the ESP8266WiFiMulti class, called 'wifiMulti'
ESP8266WebServer server(80);             // create a web server on port 80
const char *OTAName = "ESP_monitor";         // A name and a password for the OTA service
const char* mdnsName = "esp_monitor";        // Domain name for the mDNS responder
// config for time udp service
WiFiUDP UDP;                     // Create an instance of the WiFiUDP class to send and receive

IPAddress timeServerIP;          // time.nist.gov NTP server address
const char* NTPServerName = "time.nist.gov";
const int NTP_PACKET_SIZE = 48;  // NTP time stamp is in the first 48 bytes of the message
byte NTPBuffer[NTP_PACKET_SIZE]; // buffer to hold incoming and outgoing packets

unsigned long intervalNTP = ONE_HOUR; // Request NTP time every hour
unsigned long prevNTP = 0;
uint32_t timeUNIX = 0;
unsigned long lastNTPResponse = 0;
const unsigned long intervalRead = 60000;
unsigned long prevRead = 0;
long baseValue = 0;
File sensorlog;                      // a File variable to temporarily store the file

//after startWiFi(), setup NTP 
void init_ntp(){
  UDP.begin(123);                          // Start listening for UDP messages on port 123
  if(!WiFi.hostByName(NTPServerName, timeServerIP)) { // Get the IP address of the NTP server
     DEBUG_PRINTLN("NTP FAILED, ESP8266 RESTART...");
     ESP.reset();
  } else {
    DEBUG_PRINTLN("NTP initialization working well...");
  }
}
/*__________________________________________________________SETUP__________________________________________________________*/

void setup() {
  DEBUG_BEGIN;
  // Initialize sensor
  DEBUG_PRINTLN("Setting pins 0,2 as I2C bus pins...");
  //Wire.begin(SDA_PIN, SCL_PIN, I2C_MASTER);        // join i2c bus (address optional for master)
  Wire.begin(3, 1);  //scl go rx, sda go tx
  if (!max30102Sensor.begin(Wire, I2C_SPEED_FAST)) //Use default I2C port, 400kHz speed
  {
    DEBUG_PRINTLN("WAITING FOR MAX30102 RESPONSE...");
    while (1);
  }
  startWiFi();                 // Start a Wi-Fi access point, and try to connect to some given access points. Then wait for either an AP or STA connection
  startOTA();                  // Start the OTA service
  DEBUG_PRINTLN("STARTING LittleFS...");
  startFS();  
  DEBUG_PRINTLN("START MDNS...");             
  startMDNS();               // Start the mDNS responder
  startServer();               // Start a HTTP server with a file read handler and an upload handler
  init_ntp();
  DEBUG_PRINTLN("Sending for first NTP request...");   
  sendNTPpacket(timeServerIP);
  delay(500);
 max30102Sensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange); 
 avg();
}
/*__________________________________________________________LOOP__________________________________________________________*/
void loop(){
  unsigned long currentMillis = millis();
   if (currentMillis - prevNTP > intervalNTP and WiFi.status() == WL_CONNECTED) { // If 1 hour has passed since last NTP request
      prevNTP = currentMillis;
      sendNTPpacket(timeServerIP);               // Send an NTP request
    }
    uint32_t time = getTime();                   // Check if an NTP response has arrived and get the (UNIX) time
    if (time) {                                  // If a new timestamp has been received
      timeUNIX = time;
      DEBUG_PRINTLN("Received a new ntp time....");
      lastNTPResponse = currentMillis;
    } else if ((currentMillis - lastNTPResponse) > 24UL * ONE_HOUR) {   //More than 24 hour since last NTP response. Rebooting
      ESP.reset();
    }
    if (timeUNIX != 0) {
 
        prevRead = currentMillis;
        uint32_t actualTime = timeUNIX + (currentMillis - lastNTPResponse) / 1000;
           DEBUG_PRINTLN("WRITING TO SPIFFS....");
          File sensorLog = SPIFFS.open("/data.csv", "a"); // Write the time and the temperature to the csv file
          sensorLog.print(actualTime);
          sensorLog.print(',');
          sensorLog.print(max30102Sensor.getIR() - baseValue);  
          sensorLog.print(',');
          sensorLog.println(" ");
          sensorLog.close();
        } else {
        if (WiFi.status() == WL_CONNECTED){
         sendNTPpacket(timeServerIP);
         delay(500);
        }
      }
   if (WiFi.status() == WL_CONNECTED){
      server.handleClient();                      // run the server
      ArduinoOTA.handle();                        // listen for OTA events
    } else {
      delay(1000);
    }
  }
/*__________________________________________________________functions_________________________________________________________*/

void sendNTPpacket(IPAddress& address) {
  memset(NTPBuffer, 0, NTP_PACKET_SIZE);  // set all bytes in the buffer to 0
  // Initialize values needed to form NTP request
  NTPBuffer[0] = 0b11100011;   // LI, Version, Mode

  // send a packet requesting a timestamp:
  UDP.beginPacket(address, 123); // NTP requests are to port 123
  UDP.write(NTPBuffer, NTP_PACKET_SIZE);
  UDP.endPacket();
}

uint32_t getTime() {
  if (UDP.parsePacket() == 0) { // If there's no response (yet)
    return 0;
  }
  UDP.read(NTPBuffer, NTP_PACKET_SIZE); // read the packet into the buffer
  // Combine the 4 timestamp bytes into one 32-bit number
  uint32_t NTPTime = (NTPBuffer[40] << 24) | (NTPBuffer[41] << 16) | (NTPBuffer[42] << 8) | NTPBuffer[43];
  // Convert NTP time to a UNIX timestamp:
  // Unix time starts on Jan 1 1970. That's 2208988800 seconds in NTP time:
  const uint32_t seventyYears = 2208988800UL;
  // subtract seventy years:
  uint32_t UNIXTime = NTPTime - seventyYears;
  return UNIXTime;
}

void startWiFi() { // Try to connect to some given access points. Then wait for a connection
  wifiMulti.addAP("xxx", "xxx");   // add Wi-Fi networks you want to connect to
  wifiMulti.addAP("xxx", "xxx");
 // wifiMulti.addAP("ssid_from_AP_3", "your_password_for_AP_3");
  while (wifiMulti.run() != WL_CONNECTED) {  // Wait for the Wi-Fi to connect
    DEBUG_PRINTLN("WATING FOFR WIFI CONNECTION....");
    delay(250);
  }
    DEBUG_PRINTLN("WiFi connected");
    DEBUG_PRINT("IP address: ");
    DEBUG_PRINTLN(WiFi.localIP());
}

void startOTA() { // Start the OTA service
  ArduinoOTA.setHostname(OTAName);
  ArduinoOTA.onStart([]() {
  DEBUG_PRINTLN("Start");
  });
  ArduinoOTA.onEnd([]() {
   DEBUG_PRINTLN("\r\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
 DEBUG_PRINT("Progress: ");
 DEBUG_PRINTLN(progress / (total / 100));
  });
  ArduinoOTA.onError([](ota_error_t error) {
 DEBUG_PRINT("Error: ");
    if (error == OTA_AUTH_ERROR) DEBUG_PRINTLN("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) DEBUG_PRINTLN("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) DEBUG_PRINTLN("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) DEBUG_PRINTLN("Receive Failed");
    else if (error == OTA_END_ERROR) DEBUG_PRINTLN("End Failed");
  
  });
  ArduinoOTA.begin();
  DEBUG_PRINTLN("ArduinoOTA server starting...");
}

void startMDNS() { // Start the mDNS responder
  MDNS.begin(mdnsName);                        // start the multicast domain name server
  DEBUG_PRINT("mDNS responder started: http://");
  DEBUG_PRINT(mdnsName);
  DEBUG_PRINTLN(".local");
}

void startFS() { // Start the LittleFS and list all contents
  SPIFFS.begin();                             // Start the SPI Flash File System (LittleFS)
  DEBUG_PRINTLN("LittleFS started. Contents:");
  {
    Dir dir = SPIFFS.openDir("/");
    while (dir.next()) {                      // List the file system contents
      String fileName = dir.fileName();
      size_t fileSize = dir.fileSize();
      DEBUG_PRINT(fileName.c_str());
      DEBUG_PRINT(" : ");
      DEBUG_PRINTLN(formatBytes(fileSize).c_str());
    }
    DEBUG_PRINTLN("\n");
  }
} 

String formatBytes(size_t bytes) { // convert sizes in bytes to KB and MB
  if (bytes < 1024) {
    return String(bytes) + "B";
  } else if (bytes < (1024 * 1024)) {
    return String(bytes / 1024.0) + "KB";
  } else if (bytes < (1024 * 1024 * 1024)) {
    return String(bytes / 1024.0 / 1024.0) + "MB";
  }
}

void startServer() { // Start a HTTP server with a file read handler and an upload handler
  server.on("/hello", handleHello);
  server.onNotFound(handleNotFound);
  server.begin();                             // start the HTTP server
  DEBUG_PRINTLN("HTTP server started.");
}

/*__________________________________________________________SERVER_HANDLERS__________________________________________________________*/

void handleHello() {
  server.send(200, "text/plain", "hello from esp8266!");
}
void handleNotFound() { // if the requested file or page doesn't exist, return a 404 not found error
  if (!handleFileRead(server.uri())) {        // check if the file exists in the flash memory (LittleFS), if so, send it
    server.send(404, "text/plain", "404: File Not Found");
  }
}

bool handleFileRead(String path) { // send the right file to the client (if it exists)
  DEBUG_PRINTLN("handleFileRead: " + path);
  if (path.endsWith("/")) path += "index.html";          // If a folder is requested, send the index file
  String contentType = getContentType(path);             // Get the MIME type
  String pathWithGz = path + ".gz";
  if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) { // If the file exists, either as a compressed archive, or normal
    if (SPIFFS.exists(pathWithGz))                         // If there's a compressed version available
      path += ".gz";                                         // Use the compressed verion
    File file = SPIFFS.open(path, "r");                    // Open the file
    size_t sent = server.streamFile(file, contentType);    // Send it to the client
    file.close();                                          // Close the file again
    DEBUG_PRINTLN(String("\tSent file: ") + path);
    return true;
  }
  DEBUG_PRINTLN(String("\tFile Not Found: ") + path);   // If the file doesn't exist, return false
  return false;
}

String getContentType(String filename) { // determine the filetype of a given filename, based on the extension
  if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  else if (filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

void avg(){
  const byte avgAmount = 64;
  
  for (byte x = 0 ; x < avgAmount ; x++)
  {
    baseValue += max30102Sensor.getIR(); //Read the IR value
  }
  baseValue /= avgAmount;
   }
