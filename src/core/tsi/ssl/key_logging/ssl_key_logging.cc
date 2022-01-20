// Copyright 2021 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/tsi/ssl/key_logging/ssl_key_logging.h"

#include <map>

#include <grpc/support/log.h>

#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/slice/slice_internal.h"

using TlsSessionKeyLogger = tsi::TlsSessionKeyLoggerCache::TlsSessionKeyLogger;

namespace tsi {

namespace {

gpr_once g_cache_mutex_init = GPR_ONCE_INIT;
grpc_core::Mutex* g_tls_session_key_log_cache_mu = nullptr;
// A pointer to a global singleton instance.
TlsSessionKeyLoggerCache* g_cache_instance
    ABSL_GUARDED_BY(g_tls_session_key_log_cache_mu) = nullptr;

void do_cache_mutex_init(void) {
  g_tls_session_key_log_cache_mu = new grpc_core::Mutex();
}

}  // namespace

TlsSessionKeyLoggerCache::TlsSessionKeyLogger::TlsSessionKeyLogger(
    std::string tls_session_key_log_file_path,
    grpc_core::RefCountedPtr<TlsSessionKeyLoggerCache> cache)
    : tls_session_key_log_file_path_(std::move(tls_session_key_log_file_path)),
      cache_(std::move(cache)) {
  GPR_ASSERT(!tls_session_key_log_file_path_.empty());
  GPR_ASSERT(cache_ != nullptr);
  fd_ = fopen(tls_session_key_log_file_path_.c_str(), "w+");
  if (fd_ == nullptr) {
    grpc_error_handle error = GRPC_OS_ERROR(errno, "fopen");
    gpr_log(GPR_ERROR,
            "Ignoring TLS Key logging. ERROR Opening TLS Keylog "
            "file: %s",
            grpc_error_std_string(error).c_str());
  }
  cache_->tls_session_key_logger_map_.emplace(tls_session_key_log_file_path_,
                                              this);
};

TlsSessionKeyLoggerCache::TlsSessionKeyLogger::~TlsSessionKeyLogger() {
  {
    grpc_core::MutexLock lock(&lock_);
    if (fd_ != nullptr) fclose(fd_);
  }
  {
    grpc_core::MutexLock lock(g_tls_session_key_log_cache_mu);
    auto it = cache_->tls_session_key_logger_map_.find(
        tls_session_key_log_file_path_);
    if (it != cache_->tls_session_key_logger_map_.end() && it->second == this) {
      cache_->tls_session_key_logger_map_.erase(it);
    }
  }
}

void TlsSessionKeyLoggerCache::TlsSessionKeyLogger::LogSessionKeys(
    SSL_CTX* /* ssl_context */, const std::string& session_keys_info) {
  grpc_core::MutexLock lock(&lock_);
  if (fd_ == nullptr || session_keys_info.empty()) return;
  // Append to key log file under lock
  bool err =
      fwrite((session_keys_info + "\r\n").c_str(), sizeof(char),
             session_keys_info.length() + 1, fd_) < session_keys_info.length();

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

TlsSessionKeyLoggerCache::TlsSessionKeyLoggerCache()
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(g_tls_session_key_log_cache_mu) {
  g_cache_instance = this;
}

TlsSessionKeyLoggerCache::~TlsSessionKeyLoggerCache() {
  grpc_core::MutexLock lock(g_tls_session_key_log_cache_mu);
  g_cache_instance = nullptr;
}

grpc_core::RefCountedPtr<TlsSessionKeyLogger> TlsSessionKeyLoggerCache::Get(
    std::string tls_session_key_log_file_path) {
  gpr_once_init(&g_cache_mutex_init, do_cache_mutex_init);
  GPR_DEBUG_ASSERT(g_tls_session_key_log_cache_mu != nullptr);
  if (tls_session_key_log_file_path.empty()) {
    return nullptr;
  }
  {
    grpc_core::MutexLock lock(g_tls_session_key_log_cache_mu);
    grpc_core::RefCountedPtr<TlsSessionKeyLoggerCache> cache;
    if (g_cache_instance == nullptr) {
      // This will automatically set g_cache_instance.
      cache = grpc_core::MakeRefCounted<TlsSessionKeyLoggerCache>();
    } else {
      cache = g_cache_instance->Ref();
    }
    // Check cache for entry.
    auto it =
        cache->tls_session_key_logger_map_.find(tls_session_key_log_file_path);
    if (it != cache->tls_session_key_logger_map_.end()) {
      // Avoid a race condition if the destructor of the tls key logger
      // of interest is currently executing.
      auto key_logger = it->second->RefIfNonZero();
      if (key_logger != nullptr) return key_logger;
    }
    // Not found in cache, so create new entry.
    // This will automatically add itself to tls_session_key_logger_map_.
    return grpc_core::MakeRefCounted<TlsSessionKeyLogger>(
        std::move(tls_session_key_log_file_path), std::move(cache));
  }
}

};  // namespace tsi
