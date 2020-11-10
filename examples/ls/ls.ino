#include "ArduinoFTPClient.h"

FTPClient client;

void  list(char *dir) {
    FileNameIterator it = client.ls("/");
    for (auto fileIt = it.begin(); fileIt != it.end(); fileIt++)  {
        char* fileName = *fileIt;
        Serial.println(fileName)
//      uncomment for recursive list
//      listTree(fileName);
    }
}

void setup() {
    Serial.begin(115200);

    // connect to WIFI
    WiFi.begin("Phil Schatzmann", "sabrina01");
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }

    IPAddress localAddress = WiFi.localIP();
    Serial.println();
    Serial.print("Started with address ");
    Serial.println(localAddress.toString());

    // optional logging
    FTPLogger::setOutput(Serial);
    FTPLogger::setLogLevel(LOG_DEBUG);

    // open connection
    client.begin(IPAddress(192,168,1,10), "user", "password");

    list("/");
    
    // clenaup
    client.end();
}


void loop() {
}