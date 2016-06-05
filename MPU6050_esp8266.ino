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
- Sys updaten (VCC);
- File deleter (free file space checken (limit?) und ev. älteste Files löschen)
- MPU data display
- MPU Configurator
- Network Configuration via Webinterface
- Progmem warning bearbeiten
*/

//#define ACCESS_POINT
#define DELAY 1   //ms
#define RAMLOGSIZE 1024
#define I2CBUS 4,5       //this is nodemcu port 1,2 see: https://github.com/nodemcu/nodemcu-devkit-v1.0#pin-map

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <FS.h>
#include "/home/tt/sketchbook/system.inc"
#include "/home/tt/sketchbook/web.inc"

const char* myhostname = "gyro";

#ifdef ACCESS_POINT 
const char *Host="192.168.4.255";
const char *apssid = "VMACCEL";
const char *appwd = "velomobil";
#else
#include "/home/tt/sketchbook/secrets.inc"
#endif

ESP8266WebServer server ( 80 );
ESP8266HTTPUpdateServer httpUpdater;
File fsUploadFile;

unsigned short dptr=0;
unsigned short dbegin=0;

struct dat{
    unsigned long time;
    int16_t gx;
    int16_t gy;
    int16_t gz;
} data[RAMLOGSIZE];


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

int16_t gx, gy, gz;
uint8_t delay_val=DELAY;

File f;

const int led=13;
bool blinkState = false;

void toggle_LED(){
    blinkState = !blinkState;
    digitalWrite(led, blinkState);
}

void handleRoot(){
    char msg[200];
    snprintf(msg,200,"MPUID:\t%d\n",
        accelgyro.getDeviceID());
    server.send(200,"text/plain", msg);
}

void handleFileUpload(){
  HTTPUpload& upload = server.upload();
  if(upload.status == UPLOAD_FILE_START){
    String filename = upload.filename;
    if(!filename.startsWith("/")) filename = "/"+filename;
    fsUploadFile = SPIFFS.open(filename, "w");
    filename = String();
  } else if(upload.status == UPLOAD_FILE_WRITE){
    if(fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize);
  } else if(upload.status == UPLOAD_FILE_END){
    if(fsUploadFile)
      fsUploadFile.close();
  }
}

void handleNotFound() {
    String path = server.uri();
    if(path.endsWith("/")) path += "index.htm";
    String contentType = getContentType(path);
    if (SPIFFS.exists(path)){
        File file = SPIFFS.open(path, "r");
        server.streamFile(file,contentType);
        file.close();
    }else{ 
        digitalWrite ( led, 1 );
        String message = "File Not Found\n\n";
        message += "URI: ";
        message += server.uri();
        message += "\nMethod: ";
        message += ( server.method() == HTTP_GET ) ? "GET" : "POST";
        message += "\nArguments: ";
        message += server.args();
        message += "\n";

        for ( uint8_t i = 0; i < server.args(); i++ ) {
            message += " " + server.argName ( i ) + ": " + server.arg ( i ) + "\n";
        }

        server.send ( 404, "text/plain", message );
    }
	digitalWrite ( led, 0 );
}

void mpuinfo(){
    char msg[200];
    snprintf(msg,200,"MPUID:\t%d\nFIFOEnabled:\t%d\n",
        accelgyro.getDeviceID(),accelgyro.getFIFOEnabled());
    server.send(200,"text/plain", msg);
}

void mputemp(){
    String msg = String(accelgyro.getTemperature());
    server.send(200,"text/plain",msg);
}
    

void newlog(){
    // backup old logfile
    if (SPIFFS.exists("/data.txt")){
        Serial.println("data.txt exists");
        for (unsigned i=0;i<9999;i++){
            String backupname=String("/data")+String(i)+".txt";
            if (!SPIFFS.exists(backupname)){
                Serial.println("Renaming data.txt to "+backupname);
                SPIFFS.rename("/data.txt",backupname);
                break;
            }
        }
    }
}

void setup() {
    Serial.begin(115200);
    SPIFFS.begin();
    #ifdef ACCESS_POINT
    WiFi.softAP(apssid,appwd);
    #else
    WiFi.begin(ssid,password);
    #endif
    newlog();
    
    // join I2C bus (I2Cdev library doesn't do this automatically)
    //Wire.begin(4,5);
    Wire.begin(I2CBUS);

    accelgyro.initialize();
    accelgyro.setFullScaleGyroRange(MPU6050_GYRO_FS_2000);
    accelgyro.setFullScaleAccelRange(MPU6050_ACCEL_FS_8);
    accelgyro.setTempSensorEnabled(0);

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

    pinMode(led, OUTPUT);
    while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    toggle_LED();
    }
    digitalWrite(led, true);

    MDNS.begin(myhostname);
    httpUpdater.setup(&server);
    server.begin();
	server.on ( "/", handleRoot );
    server.on("/mpuinfo",mpuinfo);
    server.on("/temp",mputemp);
    server.on("/temperature",mputemp);
    server.on("/sys",system_html);
    server.on("/system",system_html);
    server.on("/upload",HTTP_POST,[](){ server.send(200,"text/plain","");},handleFileUpload);
	server.onNotFound ( handleNotFound );

    MDNS.addService("http","tcp",80);

    if (SPIFFS.exists("/data.txt")){
        Serial.println("data.txt exists");
        for (unsigned i=0;i<9999;i++){
            String backupname=String("/data")+String(i)+".txt";
            if (!SPIFFS.exists(backupname)){
                Serial.println("Renaming data.txt to "+backupname);
                SPIFFS.rename("/data.txt",backupname);
                break;
            }
        }
    }

    f=SPIFFS.open("/data.txt","a");
    f.println(String(accelgyro.getTemperature()));
    f.close();

}


void loop() {
	ESP.wdtDisable();
	server.handleClient();

    // read raw accel/gyro measurements from device
    //accelgyro.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
    //accelgyro.getAcceleration(&ax, &ay, &az);
    accelgyro.getRotation(&gx, &gy, &gz);

    data[dptr].time = millis();
    data[dptr].gx = gx;
    data[dptr].gy = gy;
    data[dptr].gz = gz;
    dptr++;

    if (dbegin!=0){
        dbegin++;
        if (dbegin>=RAMLOGSIZE)
        {
            dbegin = 0;
        }
    }
    if (dptr>=RAMLOGSIZE){
        //RAM filled
        f=SPIFFS.open("/data.txt","a");
        for (int i=0;i<RAMLOGSIZE;i++)
            f.printf("%u\t%i\t%i\t%i\n",data[i].time,data[i].gx,data[i].gy,data[i].gz);
        f.close();
        dptr = 0;
        dbegin= 1;
    } 

   // hier muss die Auswertung rein. Pendelstartdetection, Pendelfrequenz und Anzahl Pendelschwingungen (via Gyrovorzeichenwechsel)

    delay(delay_val);
}

