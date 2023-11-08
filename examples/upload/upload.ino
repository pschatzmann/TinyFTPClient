#include "WiFi.h"
#include "ArduinoFTPClient.h"

WiFiClient cmd;
WiFiClient data;
FTPClient client(cmd, data);


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
    FTPLogger::setLogLevel(LOG_DEBUG);

    // open connection
    client.begin(IPAddress(192,168,1,10), "user", "password");

    // copy data to file
    FTPFile file = client.open("/test.txt", WRITE_MODE);
    char buffer[100];
    for (int j=0;j<100;j++){
        sprintf(buffer, "test %d", j);
        file.println(buffer);
    }
    file.close();
    client.end();
}


void loop() {
}