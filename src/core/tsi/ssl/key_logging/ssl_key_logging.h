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

#include <grpc/grpc_security.h>
#include <grpc/slice.h>
#include <grpc/support/sync.h>

extern "C" {
#include <openssl/ssl.h>
}

#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/sync.h"

/// Instance to facilitate loging of SSL/TLS session keys to aid debugging.
///
/// Keys logged by an instance of this class help decrypting packet captures
/// with tools like wireshark.
///
/// This class is thread safe and serializes access to keylog files.

namespace tsi {

/// Configuration for key logging.
struct tsi_tls_key_logger_config {
  /// The full path at which the TLS keys would be exported.
  std::string tls_key_log_file_path;
  /// The format in which the TLS keys would be exported.
  grpc_tls_key_log_format key_logging_format;

  /// Future extensions can include filters such as IP addresses etc.

  /// Constructor.
  explicit tsi_tls_key_logger_config()
      : tls_key_log_file_path(""),
        key_logging_format(grpc_tls_key_log_format::TLS_KEY_LOG_FORMAT_NONE){};

  /// Copy ctor.
  tsi_tls_key_logger_config(const tsi_tls_key_logger_config& copy) {
    tls_key_log_file_path = copy.tls_key_log_file_path;
    key_logging_format = copy.key_logging_format;
  }
};

/// A class which facilitates logging Tls keys into a file. The instance is
/// bound to a file.
class TlsKeyLogger : public grpc_core::RefCounted<TlsKeyLogger> {
 public:
  /// Instantiates a TlsKeyLogger instance bound to a specific path.
  explicit TlsKeyLogger(const std::string& tls_key_log_file_path);
  ~TlsKeyLogger() override;

  /// Not copyable nor assignable.
  TlsKeyLogger(const TlsKeyLogger&) = delete;
  TlsKeyLogger& operator=(const TlsKeyLogger&) = delete;

  /// Writes session keys into the file in the key logging format specified.
  /// The passed string may be modified and logged based on the specified
  /// format.
  /// This is called upon completion of a handshake. The associated ssl_context
  /// is also provided here to support future extensions such as logging
  /// keys only when connections are made by certain IPs etc.
  void LogSessionKeys(SSL_CTX* ssl_context,
                      const tsi_tls_key_logger_config& tls_key_log_config,
                      const std::string& session_keys_info);

 private:
  FILE* fd_;
  grpc_core::Mutex lock_;
  std::string tls_key_log_file_path_;
};

/// A Wrapper class which enables key logging to the a file based on specified
/// configuration. A unique KeyLogger Container is bound to each Tls
/// security connector.
class TlsKeyLoggerContainer
    : public grpc_core::RefCounted<TlsKeyLoggerContainer> {
 public:
  /// Creates a Key Logger Container bound to a specific key logger instance
  /// and a specific key logging configuration. The configuration may grow over
  /// time and currently only includes logging format.
  explicit TlsKeyLoggerContainer(
      const tsi_tls_key_logger_config& tls_key_log_config,
      grpc_core::RefCountedPtr<TlsKeyLogger> tls_key_logger_instance)
      : tls_key_logger_instance_(std::move(tls_key_logger_instance)),
        tls_key_log_config_(tls_key_log_config){};

  /// Not copyable nor assignable.
  TlsKeyLoggerContainer(const TlsKeyLoggerContainer&) = delete;
  TlsKeyLoggerContainer& operator=(const TlsKeyLoggerContainer&) = delete;

  /// Writes session keys into the file according to the bound configuration.
  /// This is called upon completion of a handshake. The associated ssl_context
  /// is also provided here to support future extensions such as logging
  /// keys only when connections are made by certain IPs etc.
  void LogSessionKeys(SSL_CTX* ssl_context,
                      const std::string& session_keys_info) {
    tls_key_logger_instance_->LogSessionKeys(ssl_context, tls_key_log_config_,
                                             session_keys_info);
  }

 private:
  grpc_core::RefCountedPtr<TlsKeyLogger> tls_key_logger_instance_;
  tsi_tls_key_logger_config tls_key_log_config_;
};

class TlsKeyLoggerRegistry {
 public:
  /// Creates a new TlsKeyloggerContainer instance for the specified in
  /// tls_key_logger_config. The TlsKeyLoggerContainer will re-use an existing
  /// TlsKeyLogger instance if one already exists for the specified file path
  /// in the tls_key_logger_config.
  static grpc_core::RefCountedPtr<TlsKeyLoggerContainer>
  CreateTlsKeyLoggerContainer(
      const tsi_tls_key_logger_config& tls_key_logger_config);

  /// Initializes the TlsKeyLoggerRegistry.
  static void Init();

  /// Cleans up some allocations made during the initialization process.
  static void Shutdown();
};

}  // namespace tsi

#endif /* GRPC_CORE_TSI_SSL_KEY_LOGGING_SSL_KEY_LOGGING_H */
