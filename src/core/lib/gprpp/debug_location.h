//
//
// Copyright 2017 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#ifndef GRPC_SRC_CORE_LIB_GPRPP_DEBUG_LOCATION_H
#define GRPC_SRC_CORE_LIB_GPRPP_DEBUG_LOCATION_H

#if defined(__has_builtin)
#if __has_builtin(__builtin_FILE)
#define GRPC_DEFAULT_FILE __builtin_FILE()
#endif
#endif
#ifndef GRPC_DEFAULT_FILE
#define GRPC_DEFAULT_FILE "<unknown>"
#endif
#if defined(__has_builtin)
#if __has_builtin(__builtin_LINE)
#define GRPC_DEFAULT_LINE __builtin_LINE()
#endif
#endif
#ifndef GRPC_DEFAULT_LINE
#define GRPC_DEFAULT_LINE -1
#endif

namespace grpc_core {

class SourceLocation {
 public:
  // NOLINTNEXTLINE
  SourceLocation(const char* file = GRPC_DEFAULT_FILE,
                 int line = GRPC_DEFAULT_LINE)
      : file_(file), line_(line) {}
  const char* file() const { return file_; }
  int line() const { return line_; }

 private:
  const char* file_;
  int line_;
};

// Used for tracking file and line where a call is made for debug builds.
// No-op for non-debug builds.
// Callers can use the DEBUG_LOCATION macro in either case.
#ifndef NDEBUG
class DebugLocation {
 public:
  DebugLocation(const char* file = GRPC_DEFAULT_FILE,
                int line = GRPC_DEFAULT_LINE)
      : location_(file, line) {}
  const char* file() const { return location_.file(); }
  int line() const { return location_.line(); }

 private:
  SourceLocation location_;
};
#else
class DebugLocation {
 public:
  DebugLocation() {}
  DebugLocation(const char* /* file */, int /* line */) {}
  const char* file() const { return nullptr; }
  int line() const { return -1; }
};
#endif

#define DEBUG_LOCATION ::grpc_core::DebugLocation(__FILE__, __LINE__)

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_GPRPP_DEBUG_LOCATION_H
