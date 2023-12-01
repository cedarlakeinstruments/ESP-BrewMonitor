#include <math.h>

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include "LittelfuseThermistor.h"

// **************************  Configuration ***********************
// WiFiMode_t MODE = WIFI_STA;
WiFiMode_t MODE = WIFI_AP;

// **********************************************************************

// Set SSID name if in Station mode
#ifndef STASSID
#define STASSID ""
#define STAPSK  ""
#endif

#define DIV1_STYLE Style='background-color:black;color:yellow;width:20vw'

// Function prototypes
void handleRoot();
void handleNotFound();
char* buildPage(float f);
float Map(double input, float inMin, float inMax, float outMin, float outMax);
float readTemp(void);
float getThermistorReading(float);
float cToF(float c);
void sendTempData(void);
void updateSetpoint(void);
void runTc(void);


// Pin definitions
const int led = LED_BUILTIN;
const int THERMAL_PIN = D0;
const int HEAT_COOL_DIR_PIN = D5;

// Constants
const float P_CONSTANT = 4.0;
const char* ssid = STASSID;
const char* password = STAPSK;
char pageBuffer[2000];
float _temperature = readTemp();
float _setpoint = 0;
unsigned long _timestamp = 0;

ESP8266WebServer server(80);

char* buildPage(float temp)
{
    const char *page =
        "<head>\
            <style>\
                body{background-color:black; color:yellow;font-family:sans-serif;font-size:12}\
                .status{margin-left:30vw;padding-left:1vw;background-color:darkslateblue; color:white;width:30vw;font-size:24}\
                .control{margin-left:30vw;padding-left:1vw;background-color:darkslateblue; color:white;width:30vw;font-size:24}\
            </style>\
            <script>\
                setInterval(function()\
                {\
                    getData();\
                }, 5000); \
                function getData() {\
                  var xhttp = new XMLHttpRequest();\
                  xhttp.onreadystatechange = function() {\
                    if (this.readyState == 4 && this.status == 200) {\
                      document.getElementById('temperature').innerHTML =\
                      this.responseText;\
                    }\
                  };\
                  xhttp.open('GET', 'tempRead', true);\
                  xhttp.send();\
                }\
            </script>\
         </head>\
         <body>\
             <div id='temperature' class='status'>%5.1fF</div>\
             <div class='control'>\
                <form action='/updateSetpoint' method='GET'>\
                    <input type = 'button' id = 'increase' value= '+'>\
                    <input type = 'button' id = 'decrease' value= '-'>\
                    <input type='submit' value='Change Setpoint'/>\
                </form>\
            </div>\
         </body>";

    sprintf(pageBuffer, page, temp);
    return pageBuffer;
}

// Handle call to base URL
void handleRoot()
{
    server.send(200, "text/html", buildPage(_temperature));
    digitalWrite(led, 0);
    delay(50);
    digitalWrite(led, 1);
}

// URL not found
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
    digitalWrite(led, 1);

    pinMode (THERMAL_PIN, OUTPUT);
    analogWrite(THERMAL_PIN, 0);

    pinMode (HEAT_COOL_DIR_PIN, OUTPUT);
    Serial.begin(115200);
    WiFi.mode(MODE);
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

    if (MDNS.begin("TMon-1"))
    {
        MDNS.addService("http", "tcp", 80);
        // Start the mDNS responder for esp8266.local
        Serial.println("mDNS responder started");
    }
    else
    {
        Serial.println("Error setting up MDNS responder!");
    }

    // Setup handlers
    server.on("/", handleRoot);

    server.on("/tempRead", sendTempData);

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

    server.on("/updateSetpoint", updateSetpoint);   

    server.begin();
    Serial.println("HTTP server started");
}

// Main loop
void loop(void)
{
    static unsigned long lastTime = 0;
    _timestamp = millis() ;
    if (millis() - lastTime > 500)
    {
        lastTime = millis();
        _temperature = readTemp();
        runTc();
    }

    server.handleClient();
    MDNS.update();
}

// Temperature control
void runTc(void)
{
    float error = (_setpoint - _temperature );

    // Determine if heating or cooling
    digitalWrite(HEAT_COOL_DIR_PIN, error >= 0);
    // PWM output
    float output = error * P_CONSTANT + _temperature;
    analogWrite(THERMAL_PIN, output);
    Serial.printf("out: %8.2f, set: %8.2f, feedback:%8.2f\n", output, _setpoint, _temperature);
}

void updateSetpoint(void)
{
    Serial.println("Update setpoint");
}

// Send temperature data to AJAX call
void sendTempData(void)
{
    Serial.println("AJAX call to send temperature data");
    char tempData[20];
    sprintf(tempData,"%5.1fF", _temperature);
    server.send(200, "text/plain", tempData);
}

///
/// Returns temp in F
float readTemp(void)
{
    float volts = analogRead(A0) * 3.3 / 1024.0;
    float temp = getThermistorReading(volts);
    return temp;
}

float getThermistorReading(float volts)
{
    // Convert voltage to resistance
    const double Rs = 12000.0;
    double ohms = (Rs * volts) / (3.3 - volts);
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
    //Serial.printf("v: %8.2f, o:%8.2f, t:%8.2f\n", volts, ohms, temp);
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
