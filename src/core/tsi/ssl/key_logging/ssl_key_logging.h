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

// Tsi implementation of the session key log config.
// struct grpc_tls_session_key_log_config will wrap around this type.
struct TsiTlsSessionKeyLogConfig {
 public:
  TsiTlsSessionKeyLogConfig() = default;
  TsiTlsSessionKeyLogConfig(const TsiTlsSessionKeyLogConfig&) = default;
  void set_tls_session_key_log_file_path(std::string path) {
    tls_session_key_log_file_path_ = path;
  }
  void set_tls_session_key_logging_format(
      grpc_tls_session_key_log_format format) {
    session_key_logging_format_ = format;
  }
  std::string tls_session_key_log_file_path() {
    return tls_session_key_log_file_path_;
  }
  grpc_tls_session_key_log_format tls_session_key_logging_format() {
    return session_key_logging_format_;
  }
 private:
  std::string tls_session_key_log_file_path_;
  grpc_tls_session_key_log_format session_key_logging_format_;
};

// A helper class which facilitates appending Tls session keys into a file.
// The instance is bound to a file meaning only one instance of this object
// can ever exist for a given file path.
class TlsSessionKeyLogFileWriter :
    public grpc_core::RefCounted<TlsSessionKeyLogFileWriter> {
 public:
  // Instantiates a TlsSessionKeyLogger instance bound to a specific path.
  explicit TlsSessionKeyLogFileWriter(
      const std::string& tls_session_key_log_file_path);
  ~TlsSessionKeyLogFileWriter() override;

  // Not copyable nor assignable.
  TlsSessionKeyLogFileWriter(const TlsSessionKeyLogFileWriter&) = delete;
  TlsSessionKeyLogFileWriter& operator=(
      const TlsSessionKeyLogFileWriter&) = delete;

  // Writes session keys into the file in the key logging format specified.
  // The passed string may be modified and logged based on the specified
  // format.
  // This is called upon completion of a handshake. The associated ssl_context
  // is also provided here to support future extensions such as logging
  // keys only when connections are made by certain IPs etc.
  void AppendSessionKeys(
      SSL_CTX* ssl_context,
      TsiTlsSessionKeyLogConfig * tls_session_key_log_config,
      const std::string& session_keys_info);

 private:
  FILE* fd_;
  grpc_core::Mutex lock_;
  std::string tls_session_key_log_file_path_;
};

// A Wrapper class which enables key logging to the file based on specified
// configuration. A TlsSessionKeyLogger is created for each specified
// tls session key logging configuration. It may reuse an existing
// TlsSessionKeyLogFileWriter instance if one was already created for the file
// path specified in the configuration.
class TlsSessionKeyLogger : public grpc_core::RefCounted<TlsSessionKeyLogger> {
 public:
  // Creates a Key Logger bound to a specific sesison key logging configuration.
  // The configuration may grow over time and currently only includes
  // logging format and the log file path.
  TlsSessionKeyLogger(
      TsiTlsSessionKeyLogConfig tls_session_key_log_config,
      grpc_core::RefCountedPtr<
          TlsSessionKeyLogFileWriter>&& tls_key_log_file_writer)
      : tls_key_log_file_writer_(std::move(tls_key_log_file_writer)),
        tls_session_key_log_config_(tls_session_key_log_config){};

  // Not copyable nor assignable.
  TlsSessionKeyLogger(const TlsSessionKeyLogger&) = delete;
  TlsSessionKeyLogger& operator=(const TlsSessionKeyLogger&) = delete;

  // Writes session keys into the file according to the bound configuration.
  // This is called upon completion of a handshake. The associated ssl_context
  // is also provided here to support future extensions such as logging
  // keys only when connections are made by certain IPs etc.
  void LogSessionKeys(SSL_CTX* ssl_context,
                      const std::string& session_keys_info) {
    tls_key_log_file_writer_->AppendSessionKeys(
        ssl_context, &tls_session_key_log_config_, session_keys_info);
  }

 private:
  grpc_core::RefCountedPtr<
      TlsSessionKeyLogFileWriter> tls_key_log_file_writer_;
  TsiTlsSessionKeyLogConfig tls_session_key_log_config_;
};

class TlsSessionKeyLoggerRegistry {
 public:
  static grpc_core::Mutex tls_session_key_logger_registry_mu;
  static std::map<std::string, TlsSessionKeyLogFileWriter*>
    tls_session_key_log_file_writer_map
    ABSL_GUARDED_BY(&tls_session_key_logger_registry_mu);
  // Creates a new TlsSessionKeyLogger instance bound to the specified
  // configuration: tls_session_key_log_config.
  static TlsSessionKeyLogger*
  CreateTlsSessionKeyLogger(
      TsiTlsSessionKeyLogConfig tls_session_key_log_config);
};

}  // namespace tsi

#endif /* GRPC_CORE_TSI_SSL_KEY_LOGGING_SSL_KEY_LOGGING_H */
