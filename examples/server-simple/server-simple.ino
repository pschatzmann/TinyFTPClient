#include <SD.h>
#include <SPI.h>
#include <WiFi.h>

#include "FTPServer.h"

const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";
const int sdChipSelectPin = 13;

FTPServer<WiFiServer, SDClass> ftpServer(1922, &SD);

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // setup SD
#ifdef ESP32
  SPI.begin(14, 2, 15, sdChipSelectPin);  // SCK, MISO, MOSI, CS
#endif
  while (!SD.begin(sdChipSelectPin)) {
    Serial.println("SD initialization failed");
    delay(1000);
  }

  ftpServer.addUser("ftp", "ftp");
  ftpServer.begin();
  Serial.println("FTP server started");
}

void loop() { ftpServer.doLoop(); }
