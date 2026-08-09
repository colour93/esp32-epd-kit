#include "toolkit/codex_usage_client.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "toolkit/certificates.h"
#include "toolkit/core_logic.h"
#include "toolkit/http_connect_tls_client.h"

namespace epd {

RateLimitWindow CodexUsageClient::parseWindow(JsonVariantConst value) {
  RateLimitWindow window;
  if (!value.is<JsonObjectConst>()) return window;
  window.used_percent = value["used_percent"] | 0.0F;
  window.limit_window_seconds = value["limit_window_seconds"] | 0U;
  window.reset_after_seconds = value["reset_after_seconds"] | 0U;
  window.reset_at = value["reset_at"] | 0ULL;
  window.present = window.limit_window_seconds > 0;
  window.kind = core::identifyWindow(window.limit_window_seconds);
  return window;
}

void CodexUsageClient::assignWindow(const RateLimitWindow& window,
                                    CodexUsageState& state) {
  if (!window.present) return;
  switch (window.kind) {
    case core::WindowKind::kFiveHours:
      state.five_hour = window;
      break;
    case core::WindowKind::kWeekly:
      state.weekly = window;
      break;
    case core::WindowKind::kUnknown:
      if (!state.unknown.present) state.unknown = window;
      break;
  }
}

SyncStatus CodexUsageClient::fetch(const CodexSettings& settings,
                                   const String& locale,
                                   CodexUsageState& state) {
  if (settings.account_id.isEmpty() || settings.access_token.isEmpty()) {
    state.status_detail = "missing credentials";
    return state.status = SyncStatus::kAuthExpired;
  }
  const uint64_t now = static_cast<uint64_t>(time(nullptr));
  if (settings.expires_at > 0 && now >= settings.expires_at) {
    state.status_detail = "access token expired";
    return state.status = SyncStatus::kAuthExpired;
  }

  WiFiClientSecure direct_tls;
  HttpConnectTlsClient proxy_tls(settings.proxy, kTrustedRootCertificates,
                                 10000);
  WiFiClient* transport = nullptr;
  if (settings.proxy.enabled) {
    transport = &proxy_tls;
  } else {
    direct_tls.setCACert(kTrustedRootCertificates);
    direct_tls.setTimeout(10);
    transport = &direct_tls;
  }

  HTTPClient http;
  http.setConnectTimeout(10000);
  http.setTimeout(10000);
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!http.begin(*transport, kEndpoint)) {
    state.status_detail = "TLS client initialization failed";
    return state.status = SyncStatus::kTlsError;
  }

  http.addHeader("Authorization", "Bearer " + settings.access_token);
  http.addHeader("chatgpt-account-id", settings.account_id);
  http.addHeader("openai-beta", "codex-1");
  http.addHeader("Accept", "application/json");
  http.addHeader("oai-language", locale);
  http.addHeader("originator", "Codex Desktop");
  http.setUserAgent("esp32-epd-kit/" EPD_TOOLKIT_VERSION);

  const int status_code = http.GET();
  if (status_code <= 0) {
    if (settings.proxy.enabled && !proxy_tls.lastError().isEmpty()) {
      state.status_detail = proxy_tls.lastError();
      const SyncStatus failure =
          proxy_tls.errorKind() == HttpConnectTlsClient::ErrorKind::kProxy
              ? SyncStatus::kProxyError
              : SyncStatus::kTlsError;
      http.end();
      return state.status = failure;
    }
    state.status_detail = http.errorToString(status_code);
    http.end();
    return state.status = SyncStatus::kTlsError;
  }
  if (status_code == HTTP_CODE_UNAUTHORIZED) {
    state.status_detail = "HTTP 401";
    http.end();
    return state.status = SyncStatus::kAuthExpired;
  }
  if (status_code == HTTP_CODE_FORBIDDEN) {
    state.status_detail = "HTTP 403";
    http.end();
    return state.status = SyncStatus::kForbidden;
  }
  if (status_code == HTTP_CODE_TOO_MANY_REQUESTS) {
    state.status_detail = "HTTP 429";
    http.end();
    return state.status = SyncStatus::kThrottled;
  }
  if (status_code >= 300 && status_code < 400) {
    state.status_detail = "redirect refused";
    http.end();
    return state.status = SyncStatus::kProtocolError;
  }
  if (status_code != HTTP_CODE_OK) {
    state.status_detail = "HTTP " + String(status_code);
    http.end();
    return state.status = SyncStatus::kProtocolError;
  }

  const int announced_length = http.getSize();
  if (announced_length > static_cast<int>(kMaxResponseBytes)) {
    state.status_detail = "response exceeds 16 KiB";
    http.end();
    return state.status = SyncStatus::kProtocolError;
  }

  String body;
  body.reserve(announced_length > 0 ? announced_length : 2048);
  WiFiClient* stream = http.getStreamPtr();
  uint32_t last_data_at = millis();
  while (http.connected() || stream->available()) {
    while (stream->available()) {
      const int value = stream->read();
      if (value < 0) break;
      if (body.length() >= kMaxResponseBytes) {
        state.status_detail = "response exceeds 16 KiB";
        http.end();
        return state.status = SyncStatus::kProtocolError;
      }
      body += static_cast<char>(value);
      last_data_at = millis();
    }
    if (announced_length >= 0 && body.length() >= static_cast<size_t>(announced_length)) break;
    if (millis() - last_data_at > 10000) break;
    delay(1);
  }
  http.end();

  JsonDocument document;
  const DeserializationError parse_error = deserializeJson(document, body);
  if (parse_error) {
    state.status_detail = String("invalid JSON: ") + parse_error.c_str();
    return state.status = SyncStatus::kProtocolError;
  }

  CodexUsageState parsed = state;
  parsed.five_hour = {};
  parsed.weekly = {};
  parsed.unknown = {};
  parsed.additional[0] = {};
  parsed.additional[1] = {};
  parsed.plan_type = document["plan_type"] | "unknown";
  JsonVariantConst rate_limit = document["rate_limit"];
  parsed.allowed = rate_limit["allowed"] | true;
  parsed.limit_reached = rate_limit["limit_reached"] | false;
  assignWindow(parseWindow(rate_limit["primary_window"]), parsed);
  assignWindow(parseWindow(rate_limit["secondary_window"]), parsed);

  JsonArrayConst additional = document["additional_rate_limits"].as<JsonArrayConst>();
  size_t additional_index = 0;
  for (JsonVariantConst value : additional) {
    if (additional_index >= 2) break;
    AdditionalRateLimit& item = parsed.additional[additional_index++];
    item.present = true;
    item.name = value["limit_name"] | "";
    item.metered_feature = value["metered_feature"] | "";
    item.primary = parseWindow(value["rate_limit"]["primary_window"]);
    item.secondary = parseWindow(value["rate_limit"]["secondary_window"]);
  }

  parsed.has_data = parsed.five_hour.present || parsed.weekly.present ||
                    parsed.unknown.present;
  if (!parsed.has_data) {
    state.status_detail = "response contains no rate-limit windows";
    return state.status = SyncStatus::kProtocolError;
  }
  parsed.synced_at = now;
  parsed.status_detail = "";
  parsed.status = SyncStatus::kOk;
  state = parsed;
  return state.status;
}

}  // namespace epd
