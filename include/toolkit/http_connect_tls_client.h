#pragma once

#include <Arduino.h>
#include <WiFiClient.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#include "toolkit/config.h"

namespace epd {

// A minimal HTTP CONNECT transport for HTTPS. The proxy sees the destination
// host and TCP metadata, while the Codex request remains inside a TLS session
// that is authenticated against the chatgpt.com certificate chain.
class HttpConnectTlsClient final : public WiFiClient {
 public:
  enum class ErrorKind : uint8_t { kNone, kProxy, kTls };

  HttpConnectTlsClient(const HttpProxySettings& proxy, const char* root_ca,
                       uint32_t connect_timeout_ms = 10000);
  ~HttpConnectTlsClient() override;

  int connect(IPAddress ip, uint16_t port) override;
  int connect(IPAddress ip, uint16_t port, int32_t timeout_ms) override;
  int connect(const char* host, uint16_t port) override;
  int connect(const char* host, uint16_t port, int32_t timeout_ms) override;
  int setTimeout(uint32_t seconds) override;
  size_t write(uint8_t value) override;
  size_t write(const uint8_t* buffer, size_t size) override;
  int available() override;
  int read() override;
  int read(uint8_t* buffer, size_t size) override;
  int peek() override;
  void flush() override;
  void stop() override;
  uint8_t connected() override;
  operator bool() override { return connected() != 0; }

  ErrorKind errorKind() const { return error_kind_; }
  const String& lastError() const { return last_error_; }

 private:
  static int bioSend(void* context, const unsigned char* buffer, size_t size);
  static int bioReceive(void* context, unsigned char* buffer, size_t size);

  bool openTunnel(const char* target_host, uint16_t target_port);
  bool startTls(const char* target_host);
  bool waitForTlsIo(int result, uint32_t started_at) const;
  void initializeTls();
  void freeTls();
  int fail(ErrorKind kind, const String& message);
  int failTls(const char* stage, int code);

  const HttpProxySettings& proxy_;
  const char* root_ca_;
  uint32_t connect_timeout_ms_;
  WiFiClient tcp_;
  mbedtls_ssl_context ssl_;
  mbedtls_ssl_config ssl_config_;
  mbedtls_x509_crt ca_;
  mbedtls_ctr_drbg_context random_;
  mbedtls_entropy_context entropy_;
  String last_error_;
  ErrorKind error_kind_ = ErrorKind::kNone;
  bool tls_initialized_ = false;
  bool tls_ready_ = false;
  int peek_byte_ = -1;
};

}  // namespace epd
