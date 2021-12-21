/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include "src/core/lib/transport/timeout_encoding.h"

#include <stdio.h>
#include <string.h>

#include <grpc/support/log.h>

#include "src/core/lib/gpr/string.h"

namespace grpc_core {

namespace {

int64_t DivideRoundingUp(int64_t dividend, int64_t divisor) {
  return (dividend + divisor - 1) / divisor;
}

constexpr int64_t kSecondsPerMinute = 60;
constexpr int64_t kMinutesPerHour = 60;
constexpr int64_t kSecondsPerHour = kSecondsPerMinute * kMinutesPerHour;
constexpr int64_t kMaxHours = 27000;

bool IsAllSpace(const uint8_t* p, const uint8_t* end) {
  while (p != end && *p == ' ') p++;
  return p == end;
}

}  // namespace

Timeout Timeout::FromDuration(grpc_millis duration) {
  return Timeout::FromMillis(duration);
}

double Timeout::RatioVersus(Timeout other) const {
  double a = AsDuration();
  double b = other.AsDuration();
  if (b == 0) {
    if (a > 0) return 100;
    if (a < 0) return -100;
    return 0;
  }
  return 100 * (a / b - 1);
}

grpc_millis Timeout::AsDuration() const {
  grpc_millis value = value_;
  switch (unit_) {
    case Unit::kNanoseconds:
      return 0;
    case Unit::kMilliseconds:
      return value;
    case Unit::kTenMilliseconds:
      return value * 10;
    case Unit::kHundredMilliseconds:
      return value * 100;
    case Unit::kSeconds:
      return value * 1000;
    case Unit::kTenSeconds:
      return value * 10000;
    case Unit::kHundredSeconds:
      return value * 100000;
    case Unit::kMinutes:
      return value * 1000 * kSecondsPerMinute;
    case Unit::kTenMinutes:
      return value * 10000 * kSecondsPerMinute;
    case Unit::kHundredMinutes:
      return value * 100000 * kSecondsPerMinute;
    case Unit::kHours:
      return value * 1000 * kSecondsPerHour;
  }
  GPR_UNREACHABLE_CODE(return -1);
}

Slice Timeout::Encode() const {
  char buf[10];
  char* p = buf;
  uint16_t n = value_;
  int digits;
  if (n >= 10000) {
    digits = 5;
  } else if (n >= 1000) {
    digits = 4;
  } else if (n >= 100) {
    digits = 3;
  } else if (n >= 10) {
    digits = 2;
  } else {
    digits = 1;
  }
  switch (digits) {
    case 5:
      *p++ = '0' + n / 10000;
      n %= 10000;
      ABSL_FALLTHROUGH_INTENDED;
    case 4:
      *p++ = '0' + n / 1000;
      n %= 1000;
      ABSL_FALLTHROUGH_INTENDED;
    case 3:
      *p++ = '0' + n / 100;
      n %= 100;
      ABSL_FALLTHROUGH_INTENDED;
    case 2:
      *p++ = '0' + n / 10;
      n %= 10;
      ABSL_FALLTHROUGH_INTENDED;
    case 1:
      *p++ = '0' + n;
  }
  switch (unit_) {
    case Unit::kNanoseconds:
      *p++ = 'n';
      break;
    case Unit::kHundredMilliseconds:
      *p++ = '0';
      ABSL_FALLTHROUGH_INTENDED;
    case Unit::kTenMilliseconds:
      *p++ = '0';
      ABSL_FALLTHROUGH_INTENDED;
    case Unit::kMilliseconds:
      *p++ = 'm';
      break;
    case Unit::kHundredSeconds:
      *p++ = '0';
      ABSL_FALLTHROUGH_INTENDED;
    case Unit::kTenSeconds:
      *p++ = '0';
      ABSL_FALLTHROUGH_INTENDED;
    case Unit::kSeconds:
      *p++ = 'S';
      break;
    case Unit::kHundredMinutes:
      *p++ = '0';
      ABSL_FALLTHROUGH_INTENDED;
    case Unit::kTenMinutes:
      *p++ = '0';
      ABSL_FALLTHROUGH_INTENDED;
    case Unit::kMinutes:
      *p++ = 'M';
      break;
    case Unit::kHours:
      *p++ = 'H';
      break;
  }
  return Slice::FromCopiedBuffer(buf, p - buf);
}

Timeout Timeout::FromMillis(int64_t millis) {
  if (millis <= 0) {
    return Timeout(1, Unit::kNanoseconds);
  } else if (millis < 1000) {
    return Timeout(millis, Unit::kMilliseconds);
  } else if (millis < 10000) {
    int64_t value = DivideRoundingUp(millis, 10);
    if (value % 100 != 0) return Timeout(value, Unit::kTenMilliseconds);
  } else if (millis < 100000) {
    int64_t value = DivideRoundingUp(millis, 100);
    if (value % 10 != 0) return Timeout(value, Unit::kHundredMilliseconds);
  }
  return Timeout::FromSeconds(DivideRoundingUp(millis, 1000));
}

Timeout Timeout::FromSeconds(int64_t seconds) {
  GPR_DEBUG_ASSERT(seconds != 0);
  if (seconds < 1000) {
    if (seconds % kSecondsPerMinute != 0) {
      return Timeout(seconds, Unit::kSeconds);
    }
  } else if (seconds < 10000) {
    int64_t value = DivideRoundingUp(seconds, 10);
    if ((value * 10) % kSecondsPerMinute != 0) {
      return Timeout(value, Unit::kTenSeconds);
    }
  } else if (seconds < 100000) {
    int64_t value = DivideRoundingUp(seconds, 100);
    if ((value * 100) % kSecondsPerMinute != 0) {
      return Timeout(value, Unit::kHundredSeconds);
    }
  }
  return Timeout::FromMinutes(DivideRoundingUp(seconds, kSecondsPerMinute));
}

Timeout Timeout::FromMinutes(int64_t minutes) {
  GPR_DEBUG_ASSERT(minutes != 0);
  if (minutes < 1000) {
    if (minutes % kMinutesPerHour != 0) {
      return Timeout(minutes, Unit::kMinutes);
    }
  } else if (minutes < 10000) {
    int64_t value = DivideRoundingUp(minutes, 10);
    if ((value * 10) % kMinutesPerHour != 0) {
      return Timeout(value, Unit::kTenMinutes);
    }
  } else if (minutes < 100000) {
    int64_t value = DivideRoundingUp(minutes, 100);
    if ((value * 100) % kMinutesPerHour != 0) {
      return Timeout(value, Unit::kHundredMinutes);
    }
  }
  return Timeout::FromHours(DivideRoundingUp(minutes, kMinutesPerHour));
}

Timeout Timeout::FromHours(int64_t hours) {
  GPR_DEBUG_ASSERT(hours != 0);
  if (hours < kMaxHours) {
    return Timeout(hours, Unit::kHours);
  }
  return Timeout(kMaxHours, Unit::kHours);
}

absl::optional<grpc_millis> ParseTimeout(const Slice& text) {
  grpc_millis x = 0;
  const uint8_t* p = text.begin();
  const uint8_t* end = text.end();
  int have_digit = 0;
  /* skip whitespace */
  for (; p != end && *p == ' '; p++) {
  }
  /* decode numeric part */
  for (; p != end && *p >= '0' && *p <= '9'; p++) {
    int32_t digit = static_cast<int32_t>(*p - static_cast<uint8_t>('0'));
    have_digit = 1;
    /* spec allows max. 8 digits, but we allow values up to 1,000,000,000 */
    if (x >= (100 * 1000 * 1000)) {
      if (x != (100 * 1000 * 1000) || digit != 0) {
        return GRPC_MILLIS_INF_FUTURE;
      }
    }
    x = x * 10 + digit;
  }
  if (!have_digit) return absl::nullopt;
  /* skip whitespace */
  for (; p != end && *p == ' '; p++) {
  }
  if (p == end) return absl::nullopt;
  /* decode unit specifier */
  int64_t timeout;
  switch (*p) {
    case 'n':
      timeout = x / GPR_NS_PER_MS + (x % GPR_NS_PER_MS != 0);
      break;
    case 'u':
      timeout = x / GPR_US_PER_MS + (x % GPR_US_PER_MS != 0);
      break;
    case 'm':
      timeout = x;
      break;
    case 'S':
      timeout = x * GPR_MS_PER_SEC;
      break;
    case 'M':
      timeout = x * 60 * GPR_MS_PER_SEC;
      break;
    case 'H':
      timeout = x * 60 * 60 * GPR_MS_PER_SEC;
      break;
    default:
      return absl::nullopt;
  }
  p++;
  if (!IsAllSpace(p, end)) return absl::nullopt;
  return timeout;
}

}  // namespace grpc_core
