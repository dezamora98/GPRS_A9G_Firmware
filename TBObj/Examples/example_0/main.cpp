#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include "TBObj.h"

void sendCommand(StaticJsonDocument<256>& doc)
{
    String strOut;
    serializeJson(doc,strOut);
    debugln(strOut.c_str());
}

void setup()
{
    randomSeed(millis());
    GPS_Location(false);
}

void loop()
{
    Telemetry_OUT(int,algo,10000);
    Telemetry_OUT(float,humedad,5000);
    SharedAttribute(String,algo2,5000);
    Attribute_IN(int,algo3);
    RPCData_IN(bool,TEST);
    RPCFunction_IN(sendCommand);

    while (true)
    {
        algo = random(100);
        humedad = random(100);
        algo2 = static_cast<String>(random(100));
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        debugln(algo2._Data.c_str());
        if(TEST == true)
        {
            debugln("la variable TEST cambió");
        }
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}