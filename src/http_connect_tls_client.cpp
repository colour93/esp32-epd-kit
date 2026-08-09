#include "toolkit/http_connect_tls_client.h"

#include <base64.h>
#include <mbedtls/error.h>

#include <algorithm>
#include <cstring>

namespace epd {

HttpConnectTlsClient::HttpConnectTlsClient(const HttpProxySettings& proxy,
                                           const char* root_ca,
                                           uint32_t connect_timeout_ms)
    : proxy_(proxy),
      root_ca_(root_ca),
      connect_timeout_ms_(connect_timeout_ms) {}

HttpConnectTlsClient::~HttpConnectTlsClient() { stop(); }

void HttpConnectTlsClient::initializeTls() {
  mbedtls_ssl_init(&ssl_);
  mbedtls_ssl_config_init(&ssl_config_);
  mbedtls_x509_crt_init(&ca_);
  mbedtls_ctr_drbg_init(&random_);
  mbedtls_entropy_init(&entropy_);
  tls_initialized_ = true;
}

void HttpConnectTlsClient::freeTls() {
  if (!tls_initialized_) return;
  mbedtls_ssl_free(&ssl_);
  mbedtls_ssl_config_free(&ssl_config_);
  mbedtls_x509_crt_free(&ca_);
  mbedtls_ctr_drbg_free(&random_);
  mbedtls_entropy_free(&entropy_);
  tls_initialized_ = false;
  tls_ready_ = false;
}

int HttpConnectTlsClient::fail(ErrorKind kind, const String& message) {
  error_kind_ = kind;
  last_error_ = message;
  stop();
  // stop() preserves the diagnostic fields for the caller.
  error_kind_ = kind;
  last_error_ = message;
  return 0;
}

int HttpConnectTlsClient::failTls(const char* stage, int code) {
  char detail[96]{};
  mbedtls_strerror(code, detail, sizeof(detail));
  return fail(ErrorKind::kTls,
              String("TLS ") + stage + " failed: " + detail);
}

bool HttpConnectTlsClient::openTunnel(const char* target_host,
                                      uint16_t target_port) {
  if (!tcp_.connect(proxy_.host.c_str(), proxy_.port,
                    static_cast<int32_t>(connect_timeout_ms_))) {
    fail(ErrorKind::kProxy, "HTTP proxy TCP connection failed");
    return false;
  }
  tcp_.setTimeout((connect_timeout_ms_ + 999U) / 1000U);

  String request = String("CONNECT ") + target_host + ':' + target_port +
                   " HTTP/1.1\r\nHost: " + target_host + ':' + target_port +
                   "\r\nProxy-Connection: Keep-Alive\r\n";
  if (!proxy_.username.isEmpty()) {
    String credentials =
        base64::encode(proxy_.username + ':' + proxy_.password);
    credentials.replace("\r", "");
    credentials.replace("\n", "");
    request += "Proxy-Authorization: Basic " + credentials + "\r\n";
  }
  request += "\r\n";
  if (tcp_.write(reinterpret_cast<const uint8_t*>(request.c_str()),
                 request.length()) != request.length()) {
    fail(ErrorKind::kProxy, "HTTP proxy CONNECT write failed");
    return false;
  }

  String response;
  response.reserve(256);
  const uint32_t started_at = millis();
  while (millis() - started_at <= connect_timeout_ms_) {
    while (tcp_.available()) {
      const int value = tcp_.read();
      if (value < 0) break;
      response += static_cast<char>(value);
      if (response.length() > 2048) {
        fail(ErrorKind::kProxy, "HTTP proxy response header is too large");
        return false;
      }
      if (response.endsWith("\r\n\r\n")) {
        const int line_end = response.indexOf("\r\n");
        const String status_line =
            line_end >= 0 ? response.substring(0, line_end) : response;
        const int first_space = status_line.indexOf(' ');
        const int status_code = first_space >= 0
                                    ? status_line.substring(first_space + 1,
                                                            first_space + 4)
                                          .toInt()
                                    : 0;
        if (status_code == 200) return true;
        if (status_code == 407) {
          fail(ErrorKind::kProxy, "HTTP proxy authentication failed (407)");
        } else {
          fail(ErrorKind::kProxy,
               String("HTTP proxy CONNECT failed: ") + status_code);
        }
        return false;
      }
    }
    if (!tcp_.connected()) break;
    delay(1);
  }
  fail(ErrorKind::kProxy, "HTTP proxy CONNECT timed out");
  return false;
}

int HttpConnectTlsClient::bioSend(void* context,
                                  const unsigned char* buffer, size_t size) {
  auto* tcp = static_cast<WiFiClient*>(context);
  const size_t written = tcp->write(buffer, size);
  return written > 0 ? static_cast<int>(written) : MBEDTLS_ERR_SSL_WANT_WRITE;
}

int HttpConnectTlsClient::bioReceive(void* context, unsigned char* buffer,
                                     size_t size) {
  auto* tcp = static_cast<WiFiClient*>(context);
  if (tcp->available() <= 0) {
    return tcp->connected() ? MBEDTLS_ERR_SSL_WANT_READ : 0;
  }
  const int received = tcp->read(buffer, size);
  return received >= 0 ? received : MBEDTLS_ERR_SSL_WANT_READ;
}

bool HttpConnectTlsClient::waitForTlsIo(int result,
                                        uint32_t started_at) const {
  if (result != MBEDTLS_ERR_SSL_WANT_READ &&
      result != MBEDTLS_ERR_SSL_WANT_WRITE) {
    return false;
  }
  if (millis() - started_at > connect_timeout_ms_) return false;
  delay(1);
  return true;
}

bool HttpConnectTlsClient::startTls(const char* target_host) {
  initializeTls();
  static constexpr char kPersonalization[] = "esp32-epd-http-proxy";
  int result = mbedtls_ctr_drbg_seed(
      &random_, mbedtls_entropy_func, &entropy_,
      reinterpret_cast<const unsigned char*>(kPersonalization),
      sizeof(kPersonalization) - 1U);
  if (result != 0) return failTls("random seed", result) != 0;

  result = mbedtls_ssl_config_defaults(
      &ssl_config_, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
      MBEDTLS_SSL_PRESET_DEFAULT);
  if (result != 0) return failTls("configuration", result) != 0;
  mbedtls_ssl_conf_authmode(&ssl_config_, MBEDTLS_SSL_VERIFY_REQUIRED);
  mbedtls_ssl_conf_rng(&ssl_config_, mbedtls_ctr_drbg_random, &random_);

  result = mbedtls_x509_crt_parse(
      &ca_, reinterpret_cast<const unsigned char*>(root_ca_),
      strlen(root_ca_) + 1U);
  if (result < 0) return failTls("CA parse", result) != 0;
  mbedtls_ssl_conf_ca_chain(&ssl_config_, &ca_, nullptr);

  result = mbedtls_ssl_setup(&ssl_, &ssl_config_);
  if (result != 0) return failTls("setup", result) != 0;
  result = mbedtls_ssl_set_hostname(&ssl_, target_host);
  if (result != 0) return failTls("hostname", result) != 0;
  mbedtls_ssl_set_bio(&ssl_, &tcp_, bioSend, bioReceive, nullptr);

  const uint32_t started_at = millis();
  while ((result = mbedtls_ssl_handshake(&ssl_)) != 0) {
    if (!waitForTlsIo(result, started_at)) {
      if (result == MBEDTLS_ERR_SSL_WANT_READ ||
          result == MBEDTLS_ERR_SSL_WANT_WRITE) {
        return fail(ErrorKind::kTls, "TLS handshake timed out") != 0;
      }
      return failTls("handshake", result) != 0;
    }
  }
  const uint32_t verify_flags = mbedtls_ssl_get_verify_result(&ssl_);
  if (verify_flags != 0) {
    return fail(ErrorKind::kTls, "TLS certificate verification failed") != 0;
  }
  tls_ready_ = true;
  return true;
}

int HttpConnectTlsClient::connect(IPAddress, uint16_t) {
  return fail(ErrorKind::kTls,
              "TLS proxy transport requires a hostname for verification");
}

int HttpConnectTlsClient::connect(IPAddress ip, uint16_t port,
                                  int32_t timeout_ms) {
  if (timeout_ms > 0) connect_timeout_ms_ = timeout_ms;
  return connect(ip, port);
}

int HttpConnectTlsClient::connect(const char* host, uint16_t port) {
  stop();
  last_error_ = "";
  error_kind_ = ErrorKind::kNone;
  peek_byte_ = -1;
  if (host == nullptr || *host == '\0') {
    return fail(ErrorKind::kTls, "missing TLS target hostname");
  }
  if (!openTunnel(host, port)) return 0;
  if (!startTls(host)) return 0;
  return 1;
}

int HttpConnectTlsClient::connect(const char* host, uint16_t port,
                                  int32_t timeout_ms) {
  if (timeout_ms > 0) connect_timeout_ms_ = timeout_ms;
  return connect(host, port);
}

int HttpConnectTlsClient::setTimeout(uint32_t seconds) {
  Stream::setTimeout(seconds * 1000U);
  return tcp_.setTimeout(seconds);
}

size_t HttpConnectTlsClient::write(uint8_t value) {
  return write(&value, 1);
}

size_t HttpConnectTlsClient::write(const uint8_t* buffer, size_t size) {
  if (!tls_ready_ || buffer == nullptr) return 0;
  size_t total = 0;
  const uint32_t started_at = millis();
  while (total < size && millis() - started_at <= connect_timeout_ms_) {
    const int result =
        mbedtls_ssl_write(&ssl_, buffer + total, size - total);
    if (result > 0) {
      total += static_cast<size_t>(result);
      continue;
    }
    if (result != MBEDTLS_ERR_SSL_WANT_READ &&
        result != MBEDTLS_ERR_SSL_WANT_WRITE) {
      break;
    }
    delay(1);
  }
  return total;
}

int HttpConnectTlsClient::available() {
  if (!tls_ready_) return 0;
  const size_t decrypted = mbedtls_ssl_get_bytes_avail(&ssl_);
  const size_t ready = decrypted > 0 ? decrypted : tcp_.available() > 0 ? 1U : 0U;
  return static_cast<int>(std::min<size_t>(INT_MAX,
                                           ready + (peek_byte_ >= 0 ? 1U : 0U)));
}

int HttpConnectTlsClient::read() {
  if (peek_byte_ >= 0) {
    const int value = peek_byte_;
    peek_byte_ = -1;
    return value;
  }
  uint8_t value = 0;
  return read(&value, 1) == 1 ? value : -1;
}

int HttpConnectTlsClient::read(uint8_t* buffer, size_t size) {
  if (!tls_ready_ || buffer == nullptr || size == 0) return -1;
  size_t copied = 0;
  if (peek_byte_ >= 0) {
    buffer[copied++] = static_cast<uint8_t>(peek_byte_);
    peek_byte_ = -1;
    if (copied == size) return static_cast<int>(copied);
  }
  const int result = mbedtls_ssl_read(&ssl_, buffer + copied, size - copied);
  if (result > 0) return static_cast<int>(copied + result);
  if (result == MBEDTLS_ERR_SSL_WANT_READ ||
      result == MBEDTLS_ERR_SSL_WANT_WRITE) {
    return static_cast<int>(copied);
  }
  if (result == 0 || result == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
    tls_ready_ = false;
  }
  return copied > 0 ? static_cast<int>(copied) : -1;
}

int HttpConnectTlsClient::peek() {
  if (peek_byte_ < 0) peek_byte_ = read();
  return peek_byte_;
}

void HttpConnectTlsClient::flush() {}

void HttpConnectTlsClient::stop() {
  if (tls_ready_) mbedtls_ssl_close_notify(&ssl_);
  tls_ready_ = false;
  peek_byte_ = -1;
  tcp_.stop();
  freeTls();
}

uint8_t HttpConnectTlsClient::connected() {
  if (!tls_ready_) return 0;
  return tcp_.connected() || mbedtls_ssl_get_bytes_avail(&ssl_) > 0 ||
                 peek_byte_ >= 0
             ? 1
             : 0;
}

}  // namespace epd
