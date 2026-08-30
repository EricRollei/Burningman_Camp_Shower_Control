#pragma once

#include <Arduino.h>
#include <FS.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace StorageWrite {

inline bool exact(File& file, const char* data, size_t length) {
  return file && data != nullptr &&
         file.write(reinterpret_cast<const uint8_t*>(data), length) == length;
}

inline bool line(File& file, const char* value) {
  if (value == nullptr) return false;
  return exact(file, value, strlen(value)) && exact(file, "\n", 1);
}

// All persisted CSV records in this firmware are deliberately short. Keeping
// formatting on the stack lets us compare the requested and actual byte counts
// instead of trusting Print::printf(), which can return a short write.
inline bool formatted(File& file, const char* format, ...) {
  char buffer[256];
  va_list args;
  va_start(args, format);
  const int length = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  return length >= 0 && static_cast<size_t>(length) < sizeof(buffer) &&
         exact(file, buffer, static_cast<size_t>(length));
}

}  // namespace StorageWrite
