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
std::map<std::string, TlsKeyLogFileWriter*> g_tls_key_log_file_writer_map;
grpc_core::Mutex* g_tls_key_logger_registry_mu = nullptr;
static std::atomic<bool> g_tls_key_logger_registry_initialized(false);
#endif

TlsKeyLogFileWriter::TlsKeyLogFileWriter(
    const std::string& tls_key_log_file_path)
    : fd_(nullptr), tls_key_log_file_path_(tls_key_log_file_path) {
  GPR_ASSERT(!tls_key_log_file_path_.empty());
  fd_ = fopen(tls_key_log_file_path_.c_str(), "w+");
  grpc_error_handle error = GRPC_ERROR_NONE;

  if (fd_ == nullptr) {
    error = GRPC_OS_ERROR(errno, "fopen");
  }

  if (error != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR,
            "Ignoring TLS Key logging. ERROR Opening TLS Keylog "
            "file: %s", grpc_error_std_string(error).c_str());
  }
};

TlsKeyLogFileWriter::~TlsKeyLogFileWriter() {
  if (fd_ != nullptr) fclose(fd_);
}

void TlsKeyLogFileWriter::AppendSessionKeys(
    SSL_CTX* /* ssl_context */,
    const TsiTlsKeyLoggerConfig& tsi_tls_key_log_config,
    const std::string& session_keys_info) {
  grpc_core::MutexLock lock(&lock_);

  if (fd_ == nullptr || session_keys_info.empty()) return;

  switch (tsi_tls_key_log_config.key_logging_format) {
    case GRPC_TLS_KEY_LOG_FORMAT_NSS:
      // Append to key log file under lock
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

grpc_core::RefCountedPtr<TlsKeyLogger>
TlsKeyLoggerRegistry::CreateTlsKeyLogger(
    const TsiTlsKeyLoggerConfig& tsi_tls_key_log_config) {
#if OPENSSL_VERSION_NUMBER >= 0x10100000 && !defined(LIBRESSL_VERSION_NUMBER)
  if (!g_tls_key_logger_registry_initialized.load()) {
    return nullptr;
  }
  // To control parallel access to registry
  grpc_core::MutexLock lock(g_tls_key_logger_registry_mu);
  if (tsi_tls_key_log_config.tls_key_log_file_path.empty()) return nullptr;

  // Check if a TlsKeyLogFileWriter instance already exists for the specified
  // file path.
  auto it = g_tls_key_log_file_writer_map.find(
      tsi_tls_key_log_config.tls_key_log_file_path);
  if (it == g_tls_key_log_file_writer_map.end()) {
    // Create a new TlsKeyLogFileWriter instance

    // Sets Ref count of new_tls_key_log_file_writer to 1 to make sure the
    // g_tls_key_log_file_writer_map is an owner.Relevant TlsKeyLogger objects
    // which share this TlsKeyLogFileWriter instance also become owners.
    auto new_tls_key_log_file_writer =
        grpc_core::MakeRefCounted<TlsKeyLogFileWriter>(
                                  tsi_tls_key_log_config.tls_key_log_file_path)
                                  .release();

    g_tls_key_log_file_writer_map.insert(
        std::pair<std::string, TlsKeyLogFileWriter*>(
            tsi_tls_key_log_config.tls_key_log_file_path,
            new_tls_key_log_file_writer));

    // The key logger also becomes an owner of the key log file writer
    // instance.
    auto new_tls_key_logger =
        grpc_core::MakeRefCounted<TlsKeyLogger>(
            tsi_tls_key_log_config, new_tls_key_log_file_writer->Ref());
    return new_tls_key_logger;
  }

  // The key logger also becomes an owner of the key log file writer
  // instance.
  auto new_tls_key_logger =
      grpc_core::MakeRefCounted<TlsKeyLogger>(tsi_tls_key_log_config,
                                              it->second->Ref());
  return new_tls_key_logger;
#endif
  return nullptr;
}

void TlsKeyLoggerRegistry::Init() {
#if OPENSSL_VERSION_NUMBER >= 0x10100000 && !defined(LIBRESSL_VERSION_NUMBER)
  if (!g_tls_key_logger_registry_initialized.exchange(true)) {
    g_tls_key_log_file_writer_map.clear();
    g_tls_key_logger_registry_mu = new grpc_core::Mutex();
  }
#endif
}

void TlsKeyLoggerRegistry::Shutdown() {
#if OPENSSL_VERSION_NUMBER >= 0x10100000 && !defined(LIBRESSL_VERSION_NUMBER)
  if (g_tls_key_logger_registry_initialized.exchange(false)) {
    // The map calls Unref on all allotted tls key loggers
    for (auto it = g_tls_key_log_file_writer_map.begin();
         it != g_tls_key_log_file_writer_map.end(); it++) {
      it->second->Unref();
    }
    g_tls_key_log_file_writer_map.clear();
    delete g_tls_key_logger_registry_mu;
  }
#endif
}

};  // namespace tsi
