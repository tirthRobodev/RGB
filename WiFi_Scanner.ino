#include "WiFi.h"

void setup() {
  Serial.begin(115200);
  
  // Set WiFi to station mode and disconnect from an AP if it was previously connected
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  Serial.println("WiFi Scanner Setup complete.");
}

void loop() {
  Serial.println("Scanning for nearby WiFi networks...");

  // WiFi.scanNetworks will return the number of networks found
  int n = WiFi.scanNetworks();
  Serial.println("Scan complete.");
  
  if (n == 0) {
      Serial.println("No networks found.");
  } else {
    Serial.print(n);
    Serial.println(" networks found:");
    
    for (int i = 0; i < n; ++i) {
      // Print SSID and RSSI for each network found
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.print(" dBm)");
      
      // Print an asterisk if the network requires a password
      Serial.println((WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? " [OPEN]" : " [SECURE]");
      delay(10);
    }
  }
  Serial.println("\n----------------------------------\n");

  // Wait 5 seconds before scanning again
  delay(5000);
}
