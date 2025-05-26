#include "WiFi.h"
#include "FTPClient.h"

FTPClient<WiFiClient> client;

void setup() {
    Serial.begin(115200);

    // connect to WIFI
    WiFi.begin("network name", "password");
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }

#ifdef ESP32
    IPAddress localAddress = WiFi.localIP();
    Serial.println();
    Serial.print("Started with address ");
    Serial.println(localAddress.toString());
#endif
    // optional logging
    FTPLogger::setOutput(Serial);
    //FTPLogger::setLogLevel(LOG_DEBUG);

    // open connection
    client.begin(IPAddress(192,168,1,10), "ftp-userid", "ftp-password");

    // list files
    for (auto file : client.ls("/"))  {
        Serial.println(file.name());
    }
    
    // clenaup
    client.end();
}


void loop() {
}