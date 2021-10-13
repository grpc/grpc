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

typedef ::tsi::TlsSessionKeyLogFileWriterCache::TlsSessionKeyLogger
  TlsSessionKeyLogger;

static gpr_once g_cache_init = GPR_ONCE_INIT;
static grpc_core::Mutex* g_tls_session_key_log_cache_mu = nullptr;
// A pointer to a global singleton instance.
static ::tsi::TlsSessionKeyLogFileWriterCache* g_cache_instance_
  ABSL_GUARDED_BY(g_tls_session_key_log_cache_mu) = nullptr;

static void do_cache_init(void) {
  g_tls_session_key_log_cache_mu = new grpc_core::Mutex();
  grpc_core::MutexLock lock(g_tls_session_key_log_cache_mu);
  g_cache_instance_ = new ::tsi::TlsSessionKeyLogFileWriterCache();

}

namespace tsi {

TlsSessionKeyLogFileWriterCache
  ::TlsSessionKeyLogFileWriter::TlsSessionKeyLogFileWriter(
    std::string tls_session_key_log_file_path,
    TlsSessionKeyLogFileWriterCache* cache) : fd_(nullptr),
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
            "file: %s", grpc_error_std_string(error).c_str());
  }
};

TlsSessionKeyLogFileWriterCache
  ::TlsSessionKeyLogFileWriter::~TlsSessionKeyLogFileWriter() {
  if (fd_ != nullptr) fclose(fd_);
  {
    grpc_core::MutexLock lock(g_tls_session_key_log_cache_mu);
    cache_->tls_session_key_log_file_writer_map.erase(
      tls_session_key_log_file_path_);
  }
  cache_->Unref();
}

void TlsSessionKeyLogFileWriterCache
  ::TlsSessionKeyLogFileWriter::AppendSessionKeys(
    SSL_CTX* /* ssl_context */,
    TsiTlsSessionKeyLogConfig* tls_session_key_log_config,
    const std::string& session_keys_info) {
  grpc_core::MutexLock lock(&lock_);

  if (fd_ == nullptr || session_keys_info.empty()) return;

  switch (tls_session_key_log_config->session_key_logging_format_) {
    case GRPC_TLS_KEY_LOG_FORMAT_NSS:
      // Append to key log file under lock
      bool err;
      err = (fwrite((session_keys_info + "\n").c_str(), sizeof(char),
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
      break;

    // For future extensions to support more/custom logging formats
    default:
      gpr_log(GPR_INFO, "Unsupported TLS Key logging logging format: %d\n",
              tls_session_key_log_config->session_key_logging_format_);
      break;
  }
}

TlsSessionKeyLogFileWriterCache::~TlsSessionKeyLogFileWriterCache() {
  grpc_core::MutexLock lock(g_tls_session_key_log_cache_mu);
  g_cache_instance_ = nullptr;
}

grpc_core::RefCountedPtr<TlsSessionKeyLogger>
  TlsSessionKeyLogFileWriterCache::Get(
    TsiTlsSessionKeyLogConfig tls_session_key_log_config) {
  gpr_once_init(&g_cache_init, do_cache_init);
  GPR_DEBUG_ASSERT(g_tls_session_key_log_cache_mu != nullptr);
  {
    grpc_core::MutexLock lock(g_tls_session_key_log_cache_mu);
    if (g_cache_instance_ == nullptr) {
      g_cache_instance_ = new TlsSessionKeyLogFileWriterCache();
    }
    return g_cache_instance_->CreateTlsSessionKeyLogger(
      tls_session_key_log_config);
  }
}

grpc_core::RefCountedPtr<TlsSessionKeyLogger>
  TlsSessionKeyLogFileWriterCache::CreateTlsSessionKeyLogger(
    TsiTlsSessionKeyLogConfig tls_session_key_log_config) {
  if (tls_session_key_log_config.tls_session_key_log_file_path_.empty()) {
    return nullptr;
  }
  auto file_path =
      tls_session_key_log_config.tls_session_key_log_file_path_;
  // Check if a TlsSessionKeyLogFileWriter instance already exists for the
  // specified file path.
  auto it = tls_session_key_log_file_writer_map.find(file_path);
  if (it == tls_session_key_log_file_writer_map.end()) {
    // Create a new TlsSessionKeyLogFileWriter instance
    // Sets Ref count of new_tls_session_key_log_file_writer to 1 to make sure
    // the tls_session_key_log_file_writer_map is an owner. Relevant
    // TlsSessionKeyLogger objects which share this TlsSessionKeyLogFileWriter
    // instance also become owners.
    // The TlsSessionKeyLogFileWriter becomes an owner of the cache instance
    // which created it.
    auto new_tls_session_key_log_file_writer =
        grpc_core::MakeRefCounted<TlsSessionKeyLogFileWriter>(
          file_path, this);

    // Add the instance to the map.
    tls_session_key_log_file_writer_map.insert(
        std::pair<std::string, TlsSessionKeyLogFileWriter*>(
            file_path, new_tls_session_key_log_file_writer.get()));

    // The key logger becomes an owner of the key log file writer
    // instance.
    auto new_tls_session_key_logger =
        grpc_core::MakeRefCounted<TlsSessionKeyLogger>(
            tls_session_key_log_config,
            std::move(new_tls_session_key_log_file_writer));
    return new_tls_session_key_logger;
  }

  // The key logger also becomes an owner of the key log file writer
  // instance.
  auto new_tls_session_key_logger =
      grpc_core::MakeRefCounted<TlsSessionKeyLogger>(
          tls_session_key_log_config,
          it->second->Ref());
  return new_tls_session_key_logger;
}

};  // namespace tsi
