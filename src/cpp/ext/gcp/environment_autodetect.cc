//
//
// Copyright 2023 gRPC authors.
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

#include "src/cpp/ext/gcp/environment_autodetect.h"

#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/sync.h>
#include <grpcpp/impl/grpc_library.h>

#include <memory>
#include <optional>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/util/crash.h"
#include "src/core/util/env.h"
#include "src/core/util/gcp_metadata_query.h"
#include "src/core/util/load_file.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/status_helper.h"
#include "src/core/util/time.h"

namespace grpc {
namespace internal {

namespace {

// This is not a definite method to get the namespace name for GKE, but it is
// the best we have.
std::string GetNamespaceName() {
  // Read the root file.
  const char* filename =
      "/var/run/secrets/kubernetes.io/serviceaccount/namespace";
  auto namespace_name = grpc_core::LoadFile(filename, false);
  if (!namespace_name.ok()) {
    GRPC_TRACE_VLOG(environment_autodetect, 2)
        << "Reading file " << filename
        << " failed: " << grpc_core::StatusToString(namespace_name.status());
    // Fallback on an environment variable
    return grpc_core::GetEnv("NAMESPACE_NAME").value_or("");
  }
  return std::string(reinterpret_cast<const char*>((*namespace_name).begin()),
                     (*namespace_name).length());
}

// Get pod name for GKE
std::string GetPodName() {
  auto pod_name = grpc_core::GetEnv("POD_NAME");
  if (pod_name.has_value()) {
    return pod_name.value();
  }
  return grpc_core::GetEnv("HOSTNAME").value_or("");
}

// Get container name for GKE
std::string GetContainerName() {
  return grpc_core::GetEnv("HOSTNAME").value_or("");
}

// Get function name for Cloud Functions
std::string GetFunctionName() {
  auto k_service = grpc_core::GetEnv("K_SERVICE");
  if (k_service.has_value()) {
    return k_service.value();
  }
  return grpc_core::GetEnv("FUNCTION_NAME").value_or("");
}

// Get revision name for Cloud run
std::string GetRevisionName() {
  return grpc_core::GetEnv("K_REVISION").value_or("");
}

// Get service name for Cloud run
std::string GetServiceName() {
  return grpc_core::GetEnv("K_SERVICE").value_or("");
}

// Get configuration name for Cloud run
std::string GetConfiguratioName() {
  return grpc_core::GetEnv("K_CONFIGURATION").value_or("");
}

// Get module ID for App Engine
std::string GetModuleId() {
  return grpc_core::GetEnv("GAE_SERVICE").value_or("");
}

// Get version ID for App Engine
std::string GetVersionId() {
  return grpc_core::GetEnv("GAE_VERSION").value_or("");
}

// Fire and forget class
class EnvironmentAutoDetectHelper
    : public grpc_core::InternallyRefCounted<EnvironmentAutoDetectHelper>,
      private internal::GrpcLibrary {
 public:
  EnvironmentAutoDetectHelper(
      std::string project_id,
      absl::AnyInvocable<void(EnvironmentAutoDetect::ResourceType)> on_done,
      std::shared_ptr<grpc_event_engine::experimental::EventEngine>
          event_engine)
      : InternallyRefCounted(/*trace=*/nullptr, /*initial_refcount=*/2),
        project_id_(std::move(project_id)),
        on_done_(std::move(on_done)),
        event_engine_(std::move(event_engine)) {
    grpc_core::ExecCtx exec_ctx;
    // TODO(yashykt): The pollset stuff should go away once the HTTP library is
    // ported over to use EventEngine.
    pollset_ = static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
    grpc_pollset_init(pollset_, &mu_poll_);
    pollent_ = grpc_polling_entity_create_from_pollset(pollset_);
    // TODO(yashykt): Note that using EventEngine::Run is not fork-safe. If we
    // want to make this fork-safe, we might need some re-work here.
    event_engine_->Run([this] { PollLoop(); });
    AutoDetect();
  }

  ~EnvironmentAutoDetectHelper() override {
    grpc_core::ExecCtx exec_ctx;
    grpc_pollset_shutdown(
        pollset_, GRPC_CLOSURE_CREATE(
                      [](void* arg, absl::Status /* status */) {
                        grpc_pollset_destroy(static_cast<grpc_pollset*>(arg));
                        gpr_free(arg);
                      },
                      pollset_, nullptr));
  }

  void Orphan() override {
    grpc_core::Crash("Illegal Orphan() call on EnvironmentAutoDetectHelper.");
  }

 private:
  struct Attribute {
    std::string resource_attribute;
    std::string metadata_server_atttribute;
  };

  void PollLoop() {
    grpc_core::ExecCtx exec_ctx;
    bool done = false;
    gpr_mu_lock(mu_poll_);
    grpc_pollset_worker* worker = nullptr;
    if (!GRPC_LOG_IF_ERROR(
            "pollset_work",
            grpc_pollset_work(grpc_polling_entity_pollset(&pollent_), &worker,
                              grpc_core::Timestamp::InfPast()))) {
      notify_poller_ = true;
    }
    done = notify_poller_;
    gpr_mu_unlock(mu_poll_);
    if (!done) {
      event_engine_->RunAfter(grpc_core::Duration::Milliseconds(100),
                              [this] { PollLoop(); });
    } else {
      Unref();
    }
  }

  void AutoDetect() {
    grpc_core::MutexLock lock(&mu_);
    // GKE
    resource_.labels.emplace("project_id", project_id_);
    if (grpc_core::GetEnv("KUBERNETES_SERVICE_HOST").has_value()) {
      resource_.resource_type = "k8s_container";
      resource_.labels.emplace("namespace_name", GetNamespaceName());
      resource_.labels.emplace("pod_name", GetPodName());
      resource_.labels.emplace("container_name", GetContainerName());
      attributes_to_fetch_.emplace(grpc_core::GcpMetadataQuery::kZoneAttribute,
                                   "location");
      attributes_to_fetch_.emplace(
          grpc_core::GcpMetadataQuery::kClusterNameAttribute, "cluster_name");
    }
    // Cloud Functions
    else if (grpc_core::GetEnv("FUNCTION_NAME").has_value() ||
             grpc_core::GetEnv("FUNCTION_TARGET").has_value()) {
      resource_.resource_type = "cloud_function";
      resource_.labels.emplace("function_name", GetFunctionName());
      attributes_to_fetch_.emplace(
          grpc_core::GcpMetadataQuery::kRegionAttribute, "region");
    }
    // Cloud Run
    else if (grpc_core::GetEnv("K_CONFIGURATION").has_value()) {
      resource_.resource_type = "cloud_run_revision";
      resource_.labels.emplace("revision_name", GetRevisionName());
      resource_.labels.emplace("service_name", GetServiceName());
      resource_.labels.emplace("configuration_name", GetConfiguratioName());
      attributes_to_fetch_.emplace(
          grpc_core::GcpMetadataQuery::kRegionAttribute, "location");
    }
    // App Engine
    else if (grpc_core::GetEnv("GAE_SERVICE").has_value()) {
      resource_.resource_type = "gae_app";
      resource_.labels.emplace("module_id", GetModuleId());
      resource_.labels.emplace("version_id", GetVersionId());
      attributes_to_fetch_.emplace(grpc_core::GcpMetadataQuery::kZoneAttribute,
                                   "zone");
    }
    // Assume GCE
    else {
      assuming_gce_ = true;
      resource_.resource_type = "gce_instance";
      attributes_to_fetch_.emplace(
          grpc_core::GcpMetadataQuery::kInstanceIdAttribute, "instance_id");
      attributes_to_fetch_.emplace(grpc_core::GcpMetadataQuery::kZoneAttribute,
                                   "zone");
    }
    FetchMetadataServerAttributesAsynchronouslyLocked();
  }

  void FetchMetadataServerAttributesAsynchronouslyLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    CHECK(!attributes_to_fetch_.empty());
    for (auto& element : attributes_to_fetch_) {
      queries_.push_back(grpc_core::MakeOrphanable<grpc_core::GcpMetadataQuery>(
          element.first, &pollent_,
          [this](std::string attribute, absl::StatusOr<std::string> result) {
            GRPC_TRACE_LOG(environment_autodetect, INFO)
                << "Environment AutoDetect: Attribute: \"" << attribute
                << "\" Result: \""
                << (result.ok() ? result.value()
                                : grpc_core::StatusToString(result.status()))
                << "\"";
            std::optional<EnvironmentAutoDetect::ResourceType> resource;
            {
              grpc_core::MutexLock lock(&mu_);
              auto it = attributes_to_fetch_.find(attribute);
              if (it != attributes_to_fetch_.end()) {
                if (result.ok()) {
                  resource_.labels.emplace(std::move(it->second),
                                           std::move(result).value());
                }
                // If fetching from the MetadataServer failed and we were
                // assuming a GCE environment, fallback to "global".
                else if (assuming_gce_) {
                  GRPC_TRACE_LOG(environment_autodetect, INFO)
                      << "Environment Autodetect: Falling back to "
                      << "global resource type";
                  assuming_gce_ = false;
                  resource_.resource_type = "global";
                }
                attributes_to_fetch_.erase(it);
              } else {
                // This should not happen
                LOG(ERROR) << "An unexpected attribute was seen from the "
                              "MetadataServer: "
                           << attribute;
              }
              if (attributes_to_fetch_.empty()) {
                resource = std::move(resource_);
              }
            }
            if (resource.has_value()) {
              gpr_mu_lock(mu_poll_);
              notify_poller_ = true;
              gpr_mu_unlock(mu_poll_);
              auto on_done = std::move(on_done_);
              Unref();
              on_done(std::move(resource).value());
            }
          },
          grpc_core::Duration::Seconds(10)));
    }
  }

  const std::string project_id_;
  grpc_pollset* pollset_ = nullptr;
  grpc_polling_entity pollent_;
  gpr_mu* mu_poll_ = nullptr;
  absl::AnyInvocable<void(EnvironmentAutoDetect::ResourceType)> on_done_;
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_;
  grpc_core::Mutex mu_;
  bool notify_poller_ = false;
  absl::flat_hash_map<std::string /* metadata_server_attribute */,
                      std::string /* resource_attribute */>
      attributes_to_fetch_ ABSL_GUARDED_BY(mu_);
  std::vector<grpc_core::OrphanablePtr<grpc_core::GcpMetadataQuery>> queries_
      ABSL_GUARDED_BY(mu_);
  EnvironmentAutoDetect::ResourceType resource_ ABSL_GUARDED_BY(mu_);
  // This would be true if we are assuming the resource to be GCE. In this case,
  // there is a chance that it will fail and we should instead just use
  // "global".
  bool assuming_gce_ ABSL_GUARDED_BY(mu_) = false;
};

EnvironmentAutoDetect* g_autodetect = nullptr;

}  // namespace

void EnvironmentAutoDetect::Create(std::string project_id) {
  CHECK_EQ(g_autodetect, nullptr);
  CHECK(!project_id.empty());

  g_autodetect = new EnvironmentAutoDetect(project_id);
}

EnvironmentAutoDetect& EnvironmentAutoDetect::Get() { return *g_autodetect; }

EnvironmentAutoDetect::EnvironmentAutoDetect(std::string project_id)
    : project_id_(std::move(project_id)) {
  CHECK(!project_id_.empty());
}

void EnvironmentAutoDetect::NotifyOnDone(absl::AnyInvocable<void()> callback) {
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine;
  {
    grpc_core::MutexLock lock(&mu_);
    // Environment has already been detected
    if (resource_ != nullptr) {
      // Execute on the event engine to avoid deadlocks.
      return event_engine_->Run(std::move(callback));
    }
    callbacks_.push_back(std::move(callback));
    // Use the event_engine_ pointer as a signal to judge whether we've started
    // detecting the environment.
    if (event_engine_ == nullptr) {
      event_engine_ = grpc_event_engine::experimental::GetDefaultEventEngine();
      event_engine = event_engine_;
    }
  }
  if (event_engine) {
    new EnvironmentAutoDetectHelper(
        project_id_,
        [this](EnvironmentAutoDetect::ResourceType resource) {
          std::vector<absl::AnyInvocable<void()>> callbacks;
          {
            grpc_core::MutexLock lock(&mu_);
            resource_ = std::make_unique<EnvironmentAutoDetect::ResourceType>(
                std::move(resource));
            callbacks = std::move(callbacks_);
          }
          for (auto& callback : callbacks) {
            callback();
          }
        },
        std::move(event_engine));
  }
}

}  // namespace internal
}  // namespace grpc
