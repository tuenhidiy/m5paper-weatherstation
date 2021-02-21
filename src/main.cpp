#include <M5EPD.h>
#include "WiFiInfo.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiClient.h>
#define LGFX_M5PAPER
#include <LovyanGFX.hpp>
#include "misc.h"
#include "THPIcons.c"
#include "WeatherIcons.c"
#include "WindIcons.c"

// Change to your timezone
#define timezone  7

// Set location and API key
String Location = "Vietnam,VN"; // Change to your location
String API_Key  = "xxxxxxxxxxxxx";

int wifi_signal;
uint8_t global_time_synced = false;

// SHT30 temperature and humidity parameters
float tem = 0.0;
float hum = 0.0;
// Openweathermap parameters
// int temp;
// int humidity;
uint_fast8_t visibility = 0;
uint_fast8_t pressure = 0;
float wind_speed = 0.0;
float wind_degree = 0.0;

const char* weatherId="";
const char* weatherMain="";
const char* weatherDesc="";
const char* weatherIcon="";

const char* upweatherId="";
const char* upweatherMain="";

//...I also had an error with the M5 object being overloaded between M5EPD and M5Stack.  I had to rename the epd version to M5e (did not include in push)

//... NOTE.  Instead of a pointer into the json response keep a global variable to cache between weather api calls. (it will make it easier to rotate between locations eventually)
String sWeatherDesc;
String sWeatherIcon;

time_t upsunrise, sunrise;
time_t upsunset, sunset;

constexpr float FONT_SIZE_LARGE = 1.8;
constexpr float FONT_SIZE_MEDIUM = 1.0;
constexpr float FONT_SIZE_SMALL = 0.7;
constexpr uint_fast16_t M5PAPER_WIDTH = 960;
constexpr uint_fast16_t M5PAPER_HEIGHT = 540;

rtc_time_t time_ntp;
rtc_date_t date_ntp{4, 1, 1, 1970};

SemaphoreHandle_t xMutex = nullptr;

// Icon Sprites
LGFX gfx;
LGFX_Sprite THPIcons(&gfx);
LGFX_Sprite WeatherIcons(&gfx);
LGFX_Sprite WIcons(&gfx);
LGFX_Sprite SRSSIcons(&gfx);

//... make the counter global so it doesn't get recent on each execution
static uint32_t cnt = 0;


//#########################################################################################
inline int syncNTPTimeVN(void)
{
  constexpr auto NTP_SERVER1 = "2.vn.pool.ntp.org";
  constexpr auto NTP_SERVER2 = "1.asia.pool.ntp.org";
  constexpr auto NTP_SERVER3 = "3.asia.pool.ntp.org";
  constexpr auto TIME_ZONE = "ICT-7";


  auto datetime_setter = [](const tm &datetime) {
    rtc_time_t time{
        static_cast<int8_t>(datetime.tm_hour),
        static_cast<int8_t>(datetime.tm_min),
        static_cast<int8_t>(datetime.tm_sec)};
    rtc_date_t date{
        static_cast<int8_t>(datetime.tm_wday),
        static_cast<int8_t>(datetime.tm_mon + 1),
        static_cast<int8_t>(datetime.tm_mday),
        static_cast<int16_t>(datetime.tm_year + 1900)};
    M5.RTC.setDate(&date);
    M5.RTC.setTime(&time);
    date_ntp = date;
    time_ntp = time;
  };

  return syncNTPTime(datetime_setter, TIME_ZONE, NTP_SERVER1, NTP_SERVER2, NTP_SERVER3);
}
//#########################################################################################
void handleBtnPPress(void)
{
  xSemaphoreTake(xMutex, portMAX_DELAY);
  prettyEpdRefresh(gfx);
  gfx.setTextSize(FONT_SIZE_SMALL);

  gfx.startWrite();
  gfx.setCursor(30, 30);
  if (!syncNTPTimeVN())
  {
    gfx.println("Succeeded to sync time");
    struct tm timeInfo;
    if (getLocalTime(&timeInfo))
    {
      gfx.setCursor(30, 80);
      gfx.print("getLocalTime:");
      gfx.println(&timeInfo, "%Y/%m/%d %H:%M:%S");
    }
  }
  else
  {
    gfx.println("Failed to sync time");
  }

  rtc_date_t date;
  rtc_time_t time;

  // Get RTC
  M5.RTC.getTime(&time);
  M5.RTC.getDate(&date);
  gfx.setCursor(30, 130);
  gfx.print("RTC               :");
  gfx.printf("%04d/%02d/%02d ", date.year, date.mon, date.day);
  gfx.printf("%02d:%02d:%02d", time.hour, time.min, time.sec);
  gfx.endWrite();

  delay(1000);

  gfx.setTextSize(FONT_SIZE_LARGE);
  xSemaphoreGive(xMutex);
}
//#########################################################################################
inline void handleBtnRPress(void)
{
  xSemaphoreTake(xMutex, portMAX_DELAY);
  prettyEpdRefresh(gfx);
  xSemaphoreGive(xMutex);
}
//#########################################################################################
void handleBtnLPress(void)
{
  xSemaphoreTake(xMutex, portMAX_DELAY);
  prettyEpdRefresh(gfx);
  gfx.setCursor(50, 50);
  gfx.setTextSize(FONT_SIZE_SMALL);
  gfx.print("Goodbye...");
  gfx.waitDisplay();
  M5.disableEPDPower();
  M5.disableEXTPower();
  M5.disableMainPower();
  esp_deep_sleep_start();
  while (true)
    ;
  xSemaphoreGive(xMutex);
}
//#########################################################################################
void handleButton(void *pvParameters)
{
  while (true)
  {
    delay(500);
    M5.update();
    if (M5.BtnP.isPressed())
    {
      handleBtnPPress();
    }
    else if (M5.BtnR.isPressed())
    {
      handleBtnRPress();
    }
    else if (M5.BtnL.isPressed())
    {
      handleBtnLPress();
    }
  }
}
//#########################################################################################
void setup(void)
{
  constexpr uint_fast16_t WIFI_CONNECT_RETRY_MAX = 60; // 10 = 5s
  constexpr uint_fast16_t WAIT_ON_FAILURE = 2000;
  M5.begin(false, true, true, true, true);
  M5.SHT30.Begin();
  M5.RTC.begin();

  WiFi.begin(WiFiInfo::SSID, WiFiInfo::PASS);

  gfx.init();
  gfx.setEpdMode(epd_mode_t::epd_fast);
  gfx.setRotation(1);
  gfx.setFont(&fonts::lgfxJapanMinchoP_40);
  gfx.setTextSize(FONT_SIZE_SMALL);

  gfx.print("Connecting to Wi-Fi network");
  for (int cnt_retry = 0;
       cnt_retry < WIFI_CONNECT_RETRY_MAX && !WiFi.isConnected();
       cnt_retry++)
  {
    delay(500);
    gfx.print(".");
  }
  gfx.println("");
  if (WiFi.isConnected())
  {
    wifi_signal = WiFi.RSSI(); // Get Wifi Signal strength now, because the WiFi will be turned off to save power!
    gfx.print("Local IP: ");
    gfx.println(WiFi.localIP());
  }
  else
  {
    gfx.println("Failed to connect to a Wi-Fi network");
    printf ( "failed to connect wifi\n" );
    delay(WAIT_ON_FAILURE);

  }

  xMutex = xSemaphoreCreateMutex();
  if (xMutex != nullptr)
  {
    xSemaphoreGive(xMutex);
    xTaskCreatePinnedToCore(handleButton, "handleButton", 4096, nullptr, 1, nullptr, 1);
  }
  else
  {
    gfx.println("Failed to create a task for buttons");
  }
  gfx.println("Init done");
  printf ( "Init done\n" );

  delay(1000);
  gfx.setTextSize(FONT_SIZE_LARGE);
  prettyEpdRefresh(gfx);
  gfx.setCursor(0, 0);
  
}
//#########################################################################################
void MakehttpRequest()
{
  HTTPClient http;  // Declare an object of class HTTPClient
 
  // specify request destination
  http.begin("http://api.openweathermap.org/data/2.5/weather?q=" + Location + "&APPID=" + API_Key);  // !!
 
  int httpCode = http.GET();  // Sending the request
 
  if (httpCode > 0)  // Checking the returning code
    {
      String payload = http.getString();   // Getting the request response payload
 
      printf ( payload.c_str() );
      printf ( "\n" );
      DynamicJsonBuffer jsonBuffer(512);
 
      // Parse JSON object
      JsonObject& root = jsonBuffer.parseObject(payload);
      if (!root.success())
      {
        printf ( "cannot parse object" );
        // gfx.print("N/A"); // not initialized
        upweatherId   = weatherId;
        upweatherMain = weatherMain;
        //upweatherDesc = weatherDesc;
        //upweatherIcon = weatherIcon;
        upsunrise     = sunrise;
        upsunset      = sunset;
      return;
      }
      else
      {
      // temp        = (int)(root["main"]["temp"]) - 273.15;            // Get temperature in °C
      // humidity    = root["main"]["humidity"];                        // Get humidity in %

      tem = (float) (root["main"]["temp"]) - 273.15; 
      hum = (float) (root["main"]["humidity"]);
      visibility = (uint_fast8_t)root["visibility"];                    // Get visibility in m
      pressure    = (uint_fast8_t)(root["main"]["pressure"]);           // Get pressure in bar
      wind_speed  = (float)(root["wind"]["speed"]);                     // Get wind speed in m/s
      wind_degree = (float)(root["wind"]["deg"]);                       // Get wind degree in °

      upweatherId   = (const char*)(root["weather"][0]["id"]);
      upweatherMain = (const char*)(root["weather"][0]["main"]);  
      
      //upweatherDesc = (const char*)(root["weather"][0]["description"]);
      sWeatherDesc = (const char *) (root["weather"][0]["main"]);
      printf ( " in update weather desc : %s \n", sWeatherDesc.c_str() );

      //upweatherIcon = (const char*)(root["weather"][0]["icon"]);
      //printf ( "in update weather icon : %s\n", upweatherIcon );
      sWeatherIcon = (const char *) (root["weather"][0]["icon"]);
      printf ( "in update weather icon : %s\n", sWeatherIcon.c_str() );

      upsunrise = (root["sys"]["sunrise"]);
      upsunset = (root["sys"]["sunset"]);
      }
    }
  http.end();   // Close connection
}
//#########################################################################################
void showTHPInfo(float temperature, float humidity, uint_fast8_t pressureinfo, uint_fast16_t offset_x, uint_fast16_t offset_y)
{

  M5.SHT30.UpdateData();
  float t_tem = M5.SHT30.GetTemperature();
  float t_hum = M5.SHT30.GetRelHumidity();

//... changed to smaller icons for temperature and humidity to allow multiple temps (outdoor and indoor)

  // Print temperature value
  gfx.setCursor(offset_x, offset_y);
  gfx.setTextSize(FONT_SIZE_MEDIUM);
//  gfx.printf("%02.1f°C", temperature);
  gfx.printf("%02.1f°F", (temperature * 9 / 5) + 32 );

  gfx.setCursor(offset_x, offset_y + 50);
  gfx.setTextSize(FONT_SIZE_SMALL);
//  gfx.printf("%02.1f°C", t_tem);
  gfx.printf("%02.1f°F", (t_tem * 9 / 5) + 32 );
  
  // Print humidity value
  gfx.setCursor(offset_x + 190, offset_y);
  gfx.setTextSize(FONT_SIZE_MEDIUM);
  gfx.printf("%02.1f%%", humidity);

  gfx.setCursor(offset_x + 190, offset_y + 50);
  gfx.setTextSize(FONT_SIZE_SMALL);
  gfx.printf("%02.1f%%", t_hum);

  // Print pressure value
  gfx.setCursor(offset_x + 385, offset_y);
  gfx.setTextSize(FONT_SIZE_MEDIUM);
  gfx.printf("%04d\r\n", pressureinfo);
  
  // Draw temperature icon
/*
  THPIcons.createSprite(112, 128);
  THPIcons.setSwapBytes(true);
  THPIcons.fillSprite(WHITE);  
  THPIcons.pushImage(0, 0, 112, 128, (uint16_t *)Temperature112x128);
  THPIcons.pushSprite(offset_x + 10, offset_y + 55);  
*/

  THPIcons.createSprite(56, 64);
  THPIcons.setSwapBytes(true);
  THPIcons.fillSprite(WHITE);  
  THPIcons.pushImage(0, 0, 56, 64, (uint16_t *)Temperature56x64);
//  THPIcons.pushSprite(offset_x + 10, offset_y + 55);
  THPIcons.pushSprite(offset_x + 10, offset_y + 95);
  
  // Draw humidity icon
/*
  THPIcons.createSprite(88, 128);
  THPIcons.setSwapBytes(true);
  THPIcons.fillSprite(WHITE);
  THPIcons.pushImage(0, 0, 88, 128, (uint16_t *)Humidity88x128);
  THPIcons.pushSprite(offset_x + 200, offset_y + 55);
*/

  THPIcons.createSprite(44, 64);
  THPIcons.setSwapBytes(true);
  THPIcons.fillSprite(WHITE);
  THPIcons.pushImage(0, 0, 44, 64, (uint16_t *)Humidity44x64);
  THPIcons.pushSprite(offset_x + 200, offset_y + 95);


  // Draw pressure icon
  THPIcons.createSprite(128, 128);
  THPIcons.setSwapBytes(true);
  THPIcons.fillSprite(WHITE);
  THPIcons.pushImage(0, 0, 128, 128, (uint16_t *)Pressure128x128);
  THPIcons.pushSprite(offset_x + 370, offset_y + 55);
}
//#########################################################################################
void showWeatherInfo(const char* weatherDescription, uint_fast16_t offset_x, uint_fast16_t offset_y)
{
  // Print weather description
  gfx.setCursor(offset_x, offset_y);
  gfx.printf("%s\r\n", weatherDescription);
  
  // Draw weather icon
  WeatherIcons.createSprite(140, 121);
  WeatherIcons.setSwapBytes(true);
  WeatherIcons.fillSprite(WHITE);

  if(0 == strcmp(weatherIcon,"01d"))
    WeatherIcons.pushImage(0, 0, 140, 120, (uint16_t *)PIC01D);
  else if (0 == strcmp(weatherIcon,"01n"))
    WeatherIcons.pushImage(0, 0, 140, 120, (uint16_t *)PIC01N);
  else if (0 == strcmp(weatherIcon,"02d"))
    WeatherIcons.pushImage(0, 0, 140, 120, (uint16_t *)PIC02D);
  else if (0 == strcmp(weatherIcon,"02n"))
    WeatherIcons.pushImage(0, 0, 140, 120, (uint16_t *)PIC02N);
  else if (0 == strcmp(weatherIcon,"03d"))
    WeatherIcons.pushImage(0, 0, 140, 120, (uint16_t *)PIC03D);
  else if (0 == strcmp(weatherIcon,"03n"))
    WeatherIcons.pushImage(0, 0, 140, 120, (uint16_t *)PIC03N);
  else if (0 == strcmp(weatherIcon,"04d"))
    WeatherIcons.pushImage(0, 0, 140, 121, (uint16_t *)PIC04D);
  else if (0 == strcmp(weatherIcon,"04n"))
    WeatherIcons.pushImage(0, 0, 140, 120, (uint16_t *)PIC04N);
  else if (0 == strcmp(weatherIcon,"09d"))
    WeatherIcons.pushImage(0, 0, 140, 120, (uint16_t *)PIC09D);
  else if (0 == strcmp(weatherIcon,"09n"))
    WeatherIcons.pushImage(0, 0, 140, 120, (uint16_t *)PIC09N);
  else if (0 == strcmp(weatherIcon,"10d"))
    WeatherIcons.pushImage(0, 0, 140, 120, (uint16_t *)PIC10D);
  else if (0 == strcmp(weatherIcon,"10n"))
    WeatherIcons.pushImage(0, 0, 140, 120, (uint16_t *)PIC10N);
  else if (0 == strcmp(weatherIcon,"11d"))
    WeatherIcons.pushImage(0, 0, 140, 120, (uint16_t *)PIC11D);
  else if (0 == strcmp(weatherIcon,"11n"))
    WeatherIcons.pushImage(0, 0, 140, 120, (uint16_t *)PIC11N);
  else if (0 == strcmp(weatherIcon,"13d"))
    WeatherIcons.pushImage(0, 0, 140, 120, (uint16_t *)PIC13D);
  else if (0 == strcmp(weatherIcon,"13n"))
    WeatherIcons.pushImage(0, 0, 140, 120, (uint16_t *)PIC13N);
  else if (0 == strcmp(weatherIcon,"50d"))
    WeatherIcons.pushImage(0, 0, 140, 120, (uint16_t *)PIC50D);
  else if (0 == strcmp(weatherIcon,"50n"))
    WeatherIcons.pushImage(0, 0, 140, 120, (uint16_t *)PIC50N);
  else
  {
    gfx.setCursor(offset_x, offset_y);
    gfx.printf("Loading..........\r\n");
    WeatherIcons.pushImage(0, 0, 120, 120, (uint16_t *)LOADING120x120);
  }
  WeatherIcons.pushSprite(offset_x + 50, offset_y + 80);
}
//#########################################################################################
void showSunriseSunset(uint_fast16_t offset_x, uint_fast16_t offset_y)
{
  // Draw sunrise icon
  SRSSIcons.createSprite(64, 64);
  SRSSIcons.setSwapBytes(true);
  SRSSIcons.fillSprite(WHITE);
  SRSSIcons.pushImage(0, 0, 64, 64, (uint16_t *)SUNRISE64x64);
  SRSSIcons.pushSprite(offset_x, offset_y);

  // Change sunrise time to local time
  // time_t returns the time since the Epoch (00:00:00 UTC, January 1, 1970), measured in seconds
  int h_sunrise = ((upsunrise + timezone * 3600) / 3600) % 24;
  int m_sunrise = ((upsunrise + timezone * 3600) / 60) % 60;
  // int s_sunrise = (upsunrise + timezone * 3600) % 60;
  
  // Print sunrise time in hh:mm format
  gfx.setCursor(offset_x + 75, offset_y + 15);
  gfx.printf("%02d:%02d\r\n", h_sunrise, m_sunrise);
  
  // Draw sunset icon
  SRSSIcons.createSprite(64, 64);
  SRSSIcons.setSwapBytes(true);
  SRSSIcons.fillSprite(WHITE);
  SRSSIcons.pushImage(0, 0, 64, 64, (uint16_t *)SUNSET64x64);
  SRSSIcons.pushSprite(offset_x, offset_y + 70);
  // Print sunrise time in hh:mm format
  // Change sunset time to local time
  int h_sunset = ((upsunset + timezone * 3600) / 3600) % 24;
  int m_sunset = ((upsunset + timezone * 3600) / 60) % 60;
  // int s_sunset = (upsunset + timezone * 3600) % 60;
  
  // Print sunset time in hh:mm format
  gfx.setCursor(offset_x + 75, offset_y + 85);
  gfx.printf("%02d:%02d\r\n", h_sunset, m_sunset);
  
  // Draw visibility icon
  SRSSIcons.createSprite(64, 64);
  SRSSIcons.setSwapBytes(true);
  SRSSIcons.fillSprite(WHITE);
  SRSSIcons.pushImage(0, 0, 64, 64, (uint16_t *)Visibility64x64);
  SRSSIcons.pushSprite(offset_x, offset_y + 140);
  
  // Print visibility value in km
  gfx.setCursor(offset_x + 75, offset_y + 150);
  gfx.printf("%dkm\r\n", (visibility/1000));
}

//#########################################################################################

void showStatusInfo(uint_fast16_t x, uint_fast16_t y, int rssi)
{
  // WiFi Information
  gfx.setCursor(x, y);
  gfx.print("WiFi: ");
  gfx.printf("%ddBm", rssi);
  
  // Battery Information
  gfx.setCursor(x + 200, y);
  gfx.printf("BAT: ");
  uint32_t vol = M5.getBatteryVoltage();
    if(vol < 3300)
    {
        vol = 3300;
    }
    else if(vol > 4350)
    {
        vol = 4350;
    }
    float battery = (float)(vol - 3300) / (float)(4350 - 3300);
    if(battery <= 0.01)
    {
        battery = 0.01;
    }
    if(battery > 1)
    {
        battery = 1;
    }
    uint8_t percentage = battery * 100;
    gfx.printf("%d%%\r\n", percentage);
}

//#########################################################################################
void showWindInfo(float windspeed, float winddegree, uint_fast16_t offset_x, uint_fast16_t offset_y) 
{
  WIcons.createSprite(144, 144);
  WIcons.setSwapBytes(true);
  WIcons.fillSprite(WHITE);

  gfx.setCursor(offset_x + 80, offset_y);
  gfx.setTextSize(FONT_SIZE_SMALL);
  gfx.println("Wind Info");
  gfx.setTextSize(FONT_SIZE_MEDIUM);
  
  // Print wind speed and wind degree
  gfx.setCursor(offset_x + 160, offset_y + 50);
  gfx.printf("%02.1fm/s\r\n", windspeed);
  gfx.setCursor(offset_x + 160, offset_y + 100);
  gfx.printf("%02.1f°\r\n", winddegree);
  gfx.setCursor(offset_x + 160, offset_y + 150);
  
  // Draw wind icon and print wind direction in text
  if (winddegree >= 348.75 || winddegree < 11.25)  
  {
    WIcons.pushImage(0, 0, 144, 144, (uint16_t *)NN144x144);
    gfx.println(TXT_N);
  }
  else if (winddegree >=  11.25 && winddegree < 33.75)  
  {
    WIcons.pushImage(0, 0, 144, 144, (uint16_t *)NNE144x144);
    gfx.println(TXT_NNE);
  }
  else if (winddegree >=  33.75 && winddegree < 56.25)  
  {
    WIcons.pushImage(0, 0, 144, 144, (uint16_t *)NE144x144);
    gfx.println(TXT_NE);
  }
  else if (winddegree >=  56.25 && winddegree < 78.75)  
  {
    WIcons.pushImage(0, 0, 144, 144, (uint16_t *)ENE144x144);
    gfx.println(TXT_ENE);
  }
  else if (winddegree >=  78.75 && winddegree < 101.25)
  {
    WIcons.pushImage(0, 0, 144, 144, (uint16_t *)EE144x144);
    gfx.println(TXT_E);
  }
  else if (winddegree >= 101.25 && winddegree < 123.75)
  {
    WIcons.pushImage(0, 0, 144, 144, (uint16_t *)ESE144x144);
    gfx.println(TXT_ESE);
  }
  else if (winddegree >= 123.75 && winddegree < 146.25) 
  {
    WIcons.pushImage(0, 0, 144, 144, (uint16_t *)SE144x144);
    gfx.println(TXT_SE);
  }
  else if (winddegree >= 146.25 && winddegree < 168.75) 
  {
    WIcons.pushImage(0, 0, 144, 144, (uint16_t *)SSE144x144);
    gfx.println(TXT_SSE);
  }
  else if (winddegree >= 168.75 && winddegree < 191.25) 
  {
    WIcons.pushImage(0, 0, 144, 144, (uint16_t *)SS144x144);
    gfx.println(TXT_S);
  }
  else if (winddegree >= 191.25 && winddegree < 213.75) 
  {
    WIcons.pushImage(0, 0, 144, 144, (uint16_t *)SSW144x144);
    gfx.println(TXT_SSW);
  }
  else if (winddegree >= 213.75 && winddegree < 236.25) 
  {
    WIcons.pushImage(0, 0, 144, 144, (uint16_t *)SW144x144);
    gfx.println(TXT_SW);
  }
  else if (winddegree >= 236.25 && winddegree < 258.75) 
  {
    WIcons.pushImage(0, 0, 144, 144, (uint16_t *)WSW144x144);
    gfx.println(TXT_WSW);
  }
  else if (winddegree >= 258.75 && winddegree < 281.25) 
  {
    WIcons.pushImage(0, 0, 144, 144, (uint16_t *)WW144x144);
    gfx.println(TXT_W);
  }
  else if (winddegree >= 281.25 && winddegree < 303.75) 
  {
    WIcons.pushImage(0, 0, 144, 144, (uint16_t *)WNW144x144);
    gfx.println(TXT_WNW);
  }
  else if (winddegree >= 303.75 && winddegree < 326.25) 
  {
    WIcons.pushImage(0, 0, 144, 144, (uint16_t *)NW144x144);
    gfx.println(TXT_NW);
  }
  else if (winddegree >= 326.25 && winddegree < 348.75) 
  {
    WIcons.pushImage(0, 0, 144, 144, (uint16_t *)NNW144x144);
    gfx.println(TXT_NNW);
  }
  else
  {
    gfx.setCursor(offset_x + 150, offset_y);
    gfx.printf("...m/s\r\n");
    gfx.setCursor(offset_x + 150, offset_y + 50);
    gfx.printf("...°\r\n");
    gfx.setCursor(offset_x + 150, offset_y + 100);
    gfx.printf("Loading\r\n");
    WIcons.pushImage(0, 0, 144, 144, (uint16_t *)CALM144x144);
  }
  WIcons.pushSprite(offset_x, offset_y + 50);
}
//#########################################################################################

void loop(void)
{ 
  constexpr uint_fast16_t SLEEP_SEC = 5;
  constexpr uint_fast32_t TIME_SYNC_CYCLE = 7 * 3600 * 24 / SLEEP_SEC;

  //... moved to global
  //static uint32_t cnt = 0;

  xSemaphoreTake(xMutex, portMAX_DELAY);

  //M5.SHT30.UpdateData();
  //tem = M5.SHT30.GetTemperature();
  //hum = M5.SHT30.GetRelHumidity();

  rtc_date_t date;
  rtc_time_t time;

  M5.RTC.getTime(&time);
  M5.RTC.getDate(&date);
  
  
  gfx.startWrite();
  gfx.fillScreen(TFT_WHITE);
  gfx.fillRect(0.57 * M5PAPER_WIDTH, 0, 3, M5PAPER_HEIGHT, TFT_BLACK); // 960*0.57 = 547.2

  constexpr uint_fast16_t offset_y = 30;
  constexpr uint_fast16_t offset_x = 30;
  
  weatherId   = upweatherId;
  weatherMain = upweatherMain;
  //weatherDesc = upweatherDesc;
  weatherDesc = sWeatherDesc.c_str();
  //weatherIcon = upweatherIcon;
  weatherIcon = sWeatherIcon.c_str();
  sunrise     = upsunrise;
  sunset      = upsunset;

  gfx.setCursor(0, offset_y);
  gfx.setClipRect(offset_x, offset_y, M5PAPER_WIDTH - offset_x, M5PAPER_HEIGHT - offset_y);
  gfx.setTextSize(FONT_SIZE_MEDIUM);

  gfx.printf("Ho Chi Minh City, VN\r\n");
  printf ( "weather : %s\n", weatherIcon );
  showWeatherInfo(weatherDesc, offset_x, offset_y + 50);

  showSunriseSunset(offset_x + 280, offset_y + 90);

  gfx.drawLine(0, 335, 0.57 * M5PAPER_WIDTH, 335, BLACK);
  showTHPInfo(tem, hum, pressure, offset_x, 350);

  gfx.clearClipRect();

  constexpr float x = 0.61 * M5PAPER_WIDTH;
  gfx.setCursor(0, offset_y);
  gfx.setTextSize(FONT_SIZE_LARGE);
  gfx.setClipRect(x, offset_y, M5PAPER_WIDTH - offset_x - x, M5PAPER_HEIGHT - offset_y);

  gfx.printf("%02d:%02d:%02d\r\n", time.hour, time.min, time.sec);
  gfx.setTextSize(FONT_SIZE_MEDIUM);
  gfx.printf("%04d/%02d/%02d ", date.year, date.mon, date.day);
  gfx.println(weekdayToString(date.week));

  gfx.clearClipRect();

  showWindInfo(wind_speed, wind_degree, 0.57 * M5PAPER_WIDTH + 50, 180);

  constexpr float offset_y_info = 0.75 * M5PAPER_HEIGHT;
  gfx.setCursor(0, offset_y_info);
  gfx.setTextSize(FONT_SIZE_SMALL);
  gfx.setClipRect(x, offset_y_info, M5PAPER_WIDTH - x, gfx.height() - offset_y_info);

  showStatusInfo(x, offset_y_info + 90, wifi_signal);

  gfx.setTextSize(FONT_SIZE_MEDIUM);

  gfx.setCursor(0, offset_y_info + 25);
  gfx.print("NTP: ");

  if (date_ntp.year == 1970)
  {
    gfx.print("NOT SYNC"); // not initialized
  }
  else
  {
    gfx.printf("%02d/%02d %02d:%02d",
               date_ntp.mon, date_ntp.day,
               time_ntp.hour, time_ntp.min);
  }

  gfx.clearClipRect();
  gfx.setTextSize(FONT_SIZE_LARGE);
  gfx.endWrite();


  printf ( "cnt : %u\n", cnt );
  if (cnt == TIME_SYNC_CYCLE)
  {
    syncNTPTimeVN();
    cnt = 0;
  }
  xSemaphoreGive(xMutex);
  delay(SLEEP_SEC * 1000);

  if ( cnt % 50 == 0 ) 
  {
    MakehttpRequest();
  }
  cnt++;

}
