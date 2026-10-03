#include "firmware_update.h"
#include "firmware_version.h"
#include "release_info.h"
#include "wifi_setup.h"
#include <WiFi.h>
#include <cJSON.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>
#include <time.h>
#include <memory>
#include <new>

// Defer the Arduino core's automatic confirmation until display/PSRAM initialization succeeds.
extern "C" bool verifyRollbackLater() { return true; }

bool FirmwareUpdate::confirmBoot(bool healthy) {
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) != ESP_OK ||
      state != ESP_OTA_IMG_PENDING_VERIFY) return healthy;
  if (!healthy) {
    esp_ota_mark_app_invalid_rollback_and_reboot();
    return false;
  }
  return esp_ota_mark_app_valid_cancel_rollback() == ESP_OK;
}

namespace {
constexpr char kReleaseBase[] = "https://github.com/5h0utat0t2uka/M314/releases/";
constexpr size_t kMaxDownloadUrlLength = 8192;

// RAII closes TLS and frees buffers on every error path.
class Download {
 public:
  ~Download() { if (client_) esp_http_client_cleanup(client_); }
  int status = 0;
  int64_t size = -1;
  bool open(String url) {
    for (unsigned redirect = 0; redirect < 4; ++redirect) {
      status = 0;
      size = -1;
      if (!release_info::downloadUrl(url.c_str())) {
        Serial.println("Update: download URL rejected");
        return false;
      }
      if (url.length() > kMaxDownloadUrlLength) {
        Serial.println("Update: download URL too long");
        return false;
      }
      if (client_) { esp_http_client_cleanup(client_); client_ = nullptr; }
      location_ = "";
      esp_http_client_config_t config{};
      config.url = url.c_str();
      // ESP-IDF must fit the entire request line in the TX buffer. GitHub's
      // signed redirect URLs exceed the 512-byte default. Keep room for the
      // method/protocol and headers; the URL limit bounds this allocation.
      const size_t txSize = url.length() + 128;
      config.buffer_size_tx = static_cast<int>(txSize < 512 ? 512 : txSize);
      config.crt_bundle_attach = esp_crt_bundle_attach;
      config.timeout_ms = 12000;
      config.disable_auto_redirect = true;
      config.user_agent = "CoreS3-Motion-Tracker";
      config.event_handler = onEvent;
      config.user_data = this;
      // Log sizes and error codes only, never signed URLs or credentials.
      Serial.printf("Update: request %u, URL bytes=%u, TX bytes=%d\n",
                    redirect + 1, static_cast<unsigned>(url.length()), config.buffer_size_tx);
      client_ = esp_http_client_init(&config);
      if (!client_) {
        Serial.println("Update: HTTP client initialization failed");
        return false;
      }
      const esp_err_t error = esp_http_client_open(client_, 0);
      if (error != ESP_OK) {
        Serial.printf("Update: HTTP open failed: %s (%d)\n", esp_err_to_name(error), error);
        return false;
      }
      size = esp_http_client_fetch_headers(client_);
      if (size < 0) {
        Serial.printf("Update: response headers failed, errno=%d\n",
                      esp_http_client_get_errno(client_));
        return false;
      }
      status = esp_http_client_get_status_code(client_);
      Serial.printf("Update: HTTP status=%d\n", status);
      if (status == 200) return true;
      if (status != 301 && status != 302 && status != 303 && status != 307 && status != 308) return false;
      if (location_.startsWith("/")) location_ = "https://github.com" + location_;
      url = location_;
    }
    Serial.println("Update: too many redirects");
    return false;
  }
  int read(char* buffer, int capacity) { return esp_http_client_read(client_, buffer, capacity); }
 private:
  esp_http_client_handle_t client_ = nullptr;
  String location_;
  static esp_err_t onEvent(esp_http_client_event_t* event) {
    auto* self = static_cast<Download*>(event->user_data);
    if (event->event_id == HTTP_EVENT_ON_HEADER && !strcasecmp(event->header_key, "Location"))
      self->location_ = event->header_value;
    return ESP_OK;
  }
};

const char* stringField(cJSON* object, const char* key) {
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
  return cJSON_IsString(item) ? item->valuestring : "";
}
}  // namespace

bool FirmwareUpdate::check(Progress progress) {
  version_ = "";
  url_ = "";
  size_ = 0;
  wifi_settings::Credentials credentials;
  if (!WifiSetup::load(credentials)) { message_ = "Set up Wi-Fi first"; return false; }
  progress("Connecting...", -1);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(credentials.ssid, credentials.password);
  credentials = {};
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) delay(20);
  if (WiFi.status() != WL_CONNECTED) { message_ = "Wi-Fi connection failed"; return false; }
  progress("Checking time...", -1);
  configTime(0, 0, "time.cloudflare.com", "pool.ntp.org");
  start = millis();
  while (time(nullptr) < 1767225600 && millis() - start < 15000) delay(20);
  if (time(nullptr) < 1767225600) { message_ = "Time sync failed"; return false; }
  progress("Checking updates...", -1);
  Download download;
  if (!download.open(String(kReleaseBase) + "latest/download/manifest.json")) {
    message_ = download.status == 404 ? "No release available" : "Update check failed";
    return false;
  }
  if (download.size <= 0 || download.size > 2048) { message_ = "Invalid release info"; return false; }
  char buffer[2049]{};
  int used = 0;
  while (used < download.size) {
    const int count = download.read(buffer + used, static_cast<int>(download.size) - used);
    if (count <= 0) { message_ = "Download interrupted"; return false; }
    used += count;
  }
  const char* end = nullptr;
  cJSON* root = cJSON_ParseWithLengthOpts(buffer, used + 1, &end, true);
  if (!root) { message_ = "Invalid release info"; return false; }
  const String candidate = stringField(root, "version");
  std::array<unsigned, 3> current{}, next{};
  cJSON* size = cJSON_GetObjectItemCaseSensitive(root, "size");
  cJSON* schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
  const esp_partition_t* partition = esp_ota_get_next_update_partition(nullptr);
  bool ok = cJSON_IsObject(root) && cJSON_IsNumber(schema) && schema->valuedouble == 1 &&
      !strcmp(stringField(root, "board"), kFirmwareBoard) &&
      !strcmp(stringField(root, "partition"), kFirmwarePartition) &&
      !strcmp(stringField(root, "file"), "firmware.bin") &&
      release_info::version(kFirmwareVersion, current) && release_info::version(candidate.c_str(), next) &&
      release_info::digest(stringField(root, "sha256"), sha256_) &&
      cJSON_IsNumber(size) && partition && size->valuedouble >= 288 &&
      size->valuedouble <= partition->size && size->valuedouble == size->valueint;
  if (ok) size_ = size->valueint;
  cJSON_Delete(root);
  if (!ok) { message_ = "Incompatible release"; return false; }
  if (next <= current) { message_ = "Up to date"; return false; }
  version_ = candidate;
  // Pin the image to the checked version; do not fetch a moving 'latest' URL again.
  url_ = String(kReleaseBase) + "download/v" + version_ + "/firmware.bin";
  message_ = "Update available";
  return true;
}

bool FirmwareUpdate::install(Progress progress) {
  if (!size_ || !url_.length()) { message_ = "Check for updates first"; return false; }
  progress("Downloading...", 0);
  Download download;
  if (!download.open(url_) || download.size != static_cast<int64_t>(size_)) {
    message_ = "Download failed"; return false;
  }
  const esp_partition_t* partition = esp_ota_get_next_update_partition(nullptr);
  esp_ota_handle_t handle = 0;
  if (!partition || size_ > partition->size || esp_ota_begin(partition, size_, &handle) != ESP_OK) {
    message_ = "Could not start update"; return false;
  }
  mbedtls_sha256_context hash;
  mbedtls_sha256_init(&hash);
  bool ok = mbedtls_sha256_starts(&hash, 0) == 0;
  // Leave the small Arduino loop stack available for TLS and the display callback.
  std::unique_ptr<uint8_t[]> buffer(new (std::nothrow) uint8_t[4096]);
  ok = ok && buffer != nullptr;
  size_t received = 0;
  int previousPercent = -1;
  const uint32_t started = millis();
  while (ok && received < size_) {
    if (millis() - started > 180000) { ok = false; break; }
    const size_t remaining = size_ - received;
    const int count = download.read(reinterpret_cast<char*>(buffer.get()), remaining < 4096 ? remaining : 4096);
    ok = count > 0;
    if (!ok) break;
    ok = mbedtls_sha256_update(&hash, buffer.get(), count) == 0 &&
        esp_ota_write(handle, buffer.get(), count) == ESP_OK;
    received += count;
    const int percent = received * 100 / size_;
    if (percent != previousPercent) { progress("Installing...", percent); previousPercent = percent; }
    delay(1);
  }
  std::array<uint8_t, 32> actual{};
  ok = ok && received == size_ && mbedtls_sha256_finish(&hash, actual.data()) == 0 && actual == sha256_;
  mbedtls_sha256_free(&hash);
  if (!ok) {
    esp_ota_abort(handle);
    message_ = "Update failed. Try again.";
    return false;
  }
  progress("Verifying...", 100);
  // esp_ota_end checks the ESP image, including chip compatibility, before activating it.
  if (esp_ota_end(handle) != ESP_OK || esp_ota_set_boot_partition(partition) != ESP_OK) {
    message_ = "Verification failed"; return false;
  }
  progress("Restarting...", 100);
  delay(600);
  ESP.restart();
  return true;
}
