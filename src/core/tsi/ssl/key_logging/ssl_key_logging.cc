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

#include "src/core/tsi/ssl/key_logging/ssl_key_logging.h"

#include <map>

#include <grpc/support/log.h>

#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/slice/slice_internal.h"

typedef ::tsi::TlsSessionKeyLoggerCache::TlsSessionKeyLogger
    TlsSessionKeyLogger;

static gpr_once g_cache_init = GPR_ONCE_INIT;
static grpc_core::Mutex* g_tls_session_key_log_cache_mu = nullptr;
// A pointer to a global singleton instance.
static ::tsi::TlsSessionKeyLoggerCache* g_cache_instance_
    ABSL_GUARDED_BY(g_tls_session_key_log_cache_mu) = nullptr;

static void do_cache_init(void) {
  g_tls_session_key_log_cache_mu = new grpc_core::Mutex();
  grpc_core::MutexLock lock(g_tls_session_key_log_cache_mu);
  g_cache_instance_ = new ::tsi::TlsSessionKeyLoggerCache();
}

namespace tsi {

TlsSessionKeyLoggerCache ::TlsSessionKeyLogger::TlsSessionKeyLogger(
    std::string tls_session_key_log_file_path, TlsSessionKeyLoggerCache* cache)
    : fd_(nullptr),
      tls_session_key_log_file_path_(std::move(tls_session_key_log_file_path)),
      cache_(cache) {
  GPR_ASSERT(!tls_session_key_log_file_path_.empty());
  GPR_ASSERT(cache_ != nullptr);
  cache_->Ref();
  fd_ = fopen(tls_session_key_log_file_path_.c_str(), "w+");
  if (fd_ == nullptr) {
    grpc_error_handle error = GRPC_OS_ERROR(errno, "fopen");
    gpr_log(GPR_ERROR,
            "Ignoring TLS Key logging. ERROR Opening TLS Keylog "
            "file: %s",
            grpc_error_std_string(error).c_str());
  }
};

TlsSessionKeyLoggerCache ::TlsSessionKeyLogger::~TlsSessionKeyLogger() {
  if (fd_ != nullptr) fclose(fd_);
  {
    grpc_core::MutexLock lock(g_tls_session_key_log_cache_mu);
    cache_->tls_session_key_logger_map_.erase(tls_session_key_log_file_path_);
  }
  cache_->Unref();
}

void TlsSessionKeyLoggerCache ::TlsSessionKeyLogger::LogSessionKeys(
    SSL_CTX* /* ssl_context */, const std::string& session_keys_info) {
  grpc_core::MutexLock lock(&lock_);

  if (fd_ == nullptr || session_keys_info.empty()) return;

  // Append to key log file under lock
  bool err;
  err = (fwrite((session_keys_info + "\r\n").c_str(), sizeof(char),
                session_keys_info.length() + 1,
                fd_) < session_keys_info.length());

  if (err) {
    grpc_error_handle error = GRPC_OS_ERROR(errno, "fwrite");
    gpr_log(GPR_ERROR, "Error Appending to TLS session key log file: %s",
            grpc_error_std_string(error).c_str());
    fclose(fd_);
    fd_ = nullptr;  // disable future attempts to write to this file
  } else {
    fflush(fd_);
  }
}

TlsSessionKeyLoggerCache::~TlsSessionKeyLoggerCache() {
  grpc_core::MutexLock lock(g_tls_session_key_log_cache_mu);
  g_cache_instance_ = nullptr;
}

grpc_core::RefCountedPtr<TlsSessionKeyLogger> TlsSessionKeyLoggerCache::Get(
    std::string tls_session_key_log_file_path) {
  gpr_once_init(&g_cache_init, do_cache_init);
  GPR_DEBUG_ASSERT(g_tls_session_key_log_cache_mu != nullptr);
  {
    grpc_core::MutexLock lock(g_tls_session_key_log_cache_mu);
    if (g_cache_instance_ == nullptr) {
      g_cache_instance_ = new TlsSessionKeyLoggerCache();
    }
    return g_cache_instance_->CreateTlsSessionKeyLogger(
        tls_session_key_log_file_path);
  }
}

grpc_core::RefCountedPtr<TlsSessionKeyLogger>
TlsSessionKeyLoggerCache::CreateTlsSessionKeyLogger(
    std::string tls_session_key_log_file_path) {
  if (tls_session_key_log_file_path.empty()) {
    return nullptr;
  }
  // Check if a TlsSessionKeyLogger instance already exists for the
  // specified file path.
  auto it = tls_session_key_logger_map_.find(tls_session_key_log_file_path);
  if (it == tls_session_key_logger_map_.end()) {
    // Create a new TlsSessionKeyLogger instance
    // Sets Ref count of new_tls_session_key_log_file_writer to 1 to make sure
    // the tls_session_key_logger_map_ is an owner. Relevant
    // TlsSessionKeyLogger objects which share this TlsSessionKeyLogger
    // instance also become owners.
    // The TlsSessionKeyLogger becomes an owner of the cache instance
    // which created it.
    auto new_tls_session_key_logger =
        grpc_core::MakeRefCounted<TlsSessionKeyLogger>(
            tls_session_key_log_file_path, this);

    // Add the instance to the map.
    tls_session_key_logger_map_.insert(
        std::pair<std::string, TlsSessionKeyLogger*>(
            tls_session_key_log_file_path, new_tls_session_key_logger.get()));
    return new_tls_session_key_logger;
  }
  return it->second->Ref();
}

};  // namespace tsi
