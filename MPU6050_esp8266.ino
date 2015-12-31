// I2C device class (I2Cdev) demonstration Arduino sketch for MPU6050 class
// 10/7/2011 by Jeff Rowberg <jeff@rowberg.net>
// Updates should (hopefully) always be available at https://github.com/jrowberg/i2cdevlib
//
// Changelog:
//      2013-05-08 - added multiple output formats
//                 - added seamless Fastwire support
//      2011-10-07 - initial release

/* ============================================
I2Cdev device library code is placed under the MIT license
Copyright (c) 2011 Jeff Rowberg

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
===============================================
*/

/*TODO:
- Umbau auf binary transmission with minimal packet_size
*/

#define AP
#define DELAY 100   //ms

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>



#ifdef AP
const char *Host="192.168.4.255";
const char *apssid = "VMACCEL";
const char *appwd = "velomobil";
#else
//const char *Host="192.168.178.23";
const char *Host="192.168.178.255";
//const char *Host="255.255.255.255";   // does not work
const char *ssid = "FRITZ!Box 7312";
const char *password = "38018563552051264893";
#endif

unsigned int Port = 4742;      // local port to listen for UDP packets
WiFiUDP udp;
ESP8266WebServer server ( 80 );


#define PACKET_LEN  9*2
int16_t packetBuffer[9];

// I2Cdev and MPU6050 must be installed as libraries, or else the .cpp/.h files
// for both classes must be in the include path of your project
#include "I2Cdev.h"
#include "MPU6050.h"

// Arduino Wire library is required if I2Cdev I2CDEV_ARDUINO_WIRE implementation
// is used in I2Cdev.h
#include "Wire.h"

// class default I2C address is 0x68
// specific I2C addresses may be passed as a parameter here
// AD0 low = 0x68 (default for InvenSense evaluation board)
// AD0 high = 0x69
MPU6050 accelgyro;
//MPU6050 accelgyro(0x69); // <-- use for AD0 high

int16_t ax, ay, az;
int16_t gx, gy, gz;
uint8_t mpuid;
int16_t temperature;
uint16_t packet_count=0;
uint8_t delay_val=DELAY;
String rxmsg;




#define LED_PIN 1
bool blinkState = false;

void toggle_LED(){
    blinkState = !blinkState;
    digitalWrite(LED_PIN, blinkState);
}

void setup() {
    #ifdef AP
    WiFi.softAP(apssid,appwd);
    #else
    WiFi.begin(ssid,password);
    #endif
    
    // join I2C bus (I2Cdev library doesn't do this automatically)
    //Wire.begin(4,5);
    Wire.begin(12,13);

    accelgyro.initialize();
    accelgyro.setFullScaleGyroRange(MPU6050_GYRO_FS_2000);
    accelgyro.setFullScaleAccelRange(MPU6050_ACCEL_FS_8);
    //mpuid = accelgyro.getDeviceID();
    //accelgyro.setTempSensorEnabled(0);

    // use the code below to change accel/gyro offset values
    /*
    Serial.println("Updating internal sensor offsets...");
    // -76	-2359	1688	0	0	0
    Serial.print(accelgyro.getXAccelOffset()); Serial.print("\t"); // -76
    Serial.print(accelgyro.getYAccelOffset()); Serial.print("\t"); // -2359
    Serial.print(accelgyro.getZAccelOffset()); Serial.print("\t"); // 1688
    Serial.print(accelgyro.getXGyroOffset()); Serial.print("\t"); // 0
    Serial.print(accelgyro.getYGyroOffset()); Serial.print("\t"); // 0
    Serial.print(accelgyro.getZGyroOffset()); Serial.print("\t"); // 0
    Serial.print("\n");
    accelgyro.setXGyroOffset(220);
    accelgyro.setYGyroOffset(76);
    accelgyro.setZGyroOffset(-85);
    Serial.print(accelgyro.getXAccelOffset()); Serial.print("\t"); // -76
    Serial.print(accelgyro.getYAccelOffset()); Serial.print("\t"); // -2359
    Serial.print(accelgyro.getZAccelOffset()); Serial.print("\t"); // 1688
    Serial.print(accelgyro.getXGyroOffset()); Serial.print("\t"); // 0
    Serial.print(accelgyro.getYGyroOffset()); Serial.print("\t"); // 0
    Serial.print(accelgyro.getZGyroOffset()); Serial.print("\t"); // 0
    Serial.print("\n");
    */

    // configure Arduino LED for
    pinMode(LED_PIN, OUTPUT);
    
    while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    toggle_LED();
    }
    digitalWrite(LED_PIN, true);
    udp.begin(Port);
}


void loop() {
    //temperature = accelgyro.getTemperature();

    // read raw accel/gyro measurements from device
    accelgyro.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

    // these methods (and a few others) are also available
    //accelgyro.getAcceleration(&ax, &ay, &az);
    //accelgyro.getRotation(&gx, &gy, &gz);

    packetBuffer[0] = millis();
    packetBuffer[2] = packet_count;
    packetBuffer[3] = ax;
    packetBuffer[4] = ay;
    packetBuffer[5] = az;
    packetBuffer[6] = gx;
    packetBuffer[7] = gy;
    packetBuffer[8] = gz;

    udp.beginPacket(Host,Port);
    udp.write((char *)&packetBuffer,PACKET_LEN);
    udp.endPacket();
    packet_count++;

    int rx = udp.parsePacket();
    if (rx>5){
        udp.read((char *) &packetBuffer,PACKET_LEN);
        rxmsg = String((char *) &packetBuffer);
        if (rxmsg.startsWith("delay")){
            delay_val = String(rxmsg.substring(5,9)).toInt();
            }
        if (rxmsg.startsWith("led"))
            digitalWrite(LED_PIN, rxmsg.substring(4,5)=="1");
        if (rxmsg.startsWith("t"))
            toggle_LED();
        
    }
    delay(delay_val);
}

