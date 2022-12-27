#include <WiFi.h>
#include <AsyncUDP.h>

const char* ssid = "ROAM1";
const char* password = "";
WiFiMode_t MODE = WIFI_AP;
AsyncUDP udp;

//  port to send to
const unsigned int localUdpPort = 49153;

// Multicast address chosen from the internal block
IPAddress multicast_ip_addr(239, 153, 3, 4);

void setup()
{
  int status = WL_IDLE_STATUS;
  Serial.begin(115200);
  WiFi.mode(MODE);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  String hostname = String("SENSOR_" + WiFi.macAddress());
  WiFi.setHostname(hostname.c_str());
  WiFi.begin(ssid);
  Serial.println(hostname);

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("Connected to wifi");
}

void loop()
{
  static byte count = 0;
  char buffer[20];
  sprintf(buffer, "Hello %d", count++);
  // once we know where we got the inital packet from, send data back to that IP address and port
  int v = udp.broadcastTo(buffer, localUdpPort);
  Serial.println(v);
  delay(1000);
}
