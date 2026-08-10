#pragma once

#include <Arduino.h>

#ifndef EPD_TOOLKIT_RELEASE
#define TOOLKIT_LOG(scope, value)              \
  do {                                         \
    Serial.print("[toolkit][");                \
    Serial.print(scope);                       \
    Serial.print("] ");                        \
    Serial.println(value);                     \
  } while (0)
#else
#define TOOLKIT_LOG(scope, value) ((void)0)
#endif
