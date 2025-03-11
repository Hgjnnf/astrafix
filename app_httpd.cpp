// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
// (Modified for streaming without SD card support)

#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "fb_gfx.h"
#include "driver/ledc.h"

#include "sdkconfig.h"

#include "Arduino.h"
// Removed SD card library include
//#include "sd_read_write.h"

#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_ARDHAL_ESP_LOG)
  #include "esp32-hal-log.h"
  #define TAG ""
#else
  #include "esp_log.h"
  static const char *TAG = "camera_httpd";
#endif

typedef struct {
    httpd_req_t *req;
    size_t len;
} jpg_chunking_t;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY  = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART      = "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %d.%06d\r\n\r\n";

const int analogPin = 1; // Use an appropriate ADC pin for ESP32; for Arduino, it might be A0

// Voltage divider constants
const float V_SUPPLY = 3.3;  // Adjust for your board (e.g., 3.3V for ESP32, 5V for Arduino)
const float R_FIXED = 100000.0;  // Fixed resistor value in ohms

// Thermistor parameters (example values for a 10k thermistor)
const float R0 = 100000.0;   // Resistance at 25°C (in ohms)
const float THERMISTOR_T0 = 298.15;    // 25°C in Kelvin
const float beta = 3950.0;  // Beta coefficient

httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;
httpd_handle_t temp_httpd = NULL;

typedef struct {
    size_t size;  // number of values used for filtering
    size_t index; // current value index
    size_t count; // value count
    int sum;
    int *values;  // array to be filled with values
} ra_filter_t;

static ra_filter_t ra_filter;

static ra_filter_t *ra_filter_init(ra_filter_t *filter, size_t sample_size) {
    memset(filter, 0, sizeof(ra_filter_t));
    filter->values = (int *)malloc(sample_size * sizeof(int));
    if (!filter->values) {
        return NULL;
    }
    memset(filter->values, 0, sample_size * sizeof(int));
    filter->size = sample_size;
    return filter;
}

static int ra_filter_run(ra_filter_t *filter, int value) {
    if (!filter->values) {
        return value;
    }
    filter->sum -= filter->values[filter->index];
    filter->values[filter->index] = value;
    filter->sum += filter->values[filter->index];
    filter->index = (filter->index + 1) % filter->size;
    if (filter->count < filter->size) {
        filter->count++;
    }
    return filter->sum / filter->count;
}

static esp_err_t stream_handler(httpd_req_t *req) {
    camera_fb_t *fb = NULL;
    struct timeval _timestamp;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t *_jpg_buf = NULL;
    char part_buf[128];

    static int64_t last_frame = 0;
    if (!last_frame) {
        last_frame = esp_timer_get_time();
    }

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK) {
        return res;
    }
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "X-Framerate", "60");

    while (true) {
        fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Camera capture failed");
            res = ESP_FAIL;
        } else {
            _timestamp.tv_sec  = fb->timestamp.tv_sec;
            _timestamp.tv_usec = fb->timestamp.tv_usec;
            if (fb->format != PIXFORMAT_JPEG) {
                bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
                esp_camera_fb_return(fb);
                fb = NULL;
                if (!jpeg_converted) {
                    ESP_LOGE(TAG, "JPEG compression failed");
                    res = ESP_FAIL;
                }
            } else {
                _jpg_buf_len = fb->len;
                _jpg_buf = fb->buf;
            }
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        }
        if (res == ESP_OK) {
            size_t hlen = snprintf(part_buf, sizeof(part_buf), _STREAM_PART,
                                    _jpg_buf_len, _timestamp.tv_sec, _timestamp.tv_usec);
            res = httpd_resp_send_chunk(req, part_buf, hlen);
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        }
        if (fb) {
            esp_camera_fb_return(fb);
            fb = NULL;
            _jpg_buf = NULL;
        } else if (_jpg_buf) {
            free(_jpg_buf);
            _jpg_buf = NULL;
        }
        if (res != ESP_OK) {
            ESP_LOGI(TAG, "Stream sending failed: %d, exiting stream handler", res);
            break;
        }
        int64_t fr_end = esp_timer_get_time();
        int64_t frame_time = (fr_end - last_frame) / 1000;
        last_frame = fr_end;
        uint32_t avg_frame_time = ra_filter_run(&ra_filter, frame_time);
    }
    ESP_LOGI(TAG, "Stream handler exit!");
    last_frame = 0;
    return res;
}

const char index_web[] = R"rawliteral(
    <html>
      <head>
        <title>ESP32 Camera Stream & Temperature</title>
        <script>
          // Create an EventSource to receive temperature updates from the /temperature endpoint
          var tempSource = new EventSource("http://192.168.4.1:83/temperature");
          tempSource.onmessage = function(event) {
              try {
                  // Parse the JSON data received from the SSE stream
                  var data = JSON.parse(event.data);
                  // Update the temperature display element with the new reading
                  document.getElementById("temp").innerText = "Temperature: " + data.temperature + " °C";
              } catch (e) {
                  console.error("Error parsing temperature data:", e);
              }
          };
          tempSource.onerror = function(err) {
              console.error("EventSource encountered an error:", err);
              tempSource.close();
          };
        </script>
      </head>
      <body>
        <h1>ESP32 Camera Stream</h1>
        <img src="http://192.168.4.1:82/stream" style="transform:rotate(180deg); width:100%; max-width:800px;">
        <h2>Temperature Reading</h2>
        <div id="temp">Loading temperature...</div>
      </body>
    </html>
    )rawliteral";    

static esp_err_t index_handler(httpd_req_t *req) {
    esp_err_t err;
    err = httpd_resp_set_type(req, "text/html");
    if (err == ESP_OK) {
        err = httpd_resp_send(req, index_web, strlen(index_web));
    }
    return err;
}

static esp_err_t temp_handler(httpd_req_t *req) {
    // Set the response type to JSON and allow cross-origin requests
    esp_err_t res = ESP_OK;
    httpd_resp_set_type(req, "text/event-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Connection", "keep-alive");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    while (true) {
        int adcValue = analogRead(analogPin);
        // For ESP32, ADC reading typically ranges from 0 to 4095
        float voltage = (adcValue / 4095.0) * V_SUPPLY;
        
        // Calculate the thermistor resistance using voltage divider equation:
        // R_thermistor = R_FIXED * ((V_SUPPLY / voltage) - 1)
        float R_thermistor = R_FIXED * ((V_SUPPLY / voltage) - 1);
        
        // Calculate temperature using the Beta equation:
        float temperatureK = 1.0 / ( (1.0/THERMISTOR_T0) + (1.0/beta) * log(R_thermistor / R0) );
        float temperatureC = temperatureK - 273.15; // Convert Kelvin to Celsius

        // Format the JSON payload containing only the temperature
        char json[64];
        int json_len = snprintf(json, sizeof(json), "{\"temperature\": %.2f}", temperatureC);
        
        // Format as SSE data: prefix with "data: " and end with two newlines
        char sse_buffer[128];
        int sse_len = snprintf(sse_buffer, sizeof(sse_buffer), "data: %s\n\n", json);
        
        // Send the SSE chunk; exit if an error occurs (e.g., client disconnects)
        esp_err_t res = httpd_resp_send_chunk(req, sse_buffer, sse_len);
        if (res != ESP_OK) {
            break;
        }
        
        // Delay between readings (e.g., 1 second)
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    
    // Send a final empty chunk to signal the end of the response
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

void startServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;

    // Register URI handler for the index page
    httpd_uri_t index_uri = {
        .uri = "/index",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL
    };

    // Register URI handler for the streaming endpoint
    httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
        .user_ctx = NULL
    };

    httpd_uri_t temp_uri = {
        .uri = "/temperature",
        .method = HTTP_GET,
        .handler = temp_handler,
        .user_ctx = NULL
    };

    ra_filter_init(&ra_filter, 20);

    config.server_port += 1;
    config.ctrl_port   += 1;
    ESP_LOGI(TAG, "Starting camera server on port: '%d'", config.server_port);
    if (httpd_start(&camera_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(camera_httpd, &index_uri);
    }

    config.server_port += 1;
    config.ctrl_port   += 1;
    ESP_LOGI(TAG, "Starting stream server on port: '%d'", config.server_port);
    if (httpd_start(&stream_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
    }

    config.server_port += 1;
    config.ctrl_port   += 1;
    ESP_LOGI(TAG, "Starting temp server on port: '%d'", config.server_port);
    if (httpd_start(&temp_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(temp_httpd, &temp_uri);
    }
}