#pragma once

namespace epd {

/* Current public roots used by the major certificate chains serving chatgpt.com.
 * Keeping multiple PEM blocks is supported by mbedTLS and avoids setInsecure(). */
extern const char kTrustedRootCertificates[];

}  // namespace epd

