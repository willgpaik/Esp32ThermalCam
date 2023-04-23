// Import required libraries
#include <WiFi.h>
#include <Adafruit_MLX90640.h>
#include <esp_camera.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebSrv.h>
#include <ESPmDNS.h>
#include "html.h"

//TaskHandle_t TaskA;

// Bolometer - Replace with your own pinout
#define I2C_SCL 13
#define I2C_SDA 12
//#define I2C_PUP  2 // this is for TTL to connect the 2x 4k7 pull up resistors


// Camera - Currently setup according to AI-Thinker board, aka ESP32-CAM
// Replace with your own setup if needed.
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

#define FLASH_LED  4
#define SMALL_LED 33
#define LED_CONTROL 15

#include "wifi.h"
// Bolometer stuff
Adafruit_MLX90640 mlx;
const size_t thermSize = (32 * 24) * sizeof(float);
const size_t frameSize = thermSize + 30000 * sizeof(char);
size_t imageSize = 0;
char frame[frameSize]; // buffer for full frame of temperatures and image

// Websocket stuff
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Sends log messages to websockets if true
bool wsdebug = false;

void log(String text)
{
  Serial.println(text);
  if (wsdebug)
  {
    ws.textAll("SERIAL:" + text);
  }
}

void sendStatus()
{
  log("Sending status");
  String status = "STATUS:Total heap:" + String(ESP.getHeapSize()) += " | Free heap:" + String(ESP.getFreeHeap()) += " | WiFi RSSI:" + String(WiFi.RSSI()) += " | WiFi Status:" + String(WiFi.status());
  ws.textAll(status);
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len)
{
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
  {
    data[len] = 0;
    String command = String((char *)data);
    if (command == "status")
    {
      sendStatus();
    }
    if (command == "debug")
    {
      wsdebug = !wsdebug;
    }
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len)
{
  switch (type)
  {
  case WS_EVT_CONNECT:
    break;
  case WS_EVT_DISCONNECT:
    break;
  case WS_EVT_DATA:
    handleWebSocketMessage(arg, data, len);
    break;
  case WS_EVT_PONG:
  case WS_EVT_ERROR:
    break;
  }
}

void setupWifiAp(const char *ssidname, const char *pass)
{
  WiFi.mode(WIFI_AP);
  WiFi.setTxPower(WIFI_POWER_MINUS_1dBm);
  WiFi.softAP(ssidname, pass);
  IPAddress IP = WiFi.softAPIP();
  delay(5000);
  Serial.print("AP IP address: ");
  Serial.println(IP);
}
  
void setup()
{
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  if (digitalRead(I2C_SDA) == HIGH)
  {
    log("Setting up bolometer");

    // don't use below if connecting to non-TTL manner (i.e. mlx90640 module)
    //pinMode(I2C_PUP, OUTPUT);
    //digitalWrite(I2C_PUP, HIGH);
    Wire.begin(I2C_SDA, I2C_SCL);

    mlx.begin(MLX90640_I2CADDR_DEFAULT, &Wire);
    mlx.setMode(MLX90640_CHESS);
    mlx.setResolution(MLX90640_ADC_16BIT);
    mlx90640_resolution_t res = mlx.getResolution();
    mlx.setRefreshRate(MLX90640_16_HZ); // FPS = Hz/2
    mlx90640_refreshrate_t rate = mlx.getRefreshRate();
    Wire.setClock(1000000); // max 1 MHz
    log("Bolometer setup");

    //xTaskCreatePinnedToCore(take_snapshot, "picture", 100000, NULL, 0, &takePic, 1);
  }

  log("Setting up camera");
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_SVGA; // 800x600 @ 30 fps
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 2;
  esp_err_t err = esp_camera_init(&config);
  sensor_t *s = esp_camera_sensor_get();
  log("Camera setup");

  log("Setting up WiFi");

  setupWifiAp(ssid, password);

  log("WiFi setup");

  if (digitalRead(I2C_SDA) == HIGH)
  {
    log("Setting up MDNS as thermal");
    MDNS.begin("thermal");
    log("MDNS setup");
  }

  log("Setting web server");
  ws.onEvent(onEvent);
  server.addHandler(&ws);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send_P(200, "text/html", index_html); });
  server.begin();
  log("Webserver setup");

  pinMode(FLASH_LED, OUTPUT);
  digitalWrite(FLASH_LED, LOW);
  pinMode(LED_CONTROL, INPUT_PULLUP);
  pinMode(SMALL_LED, OUTPUT);
  digitalWrite(SMALL_LED, HIGH);
}


void take_snapshot( void* parameter )
{
  camera_fb_t *fb = NULL;
  fb = esp_camera_fb_get();
  if (!fb)
  {
    log("Camera capture failed. Restarting");
    ESP.restart();
  }
  //log("Moving image to frame buffer " + String(fb->len) + " (max 30000)");
  memcpy(&frame[thermSize], fb->buf, fb->len);
  imageSize = fb->len;
  esp_camera_fb_return(fb);
  fb = NULL;
}

void take_thermal()
{
  Serial.printf("ThermalgetFrame=%d  ",mlx.getFrame((float *)frame));
}

unsigned long messageTimestamp = 0;
int messageCounter = 0;

void loop() {
  ws.cleanupClients();
  uint64_t now = millis();
  if (now - messageTimestamp > 200) {
    digitalWrite(SMALL_LED,               ws.count()?LOW:HIGH);
    digitalWrite(FLASH_LED, digitalRead(LED_CONTROL)?LOW:HIGH);
    memset(frame, 0, frameSize);
    take_snapshot(NULL);
    if (digitalRead(I2C_SDA) == HIGH)
      take_thermal();
    ws.binaryAll(frame, thermSize + imageSize);
    messageTimestamp = now;
    //messageCounter++;
    /*if (messageCounter > 20) {
      sendStatus();
      messageCounter = 0;
    }*/
  }
}

