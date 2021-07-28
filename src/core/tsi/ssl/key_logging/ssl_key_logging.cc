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

#include <grpc/support/port_platform.h>

#include <map>

#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/tsi/ssl/key_logging/ssl_key_logging.h"

#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

namespace tsi {

#if OPENSSL_VERSION_NUMBER >= 0x10100000 && !defined(LIBRESSL_VERSION_NUMBER)
std::map<std::string, TlsKeyLogger*> g_tls_key_logger_map;
grpc_core::Mutex* g_tls_key_logger_registry_mu = nullptr;
#endif

TlsKeyLogger::TlsKeyLogger(const std::string& tls_key_log_file_path)
    : fd_(nullptr), tls_key_log_file_path_(tls_key_log_file_path) {
  GPR_ASSERT(!tls_key_log_file_path_.empty());
  fd_ = fopen(tls_key_log_file_path_.c_str(), "w+");
  grpc_error_handle error = GRPC_ERROR_NONE;

  if (fd_ == nullptr) {
    error = GRPC_OS_ERROR(errno, "fopen");
  }

  if (error != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR,
            "Disabling TLS Key logging. ERROR Opening TLS Keylog "
            "file: %s",
            grpc_error_std_string(error).c_str());
  }
};

TlsKeyLogger::~TlsKeyLogger() {
  if (fd_ != nullptr) fclose(fd_);
}

void TlsKeyLogger::LogSessionKeys(
    SSL_CTX* /* ssl_context */,
    const tsi_tls_key_logger_config& tls_key_log_config,
    const std::string& session_keys_info) {
  grpc_core::MutexLock lock(&lock_);

  if (fd_ == nullptr || session_keys_info.empty()) return;

  switch (tls_key_log_config.key_logging_format) {
    case TLS_KEY_LOG_FORMAT_NSS:
      // Append to keylog file under lock
      bool err;

      err = (fwrite(session_keys_info.c_str(), sizeof(char),
                    session_keys_info.length(),
                    fd_) < session_keys_info.length());

      // Append new-line character as well. Doing a separate syscall to avoid
      // dynamic memory allocation and modification of passed session_keys_info
      err |= (fwrite("\n", sizeof(char), 1, fd_) != 1);

      if (err) {
        grpc_error_handle error = GRPC_OS_ERROR(errno, "fwrite");
        gpr_log(GPR_ERROR, "Error Appending to TLS Keylog file: %s",
                grpc_error_std_string(error).c_str());
        fclose(fd_);
        fd_ = nullptr;  // disable future attempts to write to this file
      } else {
        fflush(fd_);
      }
      break;

    // For future extensions to support more/custom logging formats
    default:
      break;
  }
}

grpc_core::RefCountedPtr<TlsKeyLoggerContainer>
TlsKeyLoggerRegistry::CreateTlsKeyLoggerContainer(
    const tsi_tls_key_logger_config& tls_key_logger_config) {
#if OPENSSL_VERSION_NUMBER >= 0x10100000 && !defined(LIBRESSL_VERSION_NUMBER)
  // To control parallel access to registry
  grpc_core::MutexLock lock(g_tls_key_logger_registry_mu);
  if (tls_key_logger_config.tls_key_log_file_path.empty()) return nullptr;

  auto it =
      g_tls_key_logger_map.find(tls_key_logger_config.tls_key_log_file_path);
  if (it == g_tls_key_logger_map.end()) {
    // Create a new TlsKeyLogger instance

    // Sets Ref count of new_tls_key_logger to 1 to make sure the
    // g_tls_key_logger_map is an owner. Relevant TlsKeyLoggerContainers
    // which share this TlsKeyLogger instance also become owners.
    auto new_tls_key_logger = grpc_core::MakeRefCounted<TlsKeyLogger>(
                                  tls_key_logger_config.tls_key_log_file_path)
                                  .release();

    g_tls_key_logger_map.insert(std::pair<std::string, TlsKeyLogger*>(
        tls_key_logger_config.tls_key_log_file_path, new_tls_key_logger));

    // The key logger container also becomes an owner of the key logger
    // instance.
    auto new_tls_key_logger_container =
        grpc_core::MakeRefCounted<TlsKeyLoggerContainer>(
            tls_key_logger_config, new_tls_key_logger->Ref());
    return new_tls_key_logger_container;
  }

  // The key logger container also becomes an owner of the key logger
  // instance.
  auto new_tls_key_logger_container =
      grpc_core::MakeRefCounted<TlsKeyLoggerContainer>(tls_key_logger_config,
                                                       it->second->Ref());
  return new_tls_key_logger_container;
#endif

  return nullptr;
}

void TlsKeyLoggerRegistry::Init() {
#if OPENSSL_VERSION_NUMBER >= 0x10100000 && !defined(LIBRESSL_VERSION_NUMBER)
  g_tls_key_logger_map.clear();
  g_tls_key_logger_registry_mu = new grpc_core::Mutex();
#endif
}

void TlsKeyLoggerRegistry::Shutdown() {
#if OPENSSL_VERSION_NUMBER >= 0x10100000 && !defined(LIBRESSL_VERSION_NUMBER)
  // The map calls Unref on all allotted tls key loggers
  for (auto it = g_tls_key_logger_map.begin(); it != g_tls_key_logger_map.end();
       it++) {
    it->second->Unref();
  }
  g_tls_key_logger_map.clear();
  delete g_tls_key_logger_registry_mu;
#endif
}

};  // namespace tsi
