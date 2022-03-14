//
// Copyright 2022 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/lb_policy/oob_backend_metric.h"
#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/ext/filters/client_channel/subchannel_interface_internal.h"
#include "src/core/ext/filters/client_channel/subchannel_stream_client.h"

namespace grpc_core {

namespace {

constexpr char kProducerType[] = "orca";

class OrcaWatcher;

// This producer is registered with a subchannel.  It creates a
// streaming ORCA call and reports the resulting backend metrics to all
// registered watchers.
class OrcaProducer : public Subchannel::DataProducerInterface {
 public:
  OrcaProducer(RefCountedPtr<Subchannel> subchannel)
      : subchannel_(std::move(subchannel)) {
    subchannel_->AddDataProducer(this);
  }

  void Orphan() override {
    subchannel_->RemoveDataProducer(this);
    stream_client_.reset();
  }

  const char* type() override { return kProducerType; }

  void AddWatcher(WeakRefCountedPtr<OrcaWatcher> watcher) {
    watcher_map_[watcher.get()] = std::move(watcher);
// FIXME: start stream if not already started
  }

  void RemoveWatcher(OrcaWatcher* watcher) {
    watcher_map_.erase(watcher);
  }

 private:
  RefCountedPtr<Subchannel> subchannel_;
  Mutex mu_;
  // TODO(roth): Use std::set<> instead once we can use C++14 heterogenous
  // map lookups.
  std::map<OrcaWatcher*, WeakRefCountedPtr<OrcaWatcher>> watcher_map_
      ABSL_GUARDED_BY(mu_);
  OrphanablePtr<SubchannelStreamClient> stream_client_ ABSL_GUARDED_BY(mu_);
};

// This watcher is returned to the LB policy and added to the
// client channel SubchannelWrapper.
class OrcaWatcher : public SubchannelInterface::DataWatcherInterface {
 public:
  OrcaWatcher(Duration report_interval,
              std::unique_ptr<OobBackendMetricWatcher> watcher)
      : report_interval_(report_interval), watcher_(std::move(watcher)) {}

  void Orphan() override {
    if (producer_ != nullptr) {
      producer_->RemoveWatcher(this);
      producer_.reset();
    }
  }

  // When the client channel sees this wrapper, it will pass it the real
  // subchannel and the WorkSerializer to use.
  void SetSubchannel(
      Subchannel* subchannel,
      std::shared_ptr<WorkSerializer> work_serializer) override {
    work_serializer_ = std::move(work_serializer);
    // Check if our producer is already registered with the subchannel.
    // If not, create a new one, which will register itself with the subchannel.
    auto* p =
        static_cast<OrcaProducer*>(subchannel->GetDataProducer(kProducerType));
    if (p != nullptr) producer_ = p->RefIfNonZero();
    if (producer_ == nullptr) {
      producer_ = MakeRefCounted<OrcaProducer>(subchannel->Ref());
    }
    // Register ourself with the producer.
    producer_->AddWatcher(WeakRef());
  }

  OobBackendMetricWatcher* watcher() const { return watcher_.get(); }

 private:
  Duration report_interval_;
  std::unique_ptr<OobBackendMetricWatcher> watcher_;
  std::shared_ptr<WorkSerializer> work_serializer_;
  RefCountedPtr<OrcaProducer> producer_;
};

}  // namespace

RefCountedPtr<SubchannelInterface::DataWatcherInterface>
MakeOobBackendMetricWatcher(Duration report_interval,
                            std::unique_ptr<OobBackendMetricWatcher> watcher) {
  return MakeRefCounted<OrcaWatcher>(report_interval, std::move(watcher));
}

}  // namespace grpc_core
