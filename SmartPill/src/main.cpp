#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <Stepper.h>
#include <DHT.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "secrets.h"
#include <WiFiClientSecure.h>
#include <MQTTClient.h>
#include <ArduinoJson.h>
#include "WiFi.h"
#include "String.h"


#define AWS_IOT_PUBLISH_TOPIC "esp32/pub"
#define AWS_IOT_SUBSCRIBE_TOPIC "esp32/sub"
#define DHTPIN 13     // what pin we're connected to
#define DHTTYPE DHT22   // DHT 22 
#define BTN 32

// Invoke DHT22
DHT dht(DHTPIN, DHTTYPE); //// Initialize DHT sensor for normal 16mhz Arduino
// Invoke ST7735
TFT_eSPI tft = TFT_eSPI(); 
// invoke STEPPER
Stepper myStepper(2048, 26, 14, 27, 12); 

//Variables
int chk;
float hum;  //Stores humidity value
float temp; //Stores temperature value
int stepFlag = 0;
int stepNr = 0;
int data1=600,data2=3,data3=60;
int timeList[16] = {0};
int pillCnt = 0;
int toneFlag = 0;

struct raw
{
  String inputMessage;
  String inputParam;
} raw_strings;

const char *ntpServer = "0.dk.pool.ntp.org";
const long gmtOffset_sec = 3600;
const int daylightOffset_sec = 3600;


WiFiClientSecure net = WiFiClientSecure();
MQTTClient client = MQTTClient(256);
void messageHandler(String &topic, String &payload);

AsyncWebServer server(80);

// REPLACE WITH YOUR NETWORK CREDENTIALS
const char *ssid = "WiFimodem-6806";
const char *password = "jjmknzn4at";

const char *PARAM_INPUT_1 = "Time";
const char *PARAM_INPUT_2 = "Amount";
const char *PARAM_INPUT_3 = "Interval";

// HTML web page to handle 3 input fields (input1, input2, input3)
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head>
  <title>ESP Input Form</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  </head><body>
  <form action="/get">
    Time: <input type="time" name="Time" required>
    <input type="submit" value="Submit">
  </form><br>
  <form action="/get">
    Amount: <input type="number" name="Amount">
    <input type="submit" value="Submit">
  </form><br>
  <form action="/get">
    Interval: <input type="time" name="Interval">
    <input type="submit" value="Submit">
  </form>
</body></html>)rawliteral";

void notFound(AsyncWebServerRequest *request);
void connectAWS();
void publishMessage(String value, String type);
void printLocalTime(int *Hour, int *Minute, String *Weekday);
void save_raw_string(String inputMessage, String inputParam);
void calc_time_diff(String inputParam, String inputMessage, int hour, int minute, int *diff_hour, int *diff_minute);

void IRAM_ATTR toggleBTN()
{
  //digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  Serial.println("Interrupt Button");
  stepFlag = 1;
}

int toMin(String time){
  String Hours, Min;
  char buf[16]={0};

  time.toCharArray(buf, 16);

  char *token = strtok(buf, ":");
  Hours = token;
  token = strtok(NULL, ":");
  Min = token;

  return((Hours.toInt()*60)+Min.toInt());
}

void setup()
{
  Serial.begin(115200);
  //Interrupt
  pinMode(32, INPUT);
  attachInterrupt(32, toggleBTN, RISING);
  
  //stepper
  myStepper.setSpeed(5);
  //myStepper.step(1414);
  
  //DHT
  dht.begin();
  hum = dht.readHumidity();
  temp = dht.readTemperature();

  //ST7735
  tft.init();
  tft.setRotation(3);

  //CONNECT
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("REMEBER TO ENTER",1,10,2);
  tft.drawString("INFORMATION ON SERVER",1,26,2);
	
  //webserver and MQTT
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print('.');
    delay(1000);
  }
  Serial.println();
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  delay(1000);
  tft.fillScreen(TFT_BLACK);

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // Send web page with input fields to client
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send_P(200, "text/html", index_html); });

  //default:
  data1 = 1440;

  // Send a GET request to <ESP_IP>/get?input1=<inputMessage>
  server.on("/get", HTTP_GET, [](AsyncWebServerRequest *request)
            {
                String inputMessage;
                String inputParam;
                // GET input1 value on <ESP_IP>/get?input1=<inputMessage>
                if (request->hasParam(PARAM_INPUT_1))
                {
                  inputMessage = request->getParam(PARAM_INPUT_1)->value();
                  data1 = toMin(inputMessage);
                  inputParam = PARAM_INPUT_1;
                }
                // GET input2 value on <ESP_IP>/get?input2=<inputMessage>
                else if (request->hasParam(PARAM_INPUT_2))
                {
                  inputMessage = request->getParam(PARAM_INPUT_2)->value();
                  data2 = inputMessage.toInt();
                  inputParam = PARAM_INPUT_2;
                }
                // GET input3 value on <ESP_IP>/get?input3=<inputMessage>
                else if (request->hasParam(PARAM_INPUT_3))
                {
                  inputMessage = request->getParam(PARAM_INPUT_3)->value();
                  data3 = toMin(inputMessage);
                  inputParam = PARAM_INPUT_3;
                }
                else
                {
                  inputMessage = "No message sent";
                  inputParam = "none";
                }
                //data1 = time (Starttidspunkt af piller)
                //data2 = amount
                //data3 = interval
                //publishMessage(inputMessage, inputParam);
                for(int i=0; i<data2; i++){
                  timeList[i] = data1+(i*data3);
                  Serial.println(timeList[i]);
                }
                tft.fillScreen(TFT_BLACK);

                //Psuedo
                /*
                Change data3 in time variable
                Change into minutes
                add to timeList
                diff = timeList[pillcount] - realTime   
                change into hours and mins.

                I forhold til negativ eller posetiv giv advarsel!

                DISPLAY:
                Pille tid tilbage
                Piller tilbage i alt
                TID
                */

                request->send(200, "text/html", "HTTP GET request sent to your ESP on input field (" + inputParam + ") with value: " + inputMessage + "<br><a href=\"/\">Return to Home Page</a>"); });
  server.onNotFound(notFound);
  server.begin();
  }

  void loop()
  {
    if (stepFlag == 1){
		detachInterrupt(32);
		stepFlag = 0;
    Serial.println("Running Motor...");
		myStepper.step(1414);
		stepNr++;
    pillCnt++;
		attachInterrupt(32, toggleBTN, RISING);
	}
	
	int hour, minute;
  int timeDiffHour, timeDiffMin;
  String weekday;
  printLocalTime(&hour, &minute, &weekday);
  //Serial.println("Hour: " + String(hour) + " Minute: " + String(minute) + " Weekday: " + weekday);

  if (pillCnt==data2){
    if (hour == 0)
    {pillCnt = 0;}
  } else {
    timeDiffHour = (timeList[pillCnt]/60) - hour; 
    timeDiffMin = (timeList[pillCnt]%60) - minute;
    Serial.print(timeDiffHour);
    Serial.print(":");
    Serial.println(timeDiffMin);
  }
  

  //DISPLAY
  char displayBuf[256];
  char displaytemp[256];
  if (timeDiffHour > 0 || timeDiffMin > 0){
    sprintf(displayBuf, "%dH:%dM     ", abs(timeDiffHour), abs(timeDiffMin));
    tft.setTextColor(TFT_WHITE, TFT_GREEN);
    tft.drawString("Time left: ",5,5,4);
    tft.drawString(displayBuf,5,31,4);
    toneFlag=0;
  } else {
    sprintf(displayBuf, "%dH:%dM     ", abs(timeDiffHour), abs(timeDiffMin));
    tft.setTextColor(TFT_WHITE, TFT_BLUE);
    tft.drawString("Take pill: ",5,5,4);
    tft.drawString(displayBuf,5,31,4);
    if(toneFlag==0){
      tone(21,1000,10000);
      toneFlag=1;
    }
  }
  hum = dht.readHumidity();
  temp = dht.readTemperature();
  Serial.println(temp);
  Serial.println(hum);
  sprintf(displayBuf, "Temp: %0.1f - hum: %0.1f", temp, hum);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(displayBuf,5,81,2);


  delay(1000);
  }

  void connectAWS()
  {
    // Configure WiFiClientSecure to use the AWS IoT device credentials
    net.setCACert(AWS_CERT_CA);
    net.setCertificate(AWS_CERT_CRT);
    net.setPrivateKey(AWS_CERT_PRIVATE);

    // Connect to the MQTT broker on the AWS endpoint we defined earlier
    client.begin(AWS_IOT_ENDPOINT, 8883, net);

    // // Create a message handler
    // client.onMessage(messageHandler);

    Serial.print("Connecting to AWS IOT\n");

    while (!client.connect(THINGNAME))
    {
      Serial.print(".");
      delay(100);
    }

    if (!client.connected())
    {
      Serial.println("AWS IoT Timeout!");
      return;
    }

    // Subscribe to a topic
    // client.subscribe(AWS_IOT_SUBSCRIBE_TOPIC);

    Serial.println("AWS IoT Connected!");
  }

  void publishMessage(String value, String type)
  {
    connectAWS();
    StaticJsonDocument<200> doc;
    doc["device_id"] = "esp32";
    doc["type"] = type;
    doc["value"] = value;
    char jsonBuffer[512];
    serializeJson(doc, jsonBuffer); // print to client

    client.publish(AWS_IOT_PUBLISH_TOPIC, jsonBuffer);
  }

  void printLocalTime(int *Hour, int *Minute, String *Weekday)
  {
    struct tm timeinfo;

    if (!getLocalTime(&timeinfo))
    {
      Serial.println("Failed to obtain time");
      return;
    }
    char timeHour[3];
    strftime(timeHour, 3, "%H", &timeinfo);
    *Hour = atoi(timeHour);

    char timeMinute[3];
    strftime(timeMinute, 3, "%M", &timeinfo);
    *Minute = atoi(timeMinute);

    char timeWeekDay[10];
    strftime(timeWeekDay, 10, "%A", &timeinfo);
    *Weekday = (String)timeWeekDay;
  }

  void save_raw_string(String inputMessage, String inputParam)
  {
    Serial.println(inputMessage);
    Serial.println(inputParam);
    // raw *raw_strings, tmp;
    // raw_strings = &tmp;

    raw_strings.inputMessage = inputMessage;
    raw_strings.inputParam = inputParam;
  }

  void notFound(AsyncWebServerRequest * request)
  {
    request->send(404, "text/plain", "Not found");
  }

  void calc_time_diff(String inputParam, String inputMessage, int current_hour, int current_minute, int *diff_hour, int *diff_minute)
  {
    int n = inputMessage.length();
    char char_array[n + 1];
    char set_hour_arr[3];
    char set_minute_arr[3];

    strcpy(char_array, inputMessage.c_str());
    // for (int i = 0; i < n; i++)
    // {
    //   Serial.print(char_array[i]);
    // }
    if (inputParam == "Time")
    {
      // char *token = strtok(char_array, ":");
      for (int i = 0; i <= 1; i++)
      {
        set_hour_arr[i] = char_array[i];
        set_minute_arr[i] = char_array[i+3];
      }
    }
    Serial.println("Set time");
    Serial.println(atoi(set_hour_arr));
    Serial.println(atoi(set_minute_arr));
    Serial.println("\n");
    Serial.println("Current time");
    Serial.println(current_hour);
    Serial.println(current_minute);
    Serial.println("\n");

    int current_time = current_hour * 60 + current_minute;
    int set_time = atoi(set_hour_arr) * 60 + atoi(set_minute_arr);

    int diff = set_time - current_time;

    *diff_hour = diff / 60;
    *diff_minute = diff % 60;

    }

  /*
  int n = inputMessage.length();
  Serial.println(inputMessage);
  Serial.println(n);
  char char_array[n + 1];
  char hour_arr[3];
  char minute_arr[3];

  strcpy(char_array, inputMessage.c_str());
  for (int y = 0; y < n; y++)
  {
    Serial.println(char_array[y]);
  }

  if (inputParam == "Time")
  {
    // char *token = strtok(char_array, ":");
    for (int i = 0; i <= 1; i++)
    {
      hour_arr[i] = char_array[i];
    }
    for (int x = 0; x <= 1; x++)
    {
      minute_arr[x] = char_array[x];
    }
    Serial.println(hour_arr);
    Serial.println(minute_arr);

    server.onNotFound(notFound);
    server.begin();

    EEPROM.begin(EEPROM_SIZE);
    for (int i = 0; i <= 24; i++)
    {
      if (EEPROM.read(0) == i)
      {
        EEPROM.write(0, 0);
        EEPROM.commit();
      }
      else
      {
        EEPROM.write(0, 0);
        EEPROM.commit();
      }
    }*/