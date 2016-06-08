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
#define LOGDELAY 1   //ms
#define LOOPDELAY 1000 //ms
#define RAMLOGSIZE 2048 //1024
#define I2CBUS 4,5       //this is nodemcu port 1,2 see: https://github.com/nodemcu/nodemcu-devkit-v1.0#pin-map

#define LOG_STOP 0
#define LOG_START 1

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <FS.h>
#include "/home/tt/sketchbook/system.inc"
#include "/home/tt/sketchbook/web.inc"

void newlog();

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

short logstate = LOG_STOP;
unsigned short dptr=0;
unsigned short dbegin=0;

unsigned long lasttime=0;

struct dat{
    unsigned long time;
    int16_t gx;
    int16_t gy;
    int16_t gz;
    int16_t ax;
    int16_t ay;
    int16_t az;
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
MPU6050 mpu;
//MPU6050 mpu(0x69); // <-- use for AD0 high

int16_t gx, gy, gz;
int16_t ax, ay, az;
uint8_t delay_val=LOGDELAY;
uint16_t loopdelay=LOOPDELAY;

File logfile;

const int led=13;
bool blinkState = false;

void toggle_LED(){
    blinkState = !blinkState;
    digitalWrite(led, blinkState);
}

void handleRoot(){
    String msg="<html><head><title>Gyrologger</title><style>body{background-color: #AAAAAA; font-family: Arial, Helvetica, Sans-Serif; Color: #000000; }</style></head><body>";
    msg += "Logger: ";
    switch(logstate){
        case LOG_START:
            msg += "running";
            break;
        case LOG_STOP:
            msg += "stopped";
            break;
        default:
            msg += "fault";
    }
    server.send(200,"text/html", msg);
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
    Serial.println("not found");
    Serial.println(path);
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

void handleLog(){
    String argname, argval;
    int argint;
    unsigned int rxkey=0;
    static int txkey;
    String msg="<html><head><title>Data Logging</title><style>body{background-color: #AAAAAA; font-family: Arial, Helvetica, Sans-Serif; Color: #000000; }</style></head><body>";
    for ( uint8_t i = 0; i < server.args(); i++ ) {
        argname = server.argName(i);
        argval = server.arg(i);
        argint = argval.toInt();
        Serial.println(server.uri());
        Serial.print(argname+" = "+argval +" "+argint);
        if (argname=="key")
            rxkey = argval.toInt();
        if (argname=="start"){
            newlog();
            logfile=SPIFFS.open("/data.txt","a");
            logfile.println(String(mpu.getTemperature()));
            Serial.println("temp:"+String(mpu.getTemperature()));
            logfile.close();
            logstate = LOG_START;
            msg += "Data Logging started";
            }
        if (argname=="stop"){
            logstate = LOG_STOP;
            msg += "Data Logging stopped";
        }
    }
    msg +="</body></html>";
    server.send(200,"text/html",msg);
}

void handleFile(){
    String argname, argval;
    int argint;
    unsigned int rxkey=0;
    static int txkey;
    String msg="<html><head><title>File System</title><style>body{background-color: #AAAAAA; font-family: Arial, Helvetica, Sans-Serif; Color: #000000; }</style></head><body>";
    for ( uint8_t i = 0; i < server.args(); i++ ) {
        argname = server.argName(i);
        argval = server.arg(i);
        argint = argval.toInt();
        Serial.println(server.uri());
        Serial.print(argname+" = "+argval +" "+argint);
        if (argname=="key")
            rxkey = argval.toInt();
        if (argname=="format"){
            if (argval!="yes") break;
            if ((rxkey!=0) and (rxkey==txkey)){
                SPIFFS.format();
                msg += "formatting done";
            }else{
                txkey=random(1,100000);
                msg += "Do you really want to format the onboard filesystem and loose all files? <a href=\"file?key="+String(txkey)+"&format=yes\">yes</a>...<a href=\"/\">NO</a>";
            }
        }
        if (argname=="info"){
            if (argval==""){
                FSInfo fsinfo;
                SPIFFS.info(fsinfo);
                msg += "Total Bytes: "+String(fsinfo.totalBytes)+" </br>";
                msg += "Used Bytes: "+String(fsinfo.usedBytes)+" </br>";
            }
        }
        if ((argname=="unmount") and (argval=="yes")) {
                SPIFFS.end();
                msg += "Filesystem unmounted";
        }

    }
    msg +="</body></html>";
    server.send(200,"text/html",msg);

}
    

void mpuinfo(){
    String msg="<html><head><title>MPU Info</title><style>body{background-color: #AAAAAA; font-family: Arial, Helvetica, Sans-Serif; Color: #000000; }</style></head><body>";
    msg +="<td> MPUID: "+String(mpu.getDeviceID());
    msg +="</td><td> Temperature Sensor </td><td>" + mpu.getTempSensorEnabled()?"enabled":"disabled";
    msg +="</td><td> FIFOEnabled:"+String(mpu.getFIFOEnabled());
    msg +="</body></html>";
    server.send(200,"text/html", msg);
}

void mpuconfig(){
        String msg;
        String argname, argval;
        int argint;

        for ( uint8_t i = 0; i < server.args(); i++ ) {
            argname = server.argName(i);
            argval = server.arg(i);
            argint = argval.toInt();
            Serial.println(server.uri());
            Serial.print(argname+"="+argval +" "+argint);

            if((argname=="temp") or (argname == "temperature"))
                msg = String(mpu.getTemperature());
            else if(argname=="id")
                msg = String(mpu.getDeviceID());
            else if(argname=="fifo"){
                if (argval!="")
                    mpu.setFIFOEnabled(argint);
                msg = String(mpu.getFIFOEnabled());
                }
            else if(argname=="ax")
                msg = String(ax);
            else if(argname=="ay")
                msg = String(ay);
            else if(argname=="az")
                msg = String(az);
            else if(argname=="gx")
                msg = String(gx);
            else if(argname=="gy")
                msg = String(gy);
            else if(argname=="gz")
                msg = String(gz);
            else if(argname=="rate"){
                if (argval!="")
                    mpu.setRate(argint);
                msg = String(mpu.getRate());
                }
            
        server.send(200,"text/plain",msg);
    }
}


void filedelete(){
        String filename="nothing";
        String argname;
        String argval;
        static unsigned int txkey;
        unsigned int rxkey=0;
        String msg="<html><head><title>File Delete</title><style>body{background-color: #FF8888; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }</style></head><body>";

        for ( uint8_t i = 0; i < server.args(); i++ ) {
            argname = server.argName(i);
            argval = server.arg(i);
            if (argname=="filename")
                filename = argval;
            if (argval=="key")
                rxkey = argval.toInt();
        }
        switch(rxkey){
            case 0:
                txkey=random(1,100000);
                msg+="Delete "+filename+"?"+"</br><a href=\""+server.uri()+"?key="+String(txkey)+"&filename="+filename+"\">YES</a>---<a href=\"/\">NO</a>";
                break;
            default:
                msg+="filename";
                if (rxkey==txkey){
                    SPIFFS.remove(filename);
                    msg+=filename+" deleted";
                }else
                    msg+=filename+" not deleted";
                break;
    }
                msg+="</body></html>";
                server.send(200,"text/html",msg);
}
            
void newlog(){
    Serial.println("Creating new logfileile");
    // backup old logfileile
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
	ESP.wdtDisable();
    Serial.begin(115200);
    SPIFFS.begin();
    #ifdef ACCESS_POINT
    WiFi.softAP(apssid,appwd);
    #else
    WiFi.begin(ssid,password);
    #endif
    newlog();
    
    Wire.begin(I2CBUS);

    mpu.initialize();
    mpu.setRate(10);
    mpu.setFullScaleGyroRange(MPU6050_GYRO_FS_2000);
    mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_8);
    mpu.setTempSensorEnabled(1);

    // use the code below to change accel/gyro offset values
    /*
    Serial.println("Updating internal sensor offsets...");
    // -76	-2359	1688	0	0	0
    Serial.print(mpu.getXAccelOffset()); Serial.print("\t"); // -76
    Serial.print(mpu.getYAccelOffset()); Serial.print("\t"); // -2359
    Serial.print(mpu.getZAccelOffset()); Serial.print("\t"); // 1688
    Serial.print(mpu.getXGyroOffset()); Serial.print("\t"); // 0
    Serial.print(mpu.getYGyroOffset()); Serial.print("\t"); // 0
    Serial.print(mpu.getZGyroOffset()); Serial.print("\t"); // 0
    Serial.print("\n");
    mpu.setXGyroOffset(220);
    mpu.setYGyroOffset(76);
    mpu.setZGyroOffset(-85);
    Serial.print(mpu.getXAccelOffset()); Serial.print("\t"); // -76
    Serial.print(mpu.getYAccelOffset()); Serial.print("\t"); // -2359
    Serial.print(mpu.getZAccelOffset()); Serial.print("\t"); // 1688
    Serial.print(mpu.getXGyroOffset()); Serial.print("\t"); // 0
    Serial.print(mpu.getYGyroOffset()); Serial.print("\t"); // 0
    Serial.print(mpu.getZGyroOffset()); Serial.print("\t"); // 0
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
    server.on("/log",handleLog);
    server.on("/file",handleFile);
    server.on("/mpu",mpuconfig);
    server.on("/mpuinfo",mpuinfo);
    server.on("/sys",system_html);
    server.on("/system",system_html);
    server.on("/upload",HTTP_POST,[](){ server.send(200,"text/plain","");},handleFileUpload);
    server.on("/delete",filedelete);
	server.onNotFound ( handleNotFound );

    MDNS.addService("http","tcp",80);

    Serial.println("Setup finished");
}


void loop() {
	ESP.wdtDisable();
	server.handleClient();

    // read raw accel/gyro measurements from device
    mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
    //Serial.printf("%lu,%d,%d,%d\r",micros(),ax,ay,az);
    //mpu.getAcceleration(&ax, &ay, &az);
    //mpu.getRotation(&gx, &gy, &gz);

    data[dptr].time = micros()/100;
    data[dptr].gx = gx;
    data[dptr].gy = gy;
    data[dptr].gz = gz;
    data[dptr].ax = ax;
    data[dptr].ay = ay;
    data[dptr].az = az;
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
        if (logstate == LOG_START){
            Serial.println("writing ramlog");
            logfile=SPIFFS.open("/data.txt","a");
            Serial.print(String(logfile));
            for (int i=0;i<RAMLOGSIZE;i++){
                logfile.printf("%lu\t%i\t%i\t%i\t",data[i].time,data[i].gx,data[i].gy,data[i].gz);
                logfile.printf("%i\t%i\t%i\t",data[i].ax,data[i].ay,data[i].az);
                logfile.printf("\n");
                }
            logfile.close();
        }
        dptr = 0;
        dbegin= 1;
    } 

   // hier muss die Auswertung rein. Pendelstartdetection, Pendelfrequenz und Anzahl Pendelschwingungen (via Gyrovorzeichenwechsel)

    delay(delay_val);

    if (millis()<lasttime+loopdelay){
        lasttime=millis();
	    ESP.wdtDisable();
	    server.handleClient();
    }
}

