
#include "esp_camera.h"
#include <WiFi.h>
#include "esp_timer.h"
#include "img_converters.h"
#include "Arduino.h"
#include "fb_gfx.h"
#include "soc/soc.h"          //disable brownout problems
#include "soc/rtc_cntl_reg.h" //disable brownout problems
#include "esp_http_server.h"
#include <WiFiUdp.h>
#include <Adafruit_NeoPixel.h>
#include <esp_now.h>
#include "secrets.h"

struct InfoData {
 char command[10];
};

struct EventValue
{
  String key;
  String value;
};

InfoData infoData;
// Set the SLAVE MAC Address USE YOUR ESP-01 MAC_ADDRESS SLAVE
uint8_t slaveAddress[] = {0x30, 0x83, 0x98, 0xB0, 0xF6, 0x5B};

//functions
void brightLed();
void decodeCommand(EventValue eventValue);
void startCameraServer();
void connectToHomeWiFi();

// Wifi variables
const char*  APSSID = "WIFI_Car_4321"; 
#define SSID_NAME ssid
#define SSID_PASSWORD password

bool isConnectedToHomeWiFi = false;

// wifi UDP
#define UDP_TX_PACKET_MAX_SIZE 20
unsigned int localUDPPort = 8882; // local port to listen on
// buffers for receiving and sending data
char packetBuffer[UDP_TX_PACKET_MAX_SIZE + 1]; // buffer to hold incoming packet,
char ReplyBuffer[] = "acknowledged\r\n";       // a string to send back
WiFiUDP Udp;

// safe motor wheels
int leftCounter = 0;
int rightounter = 0;


#define PIN            3
#define NUMPIXELS      2
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);
uint32_t red = pixels.Color(255,0,0);
uint32_t green = pixels.Color(0,255,0);
uint32_t blue = pixels.Color(0,0,255);
uint32_t yellow = pixels.Color(255,255,0);
uint32_t white = pixels.Color(255,255,255);
uint32_t pink = pixels.Color(255,0,100);
uint32_t cyan = pixels.Color(0,255,255);
uint32_t orange = pixels.Color(230,80,0);
uint32_t  colors[] = {white, red, green, blue, yellow, pink, cyan, orange};
int currentColor = 0;
int ledBrightness = 20;
unsigned long previousMillis = 0; // last time update
long interval = 60000; // interval at which to do something (milliseconds)

#define PART_BOUNDARY "123456789000000000000987654321"
// defined for CAMERA_MODEL_AI_THINKER
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

static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t stream_httpd = NULL;

static esp_err_t stream_handler(httpd_req_t *req)
{
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t *_jpg_buf = NULL;
  char *part_buf[64];

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK)
  {
    return res;
  }

  while (true)
  {
    fb = esp_camera_fb_get();
    if (!fb)
    {
      //Serial.println("Camera capture failed");
      res = ESP_FAIL;
    }
    else
    {
      if (fb->width > 400)
      {
        if (fb->format != PIXFORMAT_JPEG)
        {
          bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
          esp_camera_fb_return(fb);
          fb = NULL;
          if (!jpeg_converted)
          {
            //Serial.println("JPEG compression failed");
            res = ESP_FAIL;
          }
        }
        else
        {
          _jpg_buf_len = fb->len;
          _jpg_buf = fb->buf;
        }
      }
    }
    if (res == ESP_OK)
    {
      size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }
    if (res == ESP_OK)
    {
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }
    if (res == ESP_OK)
    {
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }
    if (fb)
    {
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    }
    else if (_jpg_buf)
    {
      free(_jpg_buf);
      _jpg_buf = NULL;
    }
    if (res != ESP_OK)
    {
      break;
    }
  }
  return res;
}
// Utils
EventValue stringToStruct(char array[])
{
  String keyValueString = array;
  struct EventValue data = {"", ""};
  int delimiter, delimiter_1, delimiter_2, delimiter_3;
  delimiter = keyValueString.indexOf(",");
  delimiter_1 = keyValueString.indexOf("%", delimiter + 1);
  delimiter_2 = keyValueString.indexOf(",", delimiter_1 + 1);
  String first = keyValueString.substring(delimiter + 1, delimiter_1);
  String second = keyValueString.substring(delimiter_1 + 1, delimiter_2);
  data.value = first;
  data.key = second;
  return data;
}

void startCameraServer()
{
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;

  httpd_uri_t index_uri = {
      .uri = "/",
      .method = HTTP_GET,
      .handler = stream_handler,
      .user_ctx = NULL};

  // Serial.printf("Starting web server on port: '%d'\n", config.server_port);
  if (httpd_start(&stream_httpd, &config) == ESP_OK)
  {
    httpd_register_uri_handler(stream_httpd, &index_uri);
  }
}

void backward()
{
  strcpy(infoData.command , "B"); 
  esp_now_send(slaveAddress, (uint8_t *) &infoData, sizeof(infoData));
  
}
void forward()
{
 strcpy(infoData.command , "F"); 
  esp_now_send(slaveAddress, (uint8_t *) &infoData, sizeof(infoData));
}
void left()
{
  strcpy(infoData.command , "L"); 
  esp_now_send(slaveAddress, (uint8_t *) &infoData, sizeof(infoData));
}
void right()
{
  strcpy(infoData.command , "R"); 
  esp_now_send(slaveAddress, (uint8_t *) &infoData, sizeof(infoData));
}
void stop()
{
  strcpy(infoData.command , "S"); 
  esp_now_send(slaveAddress, (uint8_t *) &infoData, sizeof(infoData));
}

void moveCar(EventValue eventValue)
{
  if (eventValue.value == "F")
  {
    Serial.println(" to forward");
    leftCounter = 0;
    rightounter = 0;
    forward();
  } else if (eventValue.value == "B")
  {
    leftCounter = 0;
    rightounter = 0;
    backward();
  } else if (eventValue.value == "L")
  {
    leftCounter = leftCounter + 1;
    rightounter = 0;
    if (leftCounter > 20) {
      return;
    }
    left();
  } else if (eventValue.value == "R")
  {
    rightounter = rightounter + 1;
    leftCounter = 0;
    if (rightounter > 20) {
      return;
    }
    right();
  } 
}

void decodeCommand(EventValue eventValue) {

  if (eventValue.key == "wheels") {
    Serial.println("moving car");
    moveCar(eventValue);
  }else if (eventValue.key == "light") {
    int valueInt = eventValue.value.toInt();
    if (valueInt >= 0 && valueInt < 255) {
      ledBrightness = valueInt;
    }
    
  }else if (eventValue.key == "color") {
    int valueInt = eventValue.value.toInt();
    if (valueInt < 6) {
       currentColor = valueInt;
    }
  }else if (eventValue.key == "speed") {
    int valueInt = eventValue.value.toInt();
    if (valueInt >= 1 && valueInt < 9) {
        itoa(valueInt, infoData.command, 10);
        esp_now_send(slaveAddress, (uint8_t *) &infoData, sizeof(infoData));
    }

  }else if (eventValue.key == "buzzer") {
      pixels.clear();
      pixels.fill(colors[currentColor]);
      pixels.setBrightness(255);
      pixels.show(); // This sends the updated pixel color to the hardware.
      delay(200);
  }
}
void createSoftAP() {
  WiFi.mode(WIFI_AP_STA); // Modo AP + STA
  bool apStarted = WiFi.softAP(APSSID); // Iniciar el punto de acceso

  if (apStarted) {
      Serial.println("Punto de acceso creado con éxito");
      Serial.print("IP del AP: ");
      Serial.println(WiFi.softAPIP());
  } else {
      Serial.println("Error al crear el punto de acceso");
  }
}

void connectToHomeWiFi() {
  if (isConnectedToHomeWiFi) {
      Serial.println("Already Connected to Wi-Fi network");
      return;
  }

  Serial.println(" Desconecting to AP and previous conections");
  WiFi.softAPdisconnect(true); 
  WiFi.disconnect(true); 
  delay(500); 

  WiFi.mode(WIFI_STA);
  delay(500); 

  Serial.print("Connecting to red Wi-Fi: ");
  Serial.println(SSID_NAME);

  WiFi.begin(SSID_NAME, SSID_PASSWORD);

  // Intentar conectar con un tiempo de espera (timeout)
  unsigned long startTime = millis();
  const unsigned long timeout = 10000; // 10 segundos de tiempo de espera

  while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
      if (millis() - startTime > timeout) {
          Serial.println("\nError: Wi-Fi (timeout)");
          Serial.print("State Wi-Fi: ");
          Serial.println(WiFi.status()); 
          Serial.println("Back to AP_STA...");
          WiFi.mode(WIFI_AP_STA);
          createSoftAP();
          return;
      }
  }

  currentColor = 3;
  Serial.println("\nConnected to Wi-Fi!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  isConnectedToHomeWiFi = true; 
}

// change illumination LED brightness
 void brightLed() {
      pixels.clear();
      pixels.fill(colors[currentColor]);
      pixels.setBrightness(ledBrightness);
      pixels.show(); // This sends the updated pixel color to the hardware.

 }

void setupLights() {
    pixels.begin(); // This initializes the NeoPixel library.
    pixels.show();
}

void OnSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("\r\nSend message status:\t");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Sent Successfully" : "Sent Failed");
}

void setupEspNow() {
  
   // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("There was an error initializing ESP-NOW");
    return;
  }
  // We will register the callback function to respond to the event
  esp_now_register_send_cb(OnSent);
  // Register the slave
  esp_now_peer_info_t slaveInfo  = {};
  memcpy(slaveInfo.peer_addr, slaveAddress, 6);
  slaveInfo.channel = 0;  
  slaveInfo.encrypt = false;
  
  // Add slave        
  if (esp_now_add_peer(&slaveInfo) != ESP_OK){
    Serial.println("There was an error registering the slave");
    return;
  }
}

void setup()
{
  //WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // disable brownout detector

  Serial.begin(115200);
  while (!Serial);
  delay(1000);
  Serial.println("Initializing");
  setupLights();
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

  config.frame_size = FRAMESIZE_VGA;
  
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 15;
  config.fb_count = 1;

  // Camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK)
  {
    Serial.printf("Camera init failed with error 0x%x", err);
    //return;
  } 

 
createSoftAP();
startCameraServer();
Udp.begin(localUDPPort);  
setupEspNow(); 
}

void loop()
{
  delay(1);
  
  int packetSize = Udp.parsePacket();
  if (packetSize)
  {
    int n = Udp.read(packetBuffer, UDP_TX_PACKET_MAX_SIZE);
    packetBuffer[n] = 0;
    decodeCommand(stringToStruct(packetBuffer));
  }
  if (WiFi.softAPgetStationNum() == 0) {
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= interval) {
        previousMillis = currentMillis;
        connectToHomeWiFi();
    }
} else {
    if (isConnectedToHomeWiFi) {
        Serial.println("Clients connected to the AP. Disconnecting from the home Wi-Fi network...");
        WiFi.disconnect(true);
        isConnectedToHomeWiFi = false;
        createSoftAP(); 
    }
    Serial.println("Clients connected to the access point. Maintaining AP mode...");
}

// Optional: check status
/*
static unsigned long lastStatusMillis = 0;
if (millis() - lastStatusMillis >= 5000) {
    lastStatusMillis = millis();
    Serial.print("Número de clientes conectados al AP: ");
    Serial.println(WiFi.softAPgetStationNum());
    Serial.print("Estado de conexión a la red Wi-Fi: ");
    Serial.println(isConnectedToHomeWiFi ? "Conectado" : "No conectado");
}
*/
  brightLed();
}