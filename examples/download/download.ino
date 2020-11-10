#include "ArduinoFTPClient.h"

FTPClient client;

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
    //FTPLogger::setLogLevel(LOG_DEBUG);
    
    // open connection
    client.begin(IPAddress(192,168,1,10), "pschatzmann", "sabrina1");

    // copy data from file
    FTPFile file = client.open("/Users/pschatzmann/Dropbox/Manuals/Mavlink.pdf");
    int len;
    byte buffer[100];
    while (file.available()>0){
        len = file.read(buffer,100);
        Serial.write(buffer,len);
    }

    // clenaup
    file.close();
    client.end();
}


void loop() {
}