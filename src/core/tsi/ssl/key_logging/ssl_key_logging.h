/*
 *
 * Copyright 2021 gRPC authors.
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

#ifndef GRPC_CORE_TSI_SSL_KEY_LOGGING_SSL_KEY_LOGGING_H
#define GRPC_CORE_TSI_SSL_KEY_LOGGING_SSL_KEY_LOGGING_H

#include <grpc/support/port_platform.h>

#include <iostream>
#include <map>

#include <grpc/grpc_security.h>
#include <grpc/slice.h>
#include <grpc/support/sync.h>

extern "C" {
#include <openssl/ssl.h>
}

#include "absl/base/thread_annotations.h"

#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/sync.h"

namespace tsi {

class TlsSessionKeyLoggerCache {
 public:
  TlsSessionKeyLoggerCache() : ref_count_{0} {};
  ~TlsSessionKeyLoggerCache();

  void Ref() { ++ref_count_; }

  void Unref() {
    if (--ref_count_ == 0) {
      delete this;
    }
  }
  // A helper class which facilitates appending Tls session keys into a file.
  // The instance is bound to a file meaning only one instance of this object
  // can ever exist for a given file path.
  class TlsSessionKeyLogger
      : public grpc_core::RefCounted<TlsSessionKeyLogger> {
   public:
    // Instantiates a TlsSessionKeyLogger instance bound to a specific path.
    explicit TlsSessionKeyLogger(std::string tls_session_key_log_file_path,
                                 TlsSessionKeyLoggerCache* cache);
    ~TlsSessionKeyLogger() override;

    // Not copyable nor assignable.
    TlsSessionKeyLogger(const TlsSessionKeyLogger&) = delete;
    TlsSessionKeyLogger& operator=(const TlsSessionKeyLogger&) = delete;
    // Writes session keys into the file in the NSS key logging format.
    // This is called upon completion of a handshake. The associated ssl_context
    // is also provided here to support future extensions such as logging
    // keys only when connections are made by certain IPs etc.
    void LogSessionKeys(SSL_CTX* ssl_context,
                        const std::string& session_keys_info);

   private:
    FILE* fd_;
    grpc_core::Mutex lock_;  // protects appends to file
    std::string tls_session_key_log_file_path_;
    TlsSessionKeyLoggerCache* cache_;
  };
  // Creates and returns a TlsSessionKeyLogger instance.
  static grpc_core::RefCountedPtr<TlsSessionKeyLogger> Get(
      std::string tls_session_key_log_file_path);

 private:
  // Internal method which creates a new TlsSessionKeyLogger instance bound to
  // the specified file path.
  grpc_core::RefCountedPtr<TlsSessionKeyLogger> CreateTlsSessionKeyLogger(
      std::string tls_session_key_log_file_path);

  std::atomic<int> ref_count_;
  std::map<std::string, TlsSessionKeyLogger*> tls_session_key_logger_map_;
};

}  // namespace tsi

#endif  // GRPC_CORE_TSI_SSL_KEY_LOGGING_SSL_KEY_LOGGING_H
