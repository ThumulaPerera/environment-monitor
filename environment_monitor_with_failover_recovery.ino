#include <Statistic.h>
#include <dht.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFiMulti.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#define DHT11_PIN 2
#define DELAY_BETWEEN_TRANSMISSIONS 900000
#define DELAY_BETWEEN_READINGS 20000

#define NO_OF_MESSAGE_DATA 10
#define NO_OF_SENSORS 2

const char* host = "192.168.1.3";
const int  port = 5000;
const int   watchdog = 5000;
unsigned long previousMillis = millis(); 

const char* ssid     = "";      // SSID
const char* password = "";      // Password

const long utcOffsetInSeconds = 19800;
const char utcOffsetInHours[10] = "+05:30";

String sensors[NO_OF_SENSORS] = {
    "temperatureSensor",
    "humiditySensor"
  };

int nextPositionToWrite = 0;

String messageData[NO_OF_MESSAGE_DATA][1 + NO_OF_SENSORS * 3];

String alertXML = "";

unsigned long lastSentTime;

float currentTemperature;
float currentHumidity;
float isErrorDHT;

Statistic temperatureStats;
Statistic humidityStats;

ESP8266WiFiMulti wifiMulti;
HTTPClient http;

dht DHT;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);

void setup()
{
  Serial.begin(115200);
  Serial.println("starting monitoring....");

  setupWiFi();

  timeClient.begin();

  temperatureStats.clear();
  humidityStats.clear();

  for (int i = 0; i < NO_OF_MESSAGE_DATA; i++) {
    messageData[i][0] = "";  
  }
  
  lastSentTime = millis();
}


void loop()
{
  Serial.println("running.....");
  
  readTemperatureAndHumidity();
  if (not isErrorDHT)
  {
    temperatureStats.add(currentTemperature);
    humidityStats.add(currentHumidity);
  }
  
  if (isTimeElapsed(DELAY_BETWEEN_TRANSMISSIONS))
  {
    lastSentTime = millis();
    
    // should be done here. getTime() also needs wifi occationally
    connectWiFi();

    setMessageData();
    printCurrentData();

    if(isConnectedToWifi()){
      
      for (int i = 0; i < NO_OF_MESSAGE_DATA; i++){
        int readingPosition = (nextPositionToWrite + 1 + i) % NO_OF_MESSAGE_DATA;
        if (messageData[readingPosition][0] != "") {
          sendAlert(readingPosition);
        }
        
      }
      disconnectWiFi();
      
    } else {
      Serial.println("not connected to WiFi");
    }
    
    nextPositionToWrite = (nextPositionToWrite + 1) % NO_OF_MESSAGE_DATA;

    temperatureStats.clear();
    humidityStats.clear();
  } 

  delay(DELAY_BETWEEN_READINGS);
}

void setMessageData(){
  messageData[nextPositionToWrite][0] = getTime();
  messageData[nextPositionToWrite][1] = temperatureStats.average();
  messageData[nextPositionToWrite][2] = temperatureStats.unbiased_stdev();
  messageData[nextPositionToWrite][3] = temperatureStats.count();
  messageData[nextPositionToWrite][4] = humidityStats.average();
  messageData[nextPositionToWrite][5] = humidityStats.unbiased_stdev();
  messageData[nextPositionToWrite][6] = humidityStats.count();  
}

void printCurrentData(){
  Serial.println("15 minutes passed");
  Serial.println();
  Serial.print("Avg temperature : ");
  Serial.println(messageData[nextPositionToWrite][1]);
  Serial.print("Stdev temperature : ");
  Serial.println(messageData[nextPositionToWrite][2]);
  Serial.print("no. of temperature readings : ");
  Serial.println(messageData[nextPositionToWrite][3]);  
  Serial.println();
  Serial.print("Avg humidity : ");
  Serial.println(messageData[nextPositionToWrite][4]);
  Serial.print("Stdev humidity : ");
  Serial.println(messageData[nextPositionToWrite][5]);
  Serial.print("no. of humidity readings : ");
  Serial.println(messageData[nextPositionToWrite][6]);
  Serial.println();
}

void sendAlert(int readingPosition){
  generateAlertXML(readingPosition);
  Serial.print("position is : ");
  Serial.println(readingPosition);
  Serial.println("alert is:");
  Serial.println(alertXML);
  bool sendSuccess = sendHTTPRequest();
  if (sendSuccess) {
    messageData[readingPosition][0] = "";
  }
  resetAlertXML(); 
}


//....................DHT...................................//

void readTemperatureAndHumidity()
{
  int DHTStatus = DHT.read11(DHT11_PIN);
  checkErrorDHT(DHTStatus);
  currentTemperature = DHT.temperature;
  currentHumidity = DHT.humidity;
}

void checkErrorDHT(int DHTStatus)
{
  isErrorDHT = false;
  switch (DHTStatus)
  {
    case DHTLIB_OK:  
      break;
    case DHTLIB_ERROR_CHECKSUM: 
      isErrorDHT = true;
      Serial.print("DHT Checksum error,\t"); 
      break;
    case DHTLIB_ERROR_TIMEOUT: 
      isErrorDHT = true;
      Serial.print("DHT Time out error,\t"); 
      break;
    default:
      isErrorDHT = true; 
      Serial.print("DHT Unknown error,\t"); 
      break;
  }
}

//............ WiFi .................//

void setupWiFi()
{
  // Set WiFi to station mode
  WiFi.mode(WIFI_STA);
  
  // Connecting to a WiFi network/s
  wifiMulti.addAP(ssid, password);
}

void connectWiFi()
{
  int maxNoOfRetries = 5;
  int delayBetweenAttemptsMs = 1000;

  for (int i = 0; i < maxNoOfRetries; i++){
    if(wifiMulti.run() != WL_CONNECTED){
      Serial.println("trying to connect to wifi!!");
      delay(delayBetweenAttemptsMs);
    } else {
      Serial.println("WiFi connected");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      return;
    }
  }
}

boolean isConnectedToWifi(){
  return WiFi.status() == WL_CONNECTED;
}

void disconnectWiFi()
{
  WiFi.disconnect();  
}



//............ HTTP .................//

bool sendHTTPRequest()
{
  bool success = false;
  int maxNoOfRetries = 5;
  int delayBetweenRetries = 1000; 
  String url = "/";
   
  Serial.print("connecting to ");
  Serial.println(host);
  Serial.print("Requesting URL: ");
  Serial.println(url);
 
  for (int i = 0; i < maxNoOfRetries; i++){
    http.begin(host,port,url);
    http.addHeader("Content-Type", "Application/xml");
    int httpCode = http.POST(alertXML);
    if (httpCode > 0) {
      if (httpCode == 200) {
        String payload = http.getString();
        Serial.println(payload);
        success = true;
        break;
      } else {
        Serial.print("HTTP Error : ");
        Serial.println(httpCode);
      }
    } else {
      Serial.printf("[HTTP] POST... failed, error: %s\n", http.errorToString(httpCode).c_str());  
    }
    http.end();
    delay(delayBetweenRetries);
  }
  Serial.println("closing connection");
  return success;
}



// ................. XML ........................ //

void generateAlertXML(int messageIndex)
{
  appendToAlert("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
  appendToAlert("<alert xmlns=\"urn:oasis:names:tc:emergency:cap:1.2\">");
  appendToAlert("<identifier>");
  appendToAlert("MESSAGE-");
  appendToAlert(messageData[messageIndex][0]);
  appendToAlert("</identifier>");
  appendToAlert("<sender>NODEMCU</sender>");
  appendToAlert("<sent>");
  appendToAlert(messageData[messageIndex][0]);
  appendToAlert("</sent>");
  appendToAlert("<status>Actual</status>");
  appendToAlert("<msgType>Alert</msgType>");
  appendToAlert("<scope>Private</scope>");
  appendToAlert("<addresses></addresses>");

  for (int sensorNo = 0; sensorNo < NO_OF_SENSORS; sensorNo++) {
      appendSensorDataXML(
        sensors[sensorNo], 
        messageData[messageIndex][(3 * sensorNo) + 1], 
        messageData[messageIndex][(3 * sensorNo) + 2], 
        messageData[messageIndex][(3 * sensorNo) + 3]
       );
  }

  appendToAlert("</alert>");
}

void appendSensorDataXML(String sensorName, String average, String standardDeviation, String numberOfReadings)
{
  appendToAlert("<info>");
  appendToAlert("<category>Met</category>");
  appendToAlert("<event>UPDATE</event>");
  appendToAlert("<urgency>Unknown</urgency>");
  appendToAlert("<severity>Minor</severity>");
  appendToAlert("<certainty>Observed</certainty>");
  appendToAlert("<senderName>");
  appendToAlert(sensorName);
  appendToAlert("</senderName>");
  appendParameterXML("average", String(average));
  appendParameterXML("standardDeviation", String(standardDeviation));
  appendParameterXML("numberOfReadings", String(numberOfReadings));
  appendToAlert("</info>");
}

void appendParameterXML(String valueName, String value)
{
  appendToAlert("<parameter>");
  appendToAlert("<valueName>");
  appendToAlert(valueName);
  appendToAlert("</valueName>");
  appendToAlert("<value>");
  appendToAlert(value);
  appendToAlert("</value>");
  appendToAlert("</parameter>");
}

void appendToAlert (String stringToAppend)
{
  alertXML.concat(stringToAppend);
}

void resetAlertXML()
{
  alertXML = "";  
}


//..............NTPClient...........................//

String getTime() 
{
    String formattedTime = "";    

    timeClient.update();

    unsigned long epochTime = timeClient.getEpochTime();

    struct tm *ptm = gmtime ((time_t *)&epochTime);

    int monthDay = ptm->tm_mday;
    int currentMonth = ptm->tm_mon+1;
    int currentYear = ptm->tm_year+1900;

    char formattedDate[12];

    sprintf(
      formattedDate,
      "%04d-%02d-%02d",
      currentYear,
      currentMonth,
      monthDay
    );

    formattedTime.concat(formattedDate);
    formattedTime.concat("T");
    formattedTime.concat(timeClient.getFormattedTime());
    formattedTime.concat(utcOffsetInHours);  

    return formattedTime;
}



//.............Helpers............................//

boolean isTimeElapsed(unsigned long timeDifference)
{
  return (millis() - lastSentTime) > timeDifference;  
}
