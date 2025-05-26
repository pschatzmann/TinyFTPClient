


#include "WiFi.h"
#include "ArduinoFTPClient.h"

FTPClient<WiFiClient> client;

void setup() {
    Serial.begin(115200);

    // connect to WIFI
    WiFi.begin("network name", "password");
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
    client.begin(IPAddress(192,168,1,10), "user", "password");

    FTPFile file = client.open("/home/pschatzmann/configure-linux.sh");
    Serial.println(file.name());
    Serial.println(file.size());
    Serial.println(file.isDirectory());
    
    // clenaup
    client.end();
}

void loop() {
}