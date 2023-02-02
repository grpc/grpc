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

#include <grpc/support/port_platform.h>

#include "src/cpp/ext/filters/census/environment_autodetect.h"

#include <deque>
#include <memory>

#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/gprpp/env.h"
#include "src/core/lib/gprpp/load_file.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/http/httpcli.h"
#include "src/core/lib/security/credentials/credentials.h"

namespace grpc {
namespace internal {

namespace {

// Fire and Forget class (Cleans up after itself.)
// Fetches the value of an attribute from the MetadataServer on a GCP
// environment.
class MetadataQuery {
 public:
  MetadataQuery(std::string attribute, grpc_polling_entity* pollent,
                absl::AnyInvocable<void(std::string)> callback)
      : attribute_(std::move(attribute)), callback_(std::move(callback)) {
    GRPC_CLOSURE_INIT(&on_done_, OnDone, this, nullptr);
    auto uri = grpc_core::URI::Create("http", "metadata.google.internal.",
                                      std::move(attribute),
                                      {} /* query params */, "" /* fragment */);
    GPR_ASSERT(uri.ok());  // params are hardcoded
    grpc_http_request request;
    memset(&request, 0, sizeof(grpc_http_request));
    grpc_http_header header = {const_cast<char*>("Metadata-Flavor"),
                               const_cast<char*>("Google")};
    request.hdr_count = 1;
    request.hdrs = &header;
    // The http call is local. If it takes more than one sec, it is for sure not
    // on compute engine.
    auto http_request = grpc_core::HttpRequest::Get(
        std::move(*uri), nullptr /* channel args */, pollent, &request,
        grpc_core::Timestamp::Now() + grpc_core::Duration::Seconds(1),
        &on_done_, &response_,
        grpc_core::RefCountedPtr<grpc_channel_credentials>(
            grpc_insecure_credentials_create()));
    http_request->Start();
  }

 private:
  static void OnDone(void* arg, grpc_error_handle error) {
    auto* self = static_cast<MetadataQuery*>(arg);
    std::string result;
    if (!error.ok()) {
      gpr_log(GPR_ERROR, "MetadataServer Query failed for %s: %s",
              self->attribute_.c_str(),
              grpc_core::StatusToString(error).c_str());
    } else if (self->response_.status != 200) {
      gpr_log(
          GPR_ERROR, "MetadataServer Query received non-200 status for %s: %s",
          self->attribute_.c_str(), grpc_core::StatusToString(error).c_str());
    } else {
      result = self->response_.body;
    }
    auto callback = std::move(self->callback_);
    delete self;
    return self->callback_(self->response_.body);
  }

  grpc_closure on_done_;
  std::string attribute_;
  absl::AnyInvocable<void(std::string)> callback_;
  grpc_http_response response_;
};

// This is not a definite method to get the namespace name for GKE, but it is
// the best we have.
std::string GetNamespaceName() {
  // Read the root file.
  const char* filename =
      "/var/run/secrets/kubernetes.io/serviceaccount/namespace";
  auto namespace_name = grpc_core::LoadFile(filename, false);
  if (!namespace_name.ok()) {
    gpr_log(GPR_DEBUG, "Reading file %s failed: %s", filename,
            grpc_core::StatusToString(namespace_name.status()).c_str());
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
class EnvironmentAutoDetectHelper {
 public:
  explicit EnvironmentAutoDetectHelper(
      std::string project_id, grpc_polling_entity* pollent,
      absl::AnyInvocable<void(EnvironmentAutoDetect::ResourceType)> on_done)
      : project_id_(std::move(project_id)),
        pollent_(pollent),
        on_done_(std::move(on_done)) {
    // GKE
    resource_.labels.emplace("project_id", project_id_);
    if (grpc_core::GetEnv("KUBERNETES_SERVICE_HOST").has_value()) {
      resource_.resource_type = "k8s_container";
      resource_.labels.emplace("namespace_name", GetNamespaceName());
      resource_.labels.emplace("pod_name", GetPodName());
      resource_.labels.emplace("container_name", GetContainerName());
      attributes_to_fetch_.push_back(
          {"location", "computeMetadata/v1/instance/zone"});
      attributes_to_fetch_.push_back(
          {"cluster_name",
           "computeMetadata/v1/instance/attributes/cluster-name"});
    }
    // Cloud Functions
    else if (grpc_core::GetEnv("FUNCTION_NAME").has_value() ||
             grpc_core::GetEnv("FUNCTION_TARGET").has_value()) {
      resource_.resource_type = "cloud_function";
      resource_.labels.emplace("function_name", GetFunctionName());
      attributes_to_fetch_.push_back(
          {"region", "computeMetadata/v1/instance/region"});
    }
    // Cloud Run
    else if (grpc_core::GetEnv("K_CONFIGURATION").has_value()) {
      resource_.resource_type = "cloud_run_revision";
      resource_.labels.emplace("revision_name", GetRevisionName());
      resource_.labels.emplace("service_name", GetServiceName());
      resource_.labels.emplace("configuration_name", GetConfiguratioName());
      attributes_to_fetch_.push_back(
          {"location", "computeMetadata/v1/instance/region"});
    }
    // App Engine
    else if (grpc_core::GetEnv("GAE_SERVICE").has_value()) {
      resource_.resource_type = "gae_app";
      resource_.labels.emplace("module_id", GetModuleId());
      resource_.labels.emplace("version_id", GetVersionId());
      attributes_to_fetch_.push_back(
          {"zone", "computeMetadata/v1/instance/zone"});
    }
    // Assume GCE
    else {
      assuming_gce_ = true;
      resource_.resource_type = "gce_instance";
      resource_.labels.emplace("instance_id", "computeMetadata/v1/instance/id");
      resource_.labels.emplace("instance_id",
                               "computeMetadata/v1/instance/zone");
    }
    FetchMetadataServerAttributesAsynchronously();
  }

 private:
  struct Attribute {
    std::string resource_attribute;
    std::string metadata_server_atttribute;
  };

  std::string project_id_;
  grpc_polling_entity* pollent_;
  absl::AnyInvocable<void(EnvironmentAutoDetect::ResourceType)> on_done_;
  std::deque<Attribute> attributes_to_fetch_;
  EnvironmentAutoDetect::ResourceType resource_;
  // This would be true if we are assuming the resource to be GCE. In this case,
  // there is a chance that it will fail and we should instead just use
  // "global".
  bool assuming_gce_ = false;

  void FetchMetadataServerAttributesAsynchronously() {
    // Done detecting the environment. Invoke the callback with the detected
    // resource.
    if (attributes_to_fetch_.empty()) {
      auto on_done = std::move(on_done_);
      auto resource = std::move(resource_);
      delete this;
      return on_done_(std::move(resource));
    }
    new MetadataQuery(attributes_to_fetch_.front().metadata_server_atttribute,
                      pollent_, [this](std::string result) {
                        resource_.labels.emplace(
                            attributes_to_fetch_.front().resource_attribute,
                            result);
                        FetchMetadataServerAttributesAsynchronously();
                      });
  }
};

}  // namespace

EnvironmentAutoDetect& EnvironmentAutoDetect::Get() {
  return GetWithProjectId("");
}

EnvironmentAutoDetect& EnvironmentAutoDetect::GetWithProjectId(
    std::string project_id) {
  static EnvironmentAutoDetect auto_detector(std::move(project_id));
  return auto_detector;
}

void EnvironmentAutoDetect::NotifyOnDone(grpc_polling_entity* pollent,
                                         absl::AnyInvocable<void()> callback) {
  {
    grpc_core::ReleasableMutexLock lock(&mu_);
    // Environment has already been detected
    if (resource_ != nullptr) {
      lock.Release();
      // Execute on the event engine to avoid deadlocks.
      return grpc_event_engine::experimental::GetDefaultEventEngine()->Run(
          std::move(callback));
    }
    callbacks_.push_back(std::move(callback));
    // We already have a polling entity.
    if (pollent_ != nullptr) {
      return;
    }
    // We can actually start the detection now.
    pollent_ = pollent;
  }
  new EnvironmentAutoDetectHelper(
      project_id_, pollent,
      [this](EnvironmentAutoDetect::ResourceType resource) {
        std::vector<absl::AnyInvocable<void()>> callbacks;
        {
          grpc_core::MutexLock lock(&mu_);
          resource_ = std::make_unique<EnvironmentAutoDetect::ResourceType>(
              std::move(resource));
          callbacks_ = std::move(callbacks);
        }
        for (auto& callback : callbacks) {
          callback();
        }
      });
}

}  // namespace internal
}  // namespace grpc