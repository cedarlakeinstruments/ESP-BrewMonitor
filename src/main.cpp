#include <math.h>

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include "LittelfuseThermistor.h"

#ifndef STASSID
#define STASSID "ROAM1"
#define STAPSK  ""
#endif

#define DIV1_STYLE Style='background-color:black;color:yellow;width:20vw'

void handleRoot();
void handleNotFound();
char* buildPage(int p1, int p2, float f);
float Map(double input, float inMin, float inMax, float outMin, float outMax);
float readTemp(void);
float getThermistorReading(float);
float cToF(float c);

const char* ssid = STASSID;
const char* password = STAPSK;
char pageBuffer[1000];
float _temperature = readTemp();

ESP8266WebServer server(80);
const int led = LED_BUILTIN;
char* buildPage(int p1, int p2, float temp)
{
    const char* page =
        "<head>\
            <style>\
                body{background-color:black; color:yellow}\
                .status{margin-left:30vw;background-color:darkslateblue; color:dimgrey;width:30vw}\
                .control{margin-left:30vw;background-color:darkslateblue; color:darkslategrey;width:30vw}\
            </style>\
         </head>\
         <body>\
             <div class='status'>Time now %d</div>\
             <div class='status'>D1 %d</div>\
             <div class='status'>D2 %d</div>\
             <div class='status'>%5.1fF</div>\
             <div class='control'>\
                <form action='/LED_ON' method='GET'>\
                    <input type='submit' value='LED ON'/>\
                </form>\
                 <form action='/LED_OFF' method='GET'>\
                    <input type='submit' value='LED OFF'/>\
                </form>\
            </div>\
         </body>";

    sprintf(pageBuffer, page, millis(),p1, p2, temp);
    return pageBuffer;
}

void handleRoot()
{
    server.send(200, "text/html", buildPage(100, 150, _temperature));
}

void handleNotFound()
{
    String message = "File Not Found\n\n";
    message += "URI: ";
    message += server.uri();
    message += "\nMethod: ";
    message += (server.method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArguments: ";
    message += server.args();
    message += "\n";
    for (uint8_t i = 0; i < server.args(); i++)
    {
        message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
    }
    server.send(404, "text/plain", message);
}

void setup(void)
{
    pinMode(led, OUTPUT);
    digitalWrite(led, 0);

    Serial.begin(115200);
    //  WiFi.mode(WIFI_STA);
    WiFi.mode(WIFI_AP);
    WiFi.begin(ssid, password);
    Serial.println("");

    // Wait for connection
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("");
    Serial.print("Connected to ");
    Serial.println(ssid);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    if (MDNS.begin("esp8266"))
    {
        MDNS.addService("http", "tcp", 80);
        // Start the mDNS responder for esp8266.local
        Serial.println("mDNS responder started");
    }
    else
    {
        Serial.println("Error setting up MDNS responder!");
    }

    server.on("/", handleRoot);

    server.on("/inline", []()
              { server.send(200, "text/plain", "this works as well"); });

    server.on("/gif", []()
              {
                  static const uint8_t gif[] PROGMEM = {
                      0x47, 0x49, 0x46, 0x38, 0x37, 0x61, 0x10, 0x00, 0x10, 0x00, 0x80, 0x01,
                      0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x2c, 0x00, 0x00, 0x00, 0x00,
                      0x10, 0x00, 0x10, 0x00, 0x00, 0x02, 0x19, 0x8c, 0x8f, 0xa9, 0xcb, 0x9d,
                      0x00, 0x5f, 0x74, 0xb4, 0x56, 0xb0, 0xb0, 0xd2, 0xf2, 0x35, 0x1e, 0x4c,
                      0x0c, 0x24, 0x5a, 0xe6, 0x89, 0xa6, 0x4d, 0x01, 0x00, 0x3b};
                  char gif_colored[sizeof(gif)];
                  memcpy_P(gif_colored, gif, sizeof(gif));
                  // Set the background to a random set of colors
                  gif_colored[16] = millis() % 256;
                  gif_colored[17] = millis() % 256;
                  gif_colored[18] = millis() % 256;
                  server.send(200, "image/gif", gif_colored, sizeof(gif_colored));
              });

    server.onNotFound(handleNotFound);

    server.begin();
    Serial.println("HTTP server started");
}

void loop(void)
{
    static unsigned long lastTime = 0;
    if (millis() - lastTime > 5000)
    {
        lastTime = millis();
        _temperature = readTemp();
    }

    server.handleClient();
    MDNS.update();
}

///
/// Returns temp in F
float readTemp(void)
{
    float volts = analogRead(A0) * 3.0 / 1024.0;
    float temp = getThermistorReading(volts);
    return temp;
}

float getThermistorReading(float volts)
{
    // Convert voltage to resistance
    const double Rs = 12000.0;
    double ohms = (Rs * volts) / (3.0 - volts);
    float temp = 0;

    // Check for out of range
    const int sizeofTable = sizeof(ResistanceToCelsiusTable) / sizeof(ResistanceToCelsiusTable[0]);
    if (ohms > ResistanceToCelsiusTable[0])
    {
        temp = THERMISTOR_START;
    }
    else if (ohms < ResistanceToCelsiusTable[sizeofTable - 1])
    {
        temp = THERMISTOR_END;
    }
    else
    {
        // Find position in table
        int i = 1;
        for (i = 1; i < sizeofTable; i++)
        {
            if (ResistanceToCelsiusTable[i] <= ohms)
            {
                break;
            }
        }
        temp = THERMISTOR_START + i - 1;
    }

    temp = cToF(temp);
    Serial.printf("v: %8.2f, o:%8.2f, t:%8.2f\n", volts, ohms, temp);
    return temp;
}

float cToF(float c)
{
    float f= c * 9.0/5.0 + 32.0;
    return f;
}

float Map(double input, float inMin, float inMax, float outMin, float outMax)
{
    return (input - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}
