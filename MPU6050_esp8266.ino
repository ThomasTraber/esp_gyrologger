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
*/


#define CACHE_SIZE 256*1024  // Bytes  // mehr als 256K macht die Kiste anscheinend langsam
#define LOGDELAY 5   //ms
#define LOOPDELAY 1000 //ms
#define RESULTLOGSIZE 100
#define I2CBUS 4,5       //this is nodemcu port 1,2 see: https://github.com/nodemcu/nodemcu-devkit-v1.0#pin-map
//#define I2CBUS 12,13       //Zahnseideschachtel

#define LOG_STOP 0
#define LOG_START 1
#define LOG_GYRO 1
#define LOG_ACC 2

#include <buildinfo.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <FS.h>
#include <EEPROM.h>
#include "/home/tt/sketchbook/system.inc"
#include "/home/tt/sketchbook/web.inc"

uint16_t filenamecntr;
String glogfilename;
String alogfilename;

const char* myhostname = "gyro";

const char *Host="192.168.4.255";
const char *apssid = "gyro";
const char *appwd = "velomobil";        // at least 8 characters are required
#include "/home/tt/sketchbook/secrets.inc"

ESP8266WebServer server ( 80 );
ESP8266HTTPUpdateServer httpUpdater;
File fsUploadFile;

short logstate = LOG_STOP;
uint8_t logmode = LOG_GYRO|LOG_ACC;
uint16_t logcntr;
unsigned short rptr=0;

unsigned long lasttime=0;

struct res{
    unsigned long time;
    uint16_t gmax;
} results[RESULTLOGSIZE];


#include "I2Cdev.h"
#include "MPU6050.h"
#include "Wire.h"

MPU6050 mpu;

int16_t gx, gy, gz;
int16_t gxlast=0;
int16_t gylast=0;
int16_t gzlast=0;
uint16_t gxmax=0;
uint16_t gymax=0;
uint16_t gzmax=0;
int32_t gxsum=0;
int32_t gysum=0;
int32_t gzsum=0;
int16_t ax, ay, az;
uint8_t logdelay=LOGDELAY;
uint16_t loopdelay=LOOPDELAY;
uint16_t gyrosquelch=1000;       //raw sensor data

File logfile;
File cache;
uint32_t cacheptr=0;

const int led=13;
bool blinkState = false;

// ich weiss nicht, weshalb max als nicht defined angemeckert wird
int imax(int x, int y){
    if (x>y) return x;
    else return y;
}

int8_t sign(int x){
    if (x>0) return 1;
    if (x<0) return -1;
    if (x=0) return 0;
}

void toggle_LED(){
    blinkState = !blinkState;
    digitalWrite(led, blinkState);
}

/* copy cache to separate file
*/
uint32_t cache2file(){
    uint16_t fcntr;
    uint32_t fpos;
    fcntr = EEPROM.read(0) + (EEPROM.read(1)<<8);
    Serial.println("Incrementing Filename Counter"+String(fcntr));
    fcntr++;
    Serial.println(String(fcntr));;
    EEPROM.write(0,fcntr&0xFF);
    EEPROM.write(1,fcntr>>8);
    EEPROM.commit();
    
    fpos = cache.position();
    File f = SPIFFS.open("/data/"+String(fcntr)+".dat","w"); 
    cache.seek(0,SeekSet);
    do{
        f.write(cache.read());
        ESP.wdtFeed();
    }while(cache.position()<fpos);  
    return f.position();
}

uint16_t cache_read_uint16(){
    uint16_t val = cache.read();
    val |= cache.read()<<8;
    return val;
}

int16_t cache_read_int16(){
    int16_t val = cache.read();
    val |= cache.read()<<8;
    return val;
}
uint32_t cache_read_uint32(){
    uint32_t val = cache.read();
    val |= (cache.read()<<8);
    val |= (cache.read()<<16);
    val |= (cache.read()<<24);
    return val;
}

void cache_write_uint16(uint16_t val){
    cache.write(val&0xFF);
    cache.write((val>>8)&0xFF);
}

void cache_write_int16(int16_t val){
    cache.write(val&0xFF);
    cache.write((val>>8)&0xFF);
}
void cache_write_uint32(uint32_t val){
    cache.write(val&0xFF);
    cache.write((val>>8)&0xff);
    cache.write((val>>16)&0xff);
    cache.write((val>>24)&0xff);
}

uint16_t mpu_get_gyro_fs(){
    switch(mpu.getFullScaleGyroRange()){
        case 0: return 250;
        case 1: return 500;
        case 2: return 1000;
        case 3: return 2000;
        default: return 0;
    }
}

uint16_t mpu_get_acc_fs(){
    switch(mpu.getFullScaleAccelRange()){
        case 0: return 2;
        case 1: return 4;
        case 2: return 8;
        case 3: return 16;
        default: return 0;
    }
}

uint16_t mpu_get_lpf(){
    switch(mpu.getDLPFMode()){
        case 0: return 256;
        case 1: return 188;       
        case 2: return 98;
        case 3: return 42;
        case 4: return 20;
        case 5: return 10;
        case 6: return 5;
        default: return 0;
    }
}

uint16_t mpu_set_lpf(uint16_t bw){
    uint8_t m,i=0;
    uint16_t table[8]={256,188,98,42,20,10,5,0};
    do{
        if (bw>=table[i]){
            mpu.setDLPFMode(i);
            break;
        }
        i++;
    }while(1); 
    return mpu_get_lpf();
}

uint16_t mpu_get_hpf(){
    switch(mpu.getDHPFMode()){
        case 0: return 0;
        case 1: return 5000;       
        case 2: return 2500;
        case 3: return 1250;
        case 4: return 630;
        case 5: return 10;
        case 6: return 9999;
        default: return 8888;
    }
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
    msg += "</br>\n";
    if (rptr>0){
        for (uint8_t i=1;i<=rptr;i++){
            msg += String(results[i].time - results[i-1].time) + "00 us | " + results[i].gmax + "</br>";
        }
    }
    server.send(200,"text/html", msg);
}

String tempstr(){
    int temp = mpu.getTemperature();
    temp = (temp/34+365)/10;
    return String(temp);
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
    Serial.println("not found");
    Serial.println(path);
    if (path=="/data/g.txt")
        path = "/data/g"+String(filenamecntr)+".txt";
    if (path=="/data/a.txt")
        path = "/data/a"+String(filenamecntr)+".txt";
    if (not SPIFFS.exists(path))
        if (SPIFFS.exists(path+".html"))
            path += ".html";
    if (SPIFFS.exists(path)){
        String contentType = getContentType(path);
        File file = SPIFFS.open(path, "r");
        ESP.wdtFeed();
	    ESP.wdtDisable();
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

String metadata(){
    String m;
    m += "# LogCounter="+String(logcntr)+"\n";
    m += "# ESPID:"+String(ESP.getChipId(),HEX)+"\n";
    m += "# MPUID:"+String(mpu.getDeviceID(),HEX)+"\n";
    m += "# Temperature="+tempstr()+"\n";
    m += "# LogDelay="+String(logdelay)+"\n";
    m += "# GyroFullScale="+String(mpu_get_gyro_fs())+"\n";
    m += "# AccFullScale="+String(mpu_get_acc_fs())+"\n";
    m += "# LowPassFilter="+String(mpu_get_lpf())+"\n";
    m += "# HighPassFilter="+String(mpu_get_hpf())+"\n";
    m += "# Time/100us\t";
    m += "\n";
    return m;
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
            String filename;
            Serial.println("TODO: cache copy. Hier?");
            cache.seek(0,SeekSet);
            rptr=0;
            logstate = LOG_START;
            logcntr = EEPROM.read(2) + (EEPROM.read(3)<<8);
            logcntr++;
            EEPROM.write(2,logcntr&0xFF);
            EEPROM.write(3,logcntr>>8);
            EEPROM.commit();
            msg += "Data Logging started";
            }
        if (argname=="stop"){
            logstate = LOG_STOP;
            //cache2file();             //zu langsam. Muss man anders machen. Cachefile vergrößern und Pointers speichern?
            msg += "Data Logging stopped";
        }
        if (argname=="delay"){
            if (argval!=""){
                logdelay = argint;
            }
            msg += String(logdelay);
        }
        if (argname=="delete_all"){
            if (rxkey==0){
                txkey=random(1,100000);
                msg += "You really want to delete all logfiles?<a href=\"log?key=" +String(txkey)+ "&delete_all=\">yes</a>...<a href=\"/\">NO</a></br>";
            }
            else if (rxkey==txkey){
                Dir dir = SPIFFS.openDir(argval);
                String filename ;
                while(dir.next()){
                    filename = dir.fileName();
                    SPIFFS.remove(filename);
                    Serial.print("removing"+filename);
                    msg += "deleting"+filename+"</br>\n";
                }
            }
        }
        if (argname=="mode"){
            if (argval.startsWith("a")) logmode = LOG_ACC;
            else if (argval.startsWith("g")) logmode = LOG_GYRO;
            else if (argval.startsWith("b")) logmode = LOG_GYRO|LOG_ACC;
        }
        if (argname=="reset_fcntr"){
            EEPROM.write(0,0);
            EEPROM.write(1,0);
            EEPROM.commit();
            msg += "done";
        }
        if (argname=="cache"){
            unsigned long fpos = cache.position();
            cache.seek(0,SeekSet);
            //server.send(200,"text/plain",msg);
            //String header;
            //server._prepareHeader(header, 200, "text/plain", CONTENT_LENGTH_UNKNOWN);
            //server.sendContent(header);
            server.sendContent(metadata());
            do{
                msg = String(cache_read_uint32())+"\t"+String(cache_read_int16())+"\t"+String(cache_read_int16())+"\t"+String(cache_read_int16());
                if (logmode==(LOG_GYRO|LOG_ACC))
                    msg += "\t"+String(cache_read_int16())+"\t"+String(cache_read_int16())+"\t"+String(cache_read_int16());
                msg += "\n";
                server.sendContent(msg);
                ESP.wdtFeed();
            }while(cache.position()<fpos);
            cache.seek(fpos,SeekSet);
            //server.send(200,"text/plain",msg);
            return;
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
                logstate=LOG_STOP;
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
                msg += "Total kBytes: "+String(fsinfo.totalBytes/1000)+" </br>";
                msg += "Used kBytes: "+String(fsinfo.usedBytes/1000)+" </br>";
            }
        }
        if ((argname=="unmount") and (argval=="yes")) {
                SPIFFS.end();
                msg += "Filesystem unmounted";
        }
        if (argname=="dir"){
            Dir dir = SPIFFS.openDir(argval);
            FSInfo fsinfo;
            File f;
            String filename ;
            while(dir.next()){
                filename = dir.fileName();
                f = dir.openFile("r");
                Serial.println(filename+String(f.size()));
                msg += "<a href=\""+filename+"\">"+filename+"</a> "+String((f.size()+500)/1000)+"kB...<a href=\"file?delete="+filename+"\">delete</a></br>\n";
            }
            SPIFFS.info(fsinfo);
            msg += String((fsinfo.totalBytes-fsinfo.usedBytes)/1000)+" KB free<br/>";
            msg += String(fsinfo.usedBytes/1000)+" KB used<br/>";
        }
        if (argname=="delete"){
            if (rxkey==0){
                txkey=random(1,100000);
                msg += "You really want to delete " +argval+ "?<a href=\"file?key=" +String(txkey)+ "&delete=" +argval+ "\">yes</a>...<a href=\"/\">NO</a></br>";
            }
            else if (rxkey==txkey){
                SPIFFS.remove(argval);
                msg+=argval+" deleted";
            }
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

void handleConfig(){
        String msg;
        String argname, argval;
        int argint;

        for ( uint8_t i = 0; i < server.args(); i++ ) {
            argname = server.argName(i);
            argval = server.arg(i);
            argint = argval.toInt();
            Serial.println(server.uri());
            Serial.print(argname+"="+argval +" "+argint);

            if(argname=="ssid"){
                if (argval!=""){
                    File f = SPIFFS.open("/ssid","w");
                    f.print(argval);                     
                    f.close();
                    msg = "ssid changed";
                }else
                    msg = "missing ssid string";
            }
            if(argname=="password"){
                if (argval!=""){
                    File f = SPIFFS.open("/password","w");
                    f.print(argval);                     
                    f.close();
                    msg = "password changed";
                }else
                    msg = "missing password string";
            }
        }
        server.send(200,"text/plain",msg);
}

void handleCalc(){
        String msg;
        String argname, argval;
        int argint;

        for ( uint8_t i = 0; i < server.args(); i++ ) {
            argname = server.argName(i);
            argval = server.arg(i);
            argint = argval.toInt();
            Serial.println(server.uri());
            Serial.print(argname+"="+argval +" "+argint);

            if(argname=="gyrosquelch"){
                if (argval!="")
                    gyrosquelch=argint;
                msg = String(gyrosquelch);
            }
        }
        server.send(200,"text/plain",msg);
}

void handleMpu(){
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
                msg = tempstr();
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
            else if(argname=="lpf"){
                if (argval!="")
                    mpu_set_lpf(argint);
                msg = String(mpu_get_lpf());
            }
            else if(argname=="hpf"){
                if (argval!="")
                    mpu.setDHPFMode(argint);
                msg = String(mpu_get_hpf());
            }
            else if(argname=="accrange"){
                if (argval!="")
                    mpu.setFullScaleAccelRange(argint);
                msg = String(mpu.getFullScaleAccelRange());
            }
            else if(argname=="gyrorange"){
                if (argval!="")
                    mpu.setFullScaleGyroRange(argint);
                msg = String(mpu_get_gyro_fs());
            }
            else
                msg = "Unknown command";
            
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
            
void setup() {
    int timeoutcntr;
    EEPROM.begin(4);
	ESP.wdtDisable();
    Serial.begin(115200);
    Serial.println("Entering Setup");
    SPIFFS.begin();
    if(SPIFFS.exists("/ssid")){
        File f = SPIFFS.open("/ssid","r"); 
        ssid = f.readString();
        Serial.println("Reading SSID: "+ssid); 
        f.close();
    }  
    if(SPIFFS.exists("/password")){
        File f = SPIFFS.open("/password","r"); 
        password = f.readString();
        Serial.println("Reading password:"+password); 
        f.close();
    }  
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(),password.c_str());
    if (not SPIFFS.exists("/data"))
        SPIFFS.openDir("/data");
    cache = SPIFFS.open("/data/cache","w+");
    for(int i=0;i<CACHE_SIZE;i++) cache.write(0);   //ist anscheinend notwendig für nachfolgende schnelle Schreibzugriffe
    cache.seek(0,SeekSet);
    
    Wire.begin(I2CBUS);

    Serial.println("Initializing MPU");
    mpu.initialize();
    mpu.setRate(10);
    mpu.setFullScaleGyroRange(MPU6050_GYRO_FS_2000);
    mpu_set_lpf(50);
    mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_2);
    mpu.setTempSensorEnabled(1);

    for (timeoutcntr=0; (WiFi.status() != WL_CONNECTED) and (timeoutcntr<10); timeoutcntr++) {
        delay(500);
    }
    Serial.println("timeoutcntr="+String(timeoutcntr));
    if (timeoutcntr==10){
        WiFi.mode(WIFI_AP);
        WiFi.softAP(apssid,appwd);
    } 
    MDNS.begin(myhostname);
    httpUpdater.setup(&server);
    server.begin();
	server.on ( "/", handleRoot );
    server.on("/log",handleLog);
    server.on("/file",handleFile);
    server.on("/mpu",handleMpu);
    server.on("/calc",handleCalc);
    server.on("/config",handleConfig);
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
    unsigned int time;
	ESP.wdtDisable();
	server.handleClient();

    // read raw accel/gyro measurements from device
    switch(logmode){
        case LOG_GYRO | LOG_ACC:
            mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);break;
        case LOG_GYRO:
            mpu.getRotation(&gx, &gy, &gz);break;
        case LOG_ACC:
            mpu.getAcceleration(&ax, &ay, &az);break;
        default:
            Serial.println("illegal logmode");
        }
    time = micros()/100;
    //Serial.printf("%lu,%d,%d,%d\r",micros(),ax,ay,az);

    gxmax=imax(gxmax,abs(gx));
    gymax=imax(gymax,abs(gy));
    gzmax=imax(gzmax,abs(gz));
    gxsum+=gx;
    gysum+=gy;
    gzsum+=gz;

    if (
        ((abs(gx)>gyrosquelch)
        or (abs(gy)>gyrosquelch)
        or (abs(gz)>gyrosquelch))
    and 
        ( ((rptr>0) and (time!=results[rptr-1].time)) or (rptr==0))
    and (
        ((-sign(gxlast)==sign(gx)) 
        and (abs(gxsum)>abs(gysum))
        and (abs(gxsum)>abs(gzsum))
        )
      or ((-sign(gylast)==sign(gy)) 
        and (abs(gysum)>abs(gzsum))
        and (abs(gysum)>abs(gxsum))
        )
      or ((-sign(gzlast)==sign(gz)) 
        and (abs(gzsum)>abs(gysum))
        and (abs(gzsum)>abs(gxsum))
            )
          )
        ){
            Serial.println(String(time)+" "+String(gx)+" "+String(gy)+"	"+String(gz)+" "+String(gxlast)+" "+String(gylast)+" "+String(gzlast) + " " +String(gxsum)+" "+String(gysum)+" "+String(gzsum));
            // Wechsel der Schwinungsrichtung erkannt
            if (rptr<RESULTLOGSIZE){
                results[rptr].gmax=(mpu_get_gyro_fs()*1000*sqrt(gxmax*gxmax+gymax*gymax+gzmax*gzmax))/0x8000;
                results[rptr].time=time;
                rptr++;
                }
            gxmax=gymax=gzmax=0;
            gxsum=gysum=gzsum=0;
    }
    gxlast=gx;
    gylast=gy;
    gzlast=gz;

    if (logstate == LOG_START){
        cache_write_uint32(time);
        if (logmode&LOG_GYRO){
            cache_write_int16(gx);
            cache_write_int16(gy);
            cache_write_int16(gz);
        }
        if (logmode&LOG_ACC){
            cache_write_int16(ax);
            cache_write_int16(ay);
            cache_write_int16(az);
        }
    } 
    delay(logdelay);

    if (millis()<lasttime+loopdelay){
        lasttime=millis();
	    ESP.wdtDisable();
	    server.handleClient();
    }
}

