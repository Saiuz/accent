#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include "credentials.h"
#include "display.h"

// Display: https://www.waveshare.com/wiki/7.5inch_e-Paper_HAT_(B)
// Board: https://www.waveshare.com/wiki/E-Paper_ESP32_Driver_Board
// Board Manager URL: https://dl.espressif.com/dl/package_esp32_index.json
// Board: ESP32 Dev Module

// The baud rate for the serial connection.
const long serial_speed = 115200;

// The size in bytes of the streaming HTTP response buffer.
const size_t buffer_size = 1024;

// The power domains to turn off for deep sleep.
const esp_sleep_pd_domain_t power_domains[] = {
    ESP_PD_DOMAIN_RTC_PERIPH,
    ESP_PD_DOMAIN_RTC_SLOW_MEM,
    ESP_PD_DOMAIN_RTC_FAST_MEM,
    ESP_PD_DOMAIN_XTAL};

void setup() {
  // Workaround for a bug where sometimes waking up from deep sleep fails.
  // https://www.hackster.io/nickthegreek82/esp32-deep-sleep-tutorial-4398a7
  delay(500);

  Serial.begin(serial_speed);

  // Connect to the Wifi access point.
  connectWifi();

  // Initialize the display.
  initDisplay();

  // Show the latest image.
  if (downloadImage()) {
    updateDisplay();
  }

  // TODO: Report battery level/voltage to server.

  // Go to sleep until the next refresh.
  scheduleSleep();
}

void loop() {
  // The setup() function should never return.
  Serial.println("Unexpectedly entered main loop");
  delay(ULONG_MAX);
}

// Connects to the Wifi access point.
void connectWifi() {
  if (WiFi.isConnected()) {
    Serial.println("Already connected");
    return;
  }

  // Use the SSID and password specified in "credentials.h".
  Serial.printf("Connecting to \"%s\" .", wifi_credentials.ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_credentials.ssid, wifi_credentials.password);

  // Wait until connected.
  while (!WiFi.isConnected()) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nConnected to %s as %s\n", WiFi.SSID().c_str(),
                WiFi.localIP().toString().c_str());
}

// Opens a HTTP GET connection with the specified URL.
bool httpGet(HTTPClient* http, String url) {
  Serial.printf("Requesting URL: %s\n", url.c_str());

  if (!http->begin(url, root_certificate)) {
    Serial.printf("Failed to connect to server: %s\n", url.c_str());
    return false;
  }

  int status = http->GET();
  if (status <= 0) {
    Serial.printf("Request failed: %s\n", http->errorToString(status).c_str());
    http->end();
    return false;
  }

  Serial.printf("Status code: %d\n", status);
  if (status != HTTP_CODE_OK) {
    http->end();
    return false;
  }

  return true;
}

// Streams the image from the server and sends it to the display in chunks.
bool downloadImage() {
  Serial.println("Downloading image");
  HTTPClient http;

  // Use the image server specified in "credentials.h".
  if (!httpGet(&http, server_credentials.epd_url)) {
    return false;
  }

  // Start reading from the stream.
  char buffer[buffer_size];
  WiFiClient* stream = http.getStreamPtr();
  unsigned long total_count = 0;
  do {
    if (!http.connected()) {
      Serial.println("Connection lost");
      http.end();
      return false;
    }

    Serial.printf("%d bytes available\n", stream->available());

    // Fill the buffer.
    size_t count = stream->readBytes(buffer, sizeof(buffer));
    total_count += count;
    Serial.printf("Read %d bytes (%lu total)\n", count, total_count);

    // Send the buffer to the display.
    loadImage(buffer, count);
  } while (stream->available() > 0);

  Serial.println("Download complete");
  http.end();
  return true;
}

// Sleeps for a time received from the server.
void scheduleSleep() {
  Serial.println("Scheduling sleep");
  HTTPClient http;

  // Use the next time server specified in "credentials.h".
  if (!httpGet(&http, server_credentials.next_url)) {
    return;
  }

  // Read the sleep time from the server.
  String delay_ms_str = http.getString();
  http.end();
  Serial.printf("Sleep server response: %s\n", delay_ms_str.c_str());

  // Set all power domain configs to off.
  for (esp_sleep_pd_domain_t power_domain : power_domains) {
    esp_err_t status = esp_sleep_pd_config(power_domain, ESP_PD_OPTION_OFF);
    if (status != ESP_OK) {
      Serial.printf("Failed to apply power domain config: %d\n", power_domain);
    }
  }

  // Go to deep sleep.
  uint64_t delay_us = 1000 * strtoull(delay_ms_str.c_str(), NULL, 10);
  Serial.printf("Sleeping for %llu us\n", delay_us);
  esp_deep_sleep(delay_us);
}