// /**********************************************************************
//   Filename    : Video Web Server
//   Description : The camera images captured by the ESP32S3 are displayed on the web page.
//   Auther      : www.freenove.com
//   Modification: 2022/11/01
// **********************************************************************/
// #include "esp_camera.h"
// #include <WiFi.h>
// #include "sd_read_write.h"
// #include <WebServer.h>

// // Select camera model
// #define CAMERA_MODEL_ESP32S3_EYE // Has PSRAM

// #include "camera_pins.h"

// // const char* ssid     = "********";   //input your wifi name
// // const char* password = "********";   //input your wifi passwords
// const char* ssid = "ESP32_Camera_AP";
// const char* password = "12345678";

// // Initialize the web server on port 80
// WebServer server(80);

// void cameraInit(void);
// void startCameraServer();

// void handleRoot() {
//   String html = "<html><body><h1>ESP32 AP Webserver</h1><p>Welcome to the ESP32 Access Point!</p></body></html>";
//   server.send(200, "text/html", html);
// }

// void setup() {
//   Serial.begin(115200);
//   Serial.setDebugOutput(true);
//   Serial.println();

//   cameraInit();
//   sdmmcInit();
//   removeDir(SD_MMC, "/video");
//   createDir(SD_MMC, "/video");
  
//   // WiFi.begin(ssid, password);

//   // while (WiFi.status() != WL_CONNECTED) {
//   //   delay(500);
//   //   Serial.print(".");
//   // }
//   // Serial.println("");
//   // Serial.println("WiFi connected");

//     // Set up ESP32 as Access Point
//   WiFi.softAP(ssid, password);
//   IPAddress IP = WiFi.softAPIP();
//   Serial.print("AP IP address: ");
//   Serial.println(IP);

//   // Define the root URL handler
//   server.on("/", handleRoot);

//   // Start the server
//   server.begin();
//   Serial.println("Web server started");

//   startCameraServer();

//   Serial.print("Camera Ready! Use 'http://");
//   Serial.print(WiFi.localIP());
//   Serial.println("' to connect");
// }

// void loop() {
//   // put your main code here, to run repeatedly:
//   // delay(10000);
//   server.handleClient();
// }

// void cameraInit(void){
//   camera_config_t config;
//   config.ledc_channel = LEDC_CHANNEL_0;
//   config.ledc_timer = LEDC_TIMER_0;
//   config.pin_d0 = Y2_GPIO_NUM;
//   config.pin_d1 = Y3_GPIO_NUM;
//   config.pin_d2 = Y4_GPIO_NUM;
//   config.pin_d3 = Y5_GPIO_NUM;
//   config.pin_d4 = Y6_GPIO_NUM;
//   config.pin_d5 = Y7_GPIO_NUM;
//   config.pin_d6 = Y8_GPIO_NUM;
//   config.pin_d7 = Y9_GPIO_NUM;
//   config.pin_xclk = XCLK_GPIO_NUM;
//   config.pin_pclk = PCLK_GPIO_NUM;
//   config.pin_vsync = VSYNC_GPIO_NUM;
//   config.pin_href = HREF_GPIO_NUM;
//   config.pin_sccb_sda = SIOD_GPIO_NUM;
//   config.pin_sccb_scl = SIOC_GPIO_NUM;
//   config.pin_pwdn = PWDN_GPIO_NUM;
//   config.pin_reset = RESET_GPIO_NUM;
//   config.xclk_freq_hz = 20000000;
//   config.frame_size = FRAMESIZE_UXGA;
//   config.pixel_format = PIXFORMAT_JPEG; // for streaming
//   config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
//   config.fb_location = CAMERA_FB_IN_PSRAM;
//   config.jpeg_quality = 12;
//   config.fb_count = 1;
  
//   // if PSRAM IC present, init with UXGA resolution and higher JPEG quality
//   // for larger pre-allocated frame buffer.
//   if(psramFound()){
//     config.jpeg_quality = 10;
//     config.fb_count = 2;
//     config.grab_mode = CAMERA_GRAB_LATEST;
//   } else {
//     // Limit the frame size when PSRAM is not available
//     config.frame_size = FRAMESIZE_SVGA;
//     config.fb_location = CAMERA_FB_IN_DRAM;
//   }

//   // camera init
//   esp_err_t err = esp_camera_init(&config);
//   if (err != ESP_OK) {
//     Serial.printf("Camera init failed with error 0x%x", err);
//     return;
//   }

//   sensor_t * s = esp_camera_sensor_get();
//   // initial sensors are flipped vertically and colors are a bit saturated
//   s->set_vflip(s, 1); // flip it back
//   s->set_brightness(s, 1); // up the brightness just a bit
//   s->set_saturation(s, 0); // lower the saturation
// }


/**********************************************************************
  Filename    : Video Web Server
  Description : The camera images captured by the ESP32S3 are displayed
                on the web page. (Modified for WiFi AP streaming without SD card)
  Author      : www.freenove.com (modified)
  Modification: 2022/11/01 (modified)
**********************************************************************/
#include "esp_camera.h"
#include <WiFi.h>
// Removed SD card library include
//#include "sd_read_write.h"
#include <WebServer.h>  // Used here for a basic root handler (optional)

#define CAMERA_MODEL_ESP32S3_EYE // Has PSRAM

#include "camera_pins.h"

const char* ssid = "ESP32_Camera_AP";
const char* password = "12345678";

const int analogPin = 1; // Use an appropriate ADC pin for ESP32; for Arduino, it might be A0

// Voltage divider constants
const float V_SUPPLY = 3.3;  // Adjust for your board (e.g., 3.3V for ESP32, 5V for Arduino)
const float R_FIXED = 100000.0;  // Fixed resistor value in ohms

// Thermistor parameters (example values for a 10k thermistor)
const float R0 = 100000.0;   // Resistance at 25°C (in ohms)
const float THERMISTOR_T0 = 298.15;    // 25°C in Kelvin
const float beta = 3950.0;  // Beta coefficient

// Create a basic WebServer instance on port 80 for a welcome page
WebServer server(80);

void cameraInit(void);
void startCameraServer();

// Basic handler to display a welcome message
void handleRoot() {
  String html = "<html><body><h1>ESP32 AP Webserver</h1>"
                "<p>Welcome! Camera streaming is active.</p></body></html>";
  server.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  // Initialize the camera
  cameraInit();

  // Set up ESP32 as a WiFi Access Point
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);

  // Set up the basic HTTP server for the welcome page (optional)
  server.on("/", handleRoot);
  server.begin();
  Serial.println("Local web server started on port 80");

  // Start the camera streaming server (defined in app_httpd.cpp)
  startCameraServer();

  Serial.print("Camera Ready! Connect to: http://");
  Serial.print(WiFi.softAPIP());
  Serial.println(" to view the stream.");
}

void loop() {
  // Process basic HTTP server requests (for the welcome page)
  server.handleClient();
  // Serial.println(analogRead(1));
  thermistorRead();
}

void cameraInit(void) {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size   = FRAMESIZE_UXGA;
  config.pixel_format = PIXFORMAT_JPEG;  // for streaming
  config.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location  = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count     = 1;
  
  // If PSRAM is available, improve JPEG quality and increase frame buffer count.
  if (psramFound()) {
    config.jpeg_quality = 10;
    config.fb_count     = 2;
    config.grab_mode    = CAMERA_GRAB_LATEST;
  } else {
    // If PSRAM is not available, reduce the resolution.
    config.frame_size   = FRAMESIZE_SVGA;
    config.fb_location  = CAMERA_FB_IN_DRAM;
  }

  // Initialize the camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  // Adjust sensor settings
  sensor_t * s = esp_camera_sensor_get();
  s->set_vflip(s, 1);       // Flip image vertically if needed
  s->set_brightness(s, 1);  // Increase brightness slightly
  s->set_saturation(s, 0);  // Adjust saturation
}


void thermistorRead(){
    int adcValue = analogRead(analogPin);
    // For ESP32, ADC reading typically ranges from 0 to 4095
    float voltage = (adcValue / 4095.0) * V_SUPPLY;
    
    // Calculate the thermistor resistance using voltage divider equation:
    // R_thermistor = R_FIXED * ((V_SUPPLY / voltage) - 1)
    float R_thermistor = R_FIXED * ((V_SUPPLY / voltage) - 1);
    
    // Calculate temperature using the Beta equation:
    float temperatureK = 1.0 / ( (1.0/THERMISTOR_T0) + (1.0/beta) * log(R_thermistor / R0) );
    float temperatureC = temperatureK - 273.15; // Convert Kelvin to Celsius

    Serial.print("ADC Value: ");
    Serial.print(adcValue);
    Serial.print(" | Voltage: ");
    Serial.print(voltage, 3);
    Serial.print(" V | Resistance: ");
    Serial.print(R_thermistor, 2);  
    Serial.print(" ohms | Temperature: ");
    Serial.print(temperatureC, 2);
    Serial.println(" °C");
}