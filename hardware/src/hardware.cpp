
#include <Arduino.h>
#include "DFRobot_PH.h"
#include <EEPROM.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <HX711.h>
#include "Adafruit_SHT31.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <SocketIoClient.h>
#include <WiFiMulti.h>

// servo lib for esp32
#include <ESP32Servo.h>
// for time
#include "time.h"
#include "esp_sntp.h"

// other define
#define VREF 3.3
#define SCOUNT 30 // sum of sample point
const char *HOST = "watermonitoringsystem.net";
#define PORT 80
// #define DEBUG

const char *ssid = "AmericanStudy T1";
const char *pass = "66668888";

// init pin for esp32
#define SERVO_PIN 23
#define SERVO_PIN2 22
#define ONE_WIRE_BUS 32      // DS18B20 pin adc4
#define PH_PIN 34            // PH pin adc6
#define TDS_PIN 35           // TDS pin adc7
#define LOADCELL_SCK_PIN 18  // load cell pin gpio21
#define LOADCELL_DOUT_PIN 19 // load cell pin gpio19
#define SHT31_ADDR 0x44      // SHT31 address
#define SHT31_SDA 21         // SHT31 pin sda gpio18
#define SHT31_SCL 22         // SHT31 pin scl gpio19
#define LEFT_RPWM 5          // motor pin gpio5
#define LEFT_LPWM 17         // motor pin gpio17
#define RIGHT_RPWM 16        // motor pin gpio16
#define RIGHT_LPWM 4         // motor pin gpio4
#define PUMP_RELAY 15        // pump pin gpio2

const char *ntpServer1 = "pool.ntp.org";
const char *ntpServer2 = "time.nist.gov";
const char *time_zone = "ICT-7";  // UTC +7
const long gmtOffset_sec = 25200; // 7*60*60
const int daylightOffset_sec = 0;

// init ph sensor
DFRobot_PH ph;
// init temperature sensor
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature DS18(&oneWire);
// init load cell
HX711 scale;
// init humidity and temperature sensor //pass the twowire object to the constructor if change pin
// TwoWire myWire = TwoWire(0);
// Adafruit_SHT31 sht31 = Adafruit_SHT31(&myWire);
Adafruit_SHT31 sht31 = Adafruit_SHT31();
// init servo
Servo myservo;
Servo myservo2;
// init socket
SocketIoClient webSocket;
WiFiMulti WiFiMulti;

bool isSht31On = false, isFeeding = false, isPumpOn = false;
float tdsValue = 0, phValue = 0, tempValue = 0, humValue = 0, weightValue = 0;
float waterTemp = 0;
float lastWeight = 0;
float SET_WEIGHT = 100.0; // gram

int setHour = 0, setMinute = 0;

unsigned long sendDataPreviousMillis = 0;
unsigned long readSensorPreviousMillis = 0;
unsigned long previousMillis = 0;

struct tm timeinfo;

void hexdump(const void *mem, uint32_t len, uint8_t cols = 16)
{
    const uint8_t *src = (const uint8_t *)mem;
    Serial.printf("\n[HEXDUMP] Address: 0x%08X len: 0x%X (%d)", (ptrdiff_t)src, len, len);
    for (uint32_t i = 0; i < len; i++)
    {
        if (i % cols == 0)
        {
            Serial.printf("\n[0x%08X] 0x%08X: ", (ptrdiff_t)src, i);
        }
        Serial.printf("%02X ", *src);
        src++;
    }
    Serial.printf("\n");
}
// read tds at temperature and return the value
float readTds(float temperature)
{
    float analogValue = 0;
    for (int i = 0; i < SCOUNT; i++)
    {
        analogValue += analogRead(TDS_PIN);
        delay(10);
    }
    analogValue = analogValue / SCOUNT;
    float compensationCoefficient = 1.0 + 0.02 * (temperature - 25.0);
    analogValue = analogValue * VREF / 4096 / compensationCoefficient;                                       // convert analog value to voltage value
    float temp = (133.42 * pow(analogValue, 3) - 255.86 * pow(analogValue, 2) + 857.39 * analogValue) * 0.5; // convert voltage value to tds value
#ifdef DEBUG
    Serial.print("tds: ");
    Serial.println(temp, 2);
#endif
    return temp;
}

// read water temperature and return the value
float readDs18()
{
    DS18.requestTemperatures();
    delay(10);
    float tempValue = DS18.getTempCByIndex(0);
#ifdef DEBUG
    Serial.print("water temp: ");
    Serial.println(tempValue, 2);
#endif
    return tempValue;
}

// read ph value and return the value
float readPh(float temperature)
{
    float voltage, phValue;
    // voltage = analogRead(PH_PIN) / 4096 * VREF * 1000;
    voltage = analogRead(PH_PIN) * VREF / 4095.0;
    // phValue = ph.readPH(voltage, temperature);
    phValue = 3.3 * voltage;
#ifdef DEBUG
    Serial.print("ph: ");
    Serial.println(phValue, 2);
#endif
    ph.calibration(voltage, temperature);
    return phValue;
}

// start the feeding process
void feed()
{
    if (isFeeding)
    {
        Serial.println("Feeding");
        myservo.write(0);
        myservo2.write(50);
        weightValue = scale.get_units(3);
        if (lastWeight - weightValue > SET_WEIGHT)
        {
            Serial.println("Feed Done");
            myservo.write(90);
            myservo2.write(5);
            isFeeding = false;
            lastWeight = weightValue;
        }
    }
    else
    {
        myservo.write(90);
        myservo2.write(5);
    }
}

// control the motor
void moveMotor(int direction, int speed)
{
    switch (direction)
    {
    case 0x00:
        digitalWrite(LEFT_RPWM, LOW);
        digitalWrite(LEFT_LPWM, LOW);
        digitalWrite(RIGHT_RPWM, LOW);
        digitalWrite(RIGHT_LPWM, LOW);
        break;
    case 0x01:
        digitalWrite(LEFT_RPWM, speed);
        digitalWrite(LEFT_LPWM, LOW);
        digitalWrite(RIGHT_RPWM, speed);
        digitalWrite(RIGHT_LPWM, LOW);
        break;
    case 0x02:
        digitalWrite(LEFT_RPWM, LOW);
        digitalWrite(LEFT_LPWM, speed);
        digitalWrite(RIGHT_RPWM, LOW);
        digitalWrite(RIGHT_LPWM, speed);
        break;
    case 0x04:
        digitalWrite(LEFT_RPWM, speed);
        digitalWrite(LEFT_LPWM, LOW);
        digitalWrite(RIGHT_RPWM, LOW);
        digitalWrite(RIGHT_LPWM, speed);
        break;
    case 0x03:
        digitalWrite(LEFT_RPWM, LOW);
        digitalWrite(LEFT_LPWM, speed);
        digitalWrite(RIGHT_RPWM, speed);
        digitalWrite(RIGHT_LPWM, LOW);
        break;
    default:
        digitalWrite(LEFT_RPWM, LOW);
        digitalWrite(LEFT_LPWM, LOW);
        digitalWrite(RIGHT_RPWM, LOW);
        digitalWrite(RIGHT_LPWM, LOW);
        break;
    }
}

//  time sync notification cb
void time_sync_notification_cb(struct timeval *tv)
{
    Serial.println("Synchorized time");
    getLocalTime(&timeinfo);
    Serial.print("Current time: ");
    Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
}

// socket event handler
void eventSocketHandler(const char *payload, size_t length)
{
    Serial.println(payload);
    Serial.print("got event: ");
    String temp = String(payload);

    StaticJsonDocument<192> doc;
    DeserializationError error = deserializeJson(doc, temp);

    if (error)
    {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.c_str());
        return;
    }

    // handle payload
    // TODO:handle payload
    if (temp.indexOf("control"))
    {
        // handle control event
        Serial.println("control event");

        int direction = doc["direction"];
        int speed = 100;
        int pump = doc["pump"];
        isFeeding = doc["isFeeding"];
        SET_WEIGHT = doc["weight"];
        setHour = doc["hour"];
        setMinute = doc["minute"];
        if (direction == 0)
        {
            speed = 0;
            moveMotor(0, 0);
        }
        else
        {
            moveMotor(direction, speed);
        }
        digitalWrite(PUMP_RELAY, !pump);
    }
    else if (temp.indexOf("other"))
    {
        // handle other event
        Serial.println("other event");
    }
}

// format time
String formatTime(int hour, int minute)
{
    String temp = "";
    if (hour < 10)
    {
        temp += "0" + String(hour);
    }
    else
    {
        temp += String(hour);
    }
    temp += ":";
    if (minute < 10)
    {
        temp += "0" + String(minute);
    }
    else
    {
        temp += String(minute);
    }
    return temp;
}

// send Data to server
void sendData()
{
    DynamicJsonDocument doc(1024);
    // JsonArray array = doc.to<JsonArray>();
    // array.add("/esp/measure");
    // JsonObject param1 = array.createNestedObject();
    Serial.println("sending data");

    doc["clientID"] = "esp";
    doc["tds"] = String(tdsValue);
    doc["ph"] = String(phValue);
    doc["temp"] = String(tempValue);
    doc["hum"] = String(humValue);
    doc["weight"] = String(weightValue);
    doc["waterTemp"] = String(waterTemp);
    doc["time"] = formatTime(timeinfo.tm_hour, timeinfo.tm_min);
    doc["isFeeding"] = String(isFeeding);

    String output;
    serializeJson(doc, output);
    webSocket.emit("/esp/measure", output.c_str());
    // Serial.println(output);
    delay(20);
}

// check time with setTime
void checkTime(int hour, int minute)
{
    getLocalTime(&timeinfo);
    if (timeinfo.tm_hour == hour && timeinfo.tm_min == minute)
    {
        isFeeding = true;
    }
}

void setup()
{
    // init serial
    Serial.begin(115200);
    // init ph sensor
    ph.begin();
    // init tds sensor
    pinMode(TDS_PIN, INPUT);
    // init temperature sensor
    DS18.begin();
    // init load cell
    scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
    scale.set_scale(2280.f); // need Calibrate
    scale.tare();

    lastWeight = scale.get_units(3); // get the last weight
    // init humidity and temperature sensor
    // myWire.begin(SHT31_SDA, SHT31_SCL); //if change pin to other than 21,22 then uncomment this line and those above definitions
    if (!sht31.begin(SHT31_ADDR))
    {
        Serial.println("Couldn't find SHT31");
    }
    else
    {
        Serial.println("SHT31 found");
        isSht31On = true;
    }
    // init servo
    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
    ESP32PWM::allocateTimer(2);
    ESP32PWM::allocateTimer(3);
    myservo.attach(SERVO_PIN, 1000, 2000);
    myservo2.attach(SERVO_PIN2, 1000, 2000);

    myservo.write(90);
    myservo2.write(5);

    pinMode(LEFT_RPWM, OUTPUT);
    pinMode(LEFT_LPWM, OUTPUT);
    pinMode(RIGHT_RPWM, OUTPUT);
    pinMode(RIGHT_LPWM, OUTPUT);
    pinMode(PUMP_RELAY, OUTPUT);
    digitalWrite(PUMP_RELAY, HIGH);
    digitalWrite(LEFT_RPWM, LOW); // because the gpio5 is output pwm at boot, so we need to reset

    // config time for realtime check
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2);

    // setup wifi
    WiFiManager wifiManager;
    wifiManager.autoConnect("EPS32 Be thong minh");

    // Connect to WIFI
    // WiFiMulti.addAP(ssid, pass);
    // while (WiFiMulti.run() != WL_CONNECTED)
    // {
    //   delay(100);
    // }
    // Serial.println("Connected to wifi");
    // Serial.println("IP address: ");
    // Serial.println(WiFi.localIP());

    // setup socket
    // TODO:handle event
    webSocket.on("/esp/control", eventSocketHandler);
    webSocket.on("/esp/other", eventSocketHandler);
    webSocket.on("message", eventSocketHandler);
    webSocket.begin(HOST);
}

void loop()
{
    webSocket.loop();
    if (millis() - sendDataPreviousMillis > 1000)
    {
        sendDataPreviousMillis = millis();
        sendData();
    }

    if (millis() - readSensorPreviousMillis > 1000)
    {
        readSensorPreviousMillis = millis();
        tdsValue = readTds(tempValue);
        phValue = readPh(tempValue);
        waterTemp = readDs18();
        if (isSht31On)
        {
            tempValue = sht31.readTemperature();
            humValue = sht31.readHumidity();
        }
        waterTemp = readDs18();
        feed();
    }

    if (millis() - previousMillis > 60000)
    {
        checkTime(setHour, setMinute);
        previousMillis = millis();
    }
}
