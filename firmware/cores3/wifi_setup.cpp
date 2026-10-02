#include "wifi_setup.h"
#include <Preferences.h>
#include <WiFi.h>
#include <esp_random.h>

namespace {
String randomHex(size_t bytes) {
  String value;
  for (size_t i = 0; i < bytes; ++i) {
    char hex[3];
    snprintf(hex, sizeof(hex), "%02x", static_cast<unsigned>(esp_random() & 255));
    value += hex;
  }
  return value;
}

String escaped(const String& value) {
  return wifi_settings::escapeHtml(std::string_view(value.c_str(), value.length())).c_str();
}

const char* disconnectMessage(uint32_t reason) {
  switch (reason) {
    case WIFI_REASON_NO_AP_FOUND: return "Network not found";
    case WIFI_REASON_AUTH_FAIL: return "Authentication failed";
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT: return "Authentication timed out";
    case WIFI_REASON_ASSOC_FAIL: return "Association failed";
    case WIFI_REASON_BEACON_TIMEOUT: return "Wi-Fi signal lost";
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY: return "Security mismatch";
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD: return "Auth mode unsupported";
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD: return "Wi-Fi signal too weak";
    default: return "Connection failed";
  }
}
}  // namespace

bool WifiSetup::load(wifi_settings::Credentials& credentials) {
  Preferences preferences;
  if (!preferences.begin("tracker-wifi", true)) return false;
  const bool ok = preferences.getBytesLength("network") == sizeof(credentials) &&
      preferences.getBytes("network", &credentials, sizeof(credentials)) == sizeof(credentials) &&
      wifi_settings::valid(credentials);
  preferences.end();
  if (!ok) credentials = {};
  return ok;
}

bool WifiSetup::begin() {
  stop();
  // Keep trial credentials in RAM. Only a successful connection is saved in our NVS namespace.
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  if (!WiFi.mode(WIFI_AP_STA)) return false;
  apName_ = "Tracker-" + randomHex(3);
  apPassword_ = randomHex(6);
  token_ = randomHex(16);
  if (!WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                         IPAddress(255, 255, 255, 0)) ||
      !WiFi.softAP(apName_.c_str(), apPassword_.c_str(), 1, false, 1)) {
    stop();
    return false;
  }
  if (!eventsReady_) {
    // Registered once: WifiSetup has application lifetime. Do not access UI or Strings here.
    WiFi.onEvent([this](arduino_event_id_t event, arduino_event_info_t info) {
      if (!observing_.load()) return;
      if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) connectionEvent_.store(kAssociated);
      else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED)
        connectionEvent_.store(info.wifi_sta_disconnected.reason);
    });
    eventsReady_ = true;
  }
  if (!routesReady_) {
    const char* headers[] = {"Origin"};
    server_.collectHeaders(headers, 1);
    server_.on("/", HTTP_GET, [this] { if (allowedRequest()) showForm(); });
    server_.on("/save", HTTP_POST, [this] { if (allowedRequest()) submit(); });
    server_.on("/result", HTTP_GET, [this] { if (allowedRequest()) showResult(); });
    server_.onNotFound([this] { server_.send(404, "text/plain", "Not found"); });
    routesReady_ = true;
  }
  state_ = State::Ready;
  message_ = "Open the setup page";
  startedMs_ = millis();
  server_.begin();
  WiFi.scanNetworks(true);
  return true;
}

bool WifiSetup::allowedRequest() {
  // Restrict access to the setup interface; reject DNS rebinding and cross-origin requests.
  const String host = server_.hostHeader();
  const String origin = server_.header("Origin");
  if (server_.client().localIP() != IPAddress(192, 168, 4, 1) ||
      (host != "192.168.4.1" && host != "192.168.4.1:80") ||
      (origin.length() && origin != "http://192.168.4.1" && origin != "http://192.168.4.1:80")) {
    server_.send(403, "text/plain", "Access denied");
    return false;
  }
  return true;
}

void WifiSetup::sendPage(const String& body, int status, bool refresh) {
  server_.sendHeader("Cache-Control", "no-store");
  server_.sendHeader("X-Content-Type-Options", "nosniff");
  // A no-referrer form POST sends Origin: null, which allowedRequest() rejects.
  // Preserve the origin for our own form while withholding referrers from other sites.
  server_.sendHeader("Referrer-Policy", "same-origin");
  server_.sendHeader("Content-Security-Policy",
                     "default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; frame-ancestors 'none'; base-uri 'none'");
  String page = "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\">"
      "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>Wi-Fi setup</title>";
  if (refresh) page += "<meta http-equiv=\"refresh\" content=\"2;url=/result\">";
  page += "<style>body{font:18px system-ui;background:#101820;color:#fff;max-width:28rem;margin:2rem auto;padding:0 1rem}"
      "label{display:block;margin-top:1rem}input,button{box-sizing:border-box;width:100%;font:inherit;padding:.8rem;margin:.4rem 0;border-radius:.4rem}"
      "input{border:2px solid #b9c9dd}button{background:#245ce0;color:white;border:0;cursor:pointer;margin-top:1rem}"
      "a{color:#9dceff}:focus-visible{outline:3px solid #ffcf70;outline-offset:3px}small{display:block;line-height:1.5}</style>"
      "<h1>Wi-Fi setup</h1>";
  page += body;
  page += "</html>";
  server_.send(status, "text/html; charset=utf-8", page);
}

void WifiSetup::showForm() {
  if (state_ == State::Connecting || state_ == State::Saved) { showResult(); return; }
  String body = "<p>Connect your tracker to a 2.4 GHz network.</p>";
  if (state_ == State::Failed) body += String("<p role=\"alert\">") + message_ + "</p>";
  body += "<form method=\"post\" action=\"/save\"><input type=\"hidden\" name=\"token\" value=\"" + token_ +
      "\"><label for=\"ssid\">Network name</label><input id=\"ssid\" name=\"ssid\" list=\"networks\" maxlength=\"32\" required autocapitalize=\"none\" spellcheck=\"false\">"
      "<datalist id=\"networks\">";
  const int count = WiFi.scanComplete();
  for (int i = 0; i < count && i < 30; ++i) {
    body += "<option value=\"" + escaped(WiFi.SSID(i)) + "\"></option>";
  }
  body += "</datalist><small>Select a network or type its name.</small>"
      "<label for=\"password\">Password</label><input id=\"password\" name=\"password\" type=\"password\" maxlength=\"64\" autocomplete=\"new-password\">"
      "<small>Leave blank for an open network.</small><button>Connect &amp; save</button></form>"
      "<p>Saved on this tracker only.</p>";
  sendPage(body);
}

void WifiSetup::submit() {
  if (server_.arg("token") != token_) { sendPage("<p>Session expired. Reopen setup.</p>", 403); return; }
  if (state_ == State::Connecting || state_ == State::Saved) { showResult(); return; }
  const String ssid = server_.arg("ssid");
  const String password = server_.arg("password");
  if (!wifi_settings::valid(std::string_view(ssid.c_str(), ssid.length()),
                            std::string_view(password.c_str(), password.length()))) {
    sendPage("<p>Check the network name and password.</p><p><a href=\"/\">Try again</a></p>", 400);
    return;
  }
  pending_ = {};
  memcpy(pending_.ssid, ssid.c_str(), ssid.length());
  memcpy(pending_.password, password.c_str(), password.length());
  state_ = State::Connecting;
  message_ = "Connecting...";
  // Send the response before connecting: STA may move the AP to a different channel.
  connectPending_ = true;
  connectingMs_ = millis();
  showResult();
}

void WifiSetup::showResult() {
  if (state_ == State::Connecting) sendPage("<p role=\"status\">Connecting...</p><p>This may take 20 seconds.</p>", 200, true);
  else if (state_ == State::Saved) sendPage("<p>Wi-Fi saved.</p><p>You can close this page.</p>");
  else sendPage(String("<p role=\"alert\">") + message_ + "</p><p><a href=\"/\">Try again</a></p>");
}

void WifiSetup::poll() {
  if (state_ == State::Closed) return;
  server_.handleClient();
  uint32_t now = millis();
  if (connectPending_ && now - connectingMs_ >= 500 && WiFi.scanComplete() != WIFI_SCAN_RUNNING) {
    connectPending_ = false;
    observing_.store(false);
    WiFi.disconnect(false, false);
    connectionEvent_.store(0);
    observing_.store(true);
    // The Boolean API distinguishes a synchronous start failure from an in-progress attempt.
    const bool started = WiFi.STA.begin() && WiFi.STA.connect(pending_.ssid, pending_.password);
    // Refresh the poll timestamp after the blocking start call. Using the earlier
    // `now` with this later start time would underflow and immediately time out.
    now = millis();
    connectingMs_ = now;
    if (!started) {
      observing_.store(false);
      pending_ = {};
      state_ = State::Failed;
      message_ = "Could not start connection";
      Serial.println("Wi-Fi setup: connection start failed");
    }
  }
  if (state_ == State::Connecting && !connectPending_) {
    if (WiFi.status() == WL_CONNECTED) {
      observing_.store(false);
      Preferences preferences;
      bool ok = preferences.begin("tracker-wifi", false);
      if (ok) {
        ok = preferences.putBytes("network", &pending_, sizeof(pending_)) == sizeof(pending_);
        preferences.end();
      }
      pending_ = {};
      state_ = ok ? State::Saved : State::Failed;
      message_ = ok ? "Wi-Fi saved" : "Could not save. Try again.";
      savedMs_ = now;
    } else if (now - connectingMs_ >= 20000) {
      observing_.store(false);
      const uint32_t event = connectionEvent_.load();
      // Capture the real failure before our own disconnect generates another event.
      if (event == kAssociated || event == 0) {
        snprintf(failureMessage_, sizeof(failureMessage_), "%s",
                 event == kAssociated ? "No IP address received" : "Connection timed out");
      } else {
        snprintf(failureMessage_, sizeof(failureMessage_), "%s (%u)",
                 disconnectMessage(event), static_cast<unsigned>(event));
      }
      Serial.printf("Wi-Fi setup: %s; status=%d\n", failureMessage_, static_cast<int>(WiFi.status()));
      WiFi.disconnect(false, false);
      pending_ = {};
      state_ = State::Failed;
      message_ = failureMessage_;
    }
  }
  if (now - startedMs_ >= 300000 || (state_ == State::Saved && now - savedMs_ >= 8000)) stop();
}

void WifiSetup::stop() {
  observing_.store(false);
  server_.stop();
  WiFi.scanDelete();
  WiFi.mode(WIFI_OFF);
  pending_ = {};
  apPassword_ = "";
  token_ = "";
  connectPending_ = false;
  state_ = State::Closed;
}
