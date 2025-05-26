
/**
 * FTP download using an ESP32
*/

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

    // copy data from file
    FTPFile file = client.open("/home/ftp-userid/Dropbox/Manuals/Mavlink.pdf");
    int len;
    byte buffer[100];
    while (file.available()>0){
        len = file.readBytes(buffer,100);
        Serial.write(buffer,len);
    }

    // clenaup
    file.close();
    client.end();
}


void loop() {
}