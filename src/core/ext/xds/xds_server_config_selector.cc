//
//
// Copyright 2020 gRPC authors.
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

#include "src/core/ext/xds/xds_server_config_selector.h"

#include "absl/strings/str_join.h"

#include "src/core/ext/service_config/server_config_call_data.h"
#include "src/core/ext/service_config/server_config_selector.h"
#include "src/core/lib/transport/error_utils.h"

namespace grpc_core {
namespace {

void* XdsServerConfigSelectorArgCopy(void* p) {
  XdsServerConfigSelectorArg* arg = static_cast<XdsServerConfigSelectorArg*>(p);
  return arg->Ref().release();
}

void XdsServerConfigSelectorArgDestroy(void* p) {
  XdsServerConfigSelectorArg* arg = static_cast<XdsServerConfigSelectorArg*>(p);
  arg->Unref();
}

int XdsServerConfigSelectorArgCmp(void* p, void* q) {
  return QsortCompare(p, q);
}

const grpc_arg_pointer_vtable kChannelArgVtable = {
    XdsServerConfigSelectorArgCopy, XdsServerConfigSelectorArgDestroy,
    XdsServerConfigSelectorArgCmp};

}  // namespace

const char* XdsServerConfigSelectorArg::kChannelArgName =
    "grpc.internal.xds_server_config_selector";

grpc_arg XdsServerConfigSelectorArg::MakeChannelArg() const {
  return grpc_channel_arg_pointer_create(
      const_cast<char*>(kChannelArgName),
      const_cast<XdsServerConfigSelectorArg*>(this), &kChannelArgVtable);
}

RefCountedPtr<XdsServerConfigSelectorArg>
XdsServerConfigSelectorArg::GetFromChannelArgs(const grpc_channel_args& args) {
  XdsServerConfigSelectorArg* config_selector_arg =
      grpc_channel_args_find_pointer<XdsServerConfigSelectorArg>(
          &args, kChannelArgName);
  return config_selector_arg != nullptr ? config_selector_arg->Ref() : nullptr;
}

namespace {

class XdsServerConfigSelector : public ServerConfigSelector {
 public:
  ~XdsServerConfigSelector() override = default;
  static absl::StatusOr<RefCountedPtr<XdsServerConfigSelector>> Create(
      absl::StatusOr<XdsApi::RdsUpdate> rds_update,
      const std::vector<XdsApi::LdsUpdate::HttpConnectionManager::HttpFilter>&
          http_filters);

  CallConfig GetCallConfig(grpc_metadata_batch* metadata) override;

 private:
  struct VirtualHost {
    struct Route {
      XdsApi::Route::Matchers matchers;
      RefCountedPtr<ServiceConfig> method_config;
    };
    std::vector<std::string> domains;
    std::vector<Route> routes;
  };

  std::vector<VirtualHost> virtual_hosts_;
};

class ChannelData {
 public:
  static grpc_error_handle Init(grpc_channel_element* elem,
                                grpc_channel_element_args* args);
  static void Destroy(grpc_channel_element* elem);

  absl::StatusOr<RefCountedPtr<XdsServerConfigSelector>> config_selector() {
    MutexLock lock(&mu_);
    return config_selector_;
  }

 private:
  class RdsUpdateWatcher
      : public XdsServerConfigFetcher::RdsUpdateWatcherInterface {
   public:
    explicit RdsUpdateWatcher(ChannelData* chand) : chand_(chand) {}
    void OnRdsUpdate(absl::StatusOr<XdsApi::RdsUpdate> rds_update) override;

   private:
    ChannelData* chand_;
  };

  ChannelData(grpc_channel_element* elem, grpc_channel_element_args* args);
  ~ChannelData();

  // TODO(): Do you need to store the whole arg?
  RefCountedPtr<XdsServerConfigSelectorArg> config_selector_arg_;
  RdsUpdateWatcher* watcher_ = nullptr;
  Mutex mu_;
  absl::StatusOr<RefCountedPtr<XdsServerConfigSelector>> config_selector_
      ABSL_GUARDED_BY(mu_);
};

class CallData {
 public:
  static grpc_error_handle Init(grpc_call_element* elem,
                                const grpc_call_element_args* args);
  static void Destroy(grpc_call_element* elem,
                      const grpc_call_final_info* /* final_info */,
                      grpc_closure* /* then_schedule_closure */);
  static void StartTransportStreamOpBatch(grpc_call_element* elem,
                                          grpc_transport_stream_op_batch* op);

 private:
  CallData(grpc_call_element* elem, const grpc_call_element_args& args);
  ~CallData();
  static void RecvInitialMetadataReady(void* user_data,
                                       grpc_error_handle error);
  static void RecvTrailingMetadataReady(void* user_data,
                                        grpc_error_handle error);
  void MaybeResumeRecvTrailingMetadataReady();

  grpc_call_context_element* call_context_;
  grpc_core::Arena* arena_;
  grpc_core::CallCombiner* call_combiner_;
  // Overall error for the call
  grpc_error_handle error_ = GRPC_ERROR_NONE;
  // State for keeping track of recv_initial_metadata
  grpc_metadata_batch* recv_initial_metadata_ = nullptr;
  grpc_closure* original_recv_initial_metadata_ready_ = nullptr;
  grpc_closure recv_initial_metadata_ready_;
  // State for keeping of track of recv_trailing_metadata
  grpc_closure* original_recv_trailing_metadata_ready_;
  grpc_closure recv_trailing_metadata_ready_;
  grpc_error_handle recv_trailing_metadata_ready_error_;
  bool seen_recv_trailing_metadata_ready_ = false;
};

// XdsServerConfigSelector

absl::StatusOr<RefCountedPtr<XdsServerConfigSelector>>
XdsServerConfigSelector::Create(
    absl::StatusOr<XdsApi::RdsUpdate> rds_update,
    const std::vector<XdsApi::LdsUpdate::HttpConnectionManager::HttpFilter>&
        http_filters) {
  if (!rds_update.ok()) {
    return rds_update.status();
  }
  auto config_selector = MakeRefCounted<XdsServerConfigSelector>();
  for (const auto& vhost : rds_update->virtual_hosts) {
    config_selector->virtual_hosts_.push_back(VirtualHost());
    auto& virtual_host = config_selector->virtual_hosts_.back();
    virtual_host.domains = vhost.domains;
    for (const auto& route : vhost.routes) {
      virtual_host.routes.push_back(VirtualHost::Route());
      VirtualHost::Route config_selector_route = virtual_host.routes.back();
      config_selector_route.matchers = route.matchers;
      grpc_channel_args* args = nullptr;
      std::map<std::string, std::vector<std::string>> per_filter_configs;
      for (const auto& http_filter : http_filters) {
        // Find filter.  This is guaranteed to succeed, because it's checked
        // at config validation time in the XdsApi code.
        const XdsHttpFilterImpl* filter_impl =
            XdsHttpFilterRegistry::GetFilterForType(
                http_filter.config.config_proto_type_name);
        GPR_ASSERT(filter_impl != nullptr);
        // If there is not actually any C-core filter associated with this
        // xDS filter, then it won't need any config, so skip it.
        if (filter_impl->channel_filter() == nullptr) continue;
        // Allow filter to add channel args that may affect service config
        // parsing.
        // TODO(): Is modifying channel args needed?
        args = filter_impl->ModifyChannelArgs(args);
        // Find config override, if any.
        const XdsHttpFilterImpl::FilterConfig* config_override = nullptr;
        auto it = route.typed_per_filter_config.find(http_filter.name);
        if (it == route.typed_per_filter_config.end()) {
          it = vhost.typed_per_filter_config.find(http_filter.name);
          if (it != vhost.typed_per_filter_config.end()) {
            config_override = &it->second;
          }
        } else {
          config_override = &it->second;
        }
        // Generate service config for filter.
        auto method_config_field = filter_impl->GenerateServiceConfig(
            http_filter.config, config_override);
        if (!method_config_field.ok()) {
          return method_config_field.status();
        }
        per_filter_configs[method_config_field->service_config_field_name]
            .push_back(method_config_field->element);
      }
      grpc_channel_args_destroy(args);
      std::vector<std::string> fields;
      fields.reserve(per_filter_configs.size());
      for (const auto& p : per_filter_configs) {
        fields.emplace_back(absl::StrCat("    \"", p.first, "\": [\n",
                                         absl::StrJoin(p.second, ",\n"),
                                         "\n    ]"));
      }
      if (!fields.empty()) {
        std::string json = absl::StrCat(
            "{\n"
            "  \"methodConfig\": [ {\n"
            "    \"name\": [\n"
            "      {}\n"
            "    ],\n"
            "    ",
            absl::StrJoin(fields, ",\n"),
            "\n  } ]\n"
            "}");
        grpc_error_handle error = GRPC_ERROR_NONE;
        config_selector_route.method_config =
            ServiceConfig::Create(args, json.c_str(), &error);
        GPR_ASSERT(error == GRPC_ERROR_NONE);
      }
    }
  }
  return config_selector;
}

namespace {

// TODO(): Factor out common code with clientside
absl::optional<absl::string_view> GetHeaderValue(
    grpc_metadata_batch* initial_metadata, absl::string_view header_name,
    std::string* concatenated_value) {
  // Note: If we ever allow binary headers here, we still need to
  // special-case ignore "grpc-tags-bin" and "grpc-trace-bin", since
  // they are not visible to the LB policy in grpc-go.
  if (absl::EndsWith(header_name, "-bin")) {
    return absl::nullopt;
  } else if (header_name == "content-type") {
    return "application/grpc";
  }
  return grpc_metadata_batch_get_value(initial_metadata, header_name,
                                       concatenated_value);
}

bool HeadersMatch(const std::vector<HeaderMatcher>& header_matchers,
                  grpc_metadata_batch* initial_metadata) {
  for (const auto& header_matcher : header_matchers) {
    std::string concatenated_value;
    if (!header_matcher.Match(GetHeaderValue(
            initial_metadata, header_matcher.name(), &concatenated_value))) {
      return false;
    }
  }
  return true;
}

bool UnderFraction(const uint32_t fraction_per_million) {
  // Generate a random number in [0, 1000000).
  const uint32_t random_number = rand() % 1000000;
  return random_number < fraction_per_million;
}

}  // namespace

ServerConfigSelector::CallConfig XdsServerConfigSelector::GetCallConfig(
    grpc_metadata_batch* metadata) {
  CallConfig call_config;
  if (metadata->legacy_index()->named.path == nullptr) {
    call_config.error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("No path found");
    return call_config;
  }
  absl::string_view path = absl::string_view(
      reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(
          GRPC_MDVALUE(metadata->legacy_index()->named.path->md))),
      GRPC_SLICE_LENGTH(
          GRPC_MDVALUE(metadata->legacy_index()->named.path->md)));
  if (metadata->legacy_index()->named.authority == nullptr) {
    call_config.error =
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("No authority found");
    return call_config;
  }
  absl::string_view authority = absl::string_view(
      reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(
          GRPC_MDVALUE(metadata->legacy_index()->named.authority->md))),
      GRPC_SLICE_LENGTH(
          GRPC_MDVALUE(metadata->legacy_index()->named.authority->md)));
  auto* virtual_host =
      XdsApi::RdsUpdate::FindVirtualHostForDomain(&virtual_hosts_, authority);
  if (virtual_host == nullptr) {
    call_config.error = GRPC_ERROR_CREATE_FROM_CPP_STRING(
        absl::StrCat("could not find VirtualHost for ", authority,
                     " in RouteConfiguration"));
    return call_config;
  }
  for (const auto& route : virtual_host->routes) {
    // Path matching.
    if (!route.matchers.path_matcher.Match(path)) {
      continue;
    }
    // Header Matching.
    if (!HeadersMatch(route.matchers.header_matchers, metadata)) {
      continue;
    }
    // Match fraction check
    if (route.matchers.fraction_per_million.has_value() &&
        !UnderFraction(route.matchers.fraction_per_million.value())) {
      continue;
    }
    if (route.method_config != nullptr) {
      call_config.method_configs =
          route.method_config->GetMethodParsedConfigVector(grpc_empty_slice());
      call_config.service_config = route.method_config;
    }
    return call_config;
  }
  call_config.error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("No route matched");
  return call_config;
}

// ChannelData::RdsUpdateWatcher

void ChannelData::RdsUpdateWatcher::OnRdsUpdate(
    absl::StatusOr<XdsApi::RdsUpdate> rds_update) {
  MutexLock lock(&chand_->mu_);
  chand_->config_selector_ = XdsServerConfigSelector::Create(
      rds_update, chand_->config_selector_arg_->http_filters);
}

// ChannelData

grpc_error_handle ChannelData::Init(grpc_channel_element* elem,
                                    grpc_channel_element_args* args) {
  GPR_ASSERT(elem->filter = &kXdsServerConfigSelectorFilter);
  new (elem->channel_data) ChannelData(elem, args);
  return GRPC_ERROR_NONE;
}

void ChannelData::Destroy(grpc_channel_element* elem) {
  auto* chand = static_cast<ChannelData*>(elem->channel_data);
  chand->~ChannelData();
}

ChannelData::ChannelData(grpc_channel_element* /* elem */,
                         grpc_channel_element_args* args) {
  config_selector_arg_ =
      XdsServerConfigSelectorArg::GetFromChannelArgs(*args->channel_args);
  if (config_selector_arg_->rds_update) {
    config_selector_ = XdsServerConfigSelector::Create(
        *config_selector_arg_->rds_update, config_selector_arg_->http_filters);
  } else {
    GPR_ASSERT(!config_selector_arg_->resource_name.empty());
    auto watcher = absl::make_unique<RdsUpdateWatcher>(this);
    watcher_ = watcher.get();
    auto rds_update =
        config_selector_arg_->server_config_fetcher->StartRdsWatch(
            config_selector_arg_->resource_name, std::move(watcher));
    GPR_ASSERT(rds_update.has_value());
    config_selector_ = XdsServerConfigSelector::Create(
        rds_update.value(), config_selector_arg_->http_filters);
  }
}

ChannelData::~ChannelData() {
  if (watcher_) {
    config_selector_arg_->server_config_fetcher->CancelRdsWatch(
        config_selector_arg_->resource_name, watcher_);
  }
}

// CallData

grpc_error_handle CallData::Init(grpc_call_element* elem,
                                 const grpc_call_element_args* args) {
  new (elem->call_data) CallData(elem, *args);
  return GRPC_ERROR_NONE;
}

void CallData::Destroy(grpc_call_element* elem,
                       const grpc_call_final_info* /*final_info*/,
                       grpc_closure* /*then_schedule_closure*/) {
  auto* calld = static_cast<CallData*>(elem->call_data);
  calld->~CallData();
}

void CallData::StartTransportStreamOpBatch(grpc_call_element* elem,
                                           grpc_transport_stream_op_batch* op) {
  CallData* calld = static_cast<CallData*>(elem->call_data);
  if (op->recv_initial_metadata) {
    calld->recv_initial_metadata_ =
        op->payload->recv_initial_metadata.recv_initial_metadata;
    calld->original_recv_initial_metadata_ready_ =
        op->payload->recv_initial_metadata.recv_initial_metadata_ready;
    op->payload->recv_initial_metadata.recv_initial_metadata_ready =
        &calld->recv_initial_metadata_ready_;
  }
  if (op->recv_trailing_metadata) {
    // We might generate errors on receiving initial metadata which we need to
    // bubble up through recv_trailing_metadata_ready
    calld->original_recv_trailing_metadata_ready_ =
        op->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
    op->payload->recv_trailing_metadata.recv_trailing_metadata_ready =
        &calld->recv_trailing_metadata_ready_;
  }
  // Chain to the next filter.
  grpc_call_next_op(elem, op);
}

CallData::CallData(grpc_call_element* elem, const grpc_call_element_args& args)
    : call_context_(args.context),
      arena_(args.arena),
      call_combiner_(args.call_combiner) {
  GRPC_CLOSURE_INIT(&recv_initial_metadata_ready_, RecvInitialMetadataReady,
                    elem, grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&recv_trailing_metadata_ready_, RecvTrailingMetadataReady,
                    elem, grpc_schedule_on_exec_ctx);
}

CallData::~CallData() { GRPC_ERROR_UNREF(error_); }

void CallData::RecvInitialMetadataReady(void* user_data,
                                        grpc_error_handle error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(user_data);
  CallData* calld = static_cast<CallData*>(elem->call_data);
  ChannelData* chand = static_cast<ChannelData*>(elem->channel_data);
  if (error == GRPC_ERROR_NONE) {
    auto config_selector = chand->config_selector();
    if (config_selector.ok()) {
      auto call_config =
          config_selector.value()->GetCallConfig(calld->recv_initial_metadata_);
      if (call_config.error != GRPC_ERROR_NONE) {
        calld->error_ = call_config.error;
        error = call_config.error;  // Does not take a ref
      } else {
        calld->arena_->New<ServerConfigCallData>(
            std::move(call_config.service_config), call_config.method_configs,
            calld->call_context_);
      }
    } else {
      calld->error_ = absl_status_to_grpc_error(config_selector.status());
    }
  }
  calld->MaybeResumeRecvTrailingMetadataReady();
  grpc_closure* closure = calld->original_recv_initial_metadata_ready_;
  calld->original_recv_initial_metadata_ready_ = nullptr;
  Closure::Run(DEBUG_LOCATION, closure, GRPC_ERROR_REF(error));
}

void CallData::RecvTrailingMetadataReady(void* user_data,
                                         grpc_error_handle error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(user_data);
  CallData* calld = static_cast<CallData*>(elem->call_data);
  if (calld->original_recv_initial_metadata_ready_ != nullptr) {
    calld->seen_recv_trailing_metadata_ready_ = true;
    calld->recv_trailing_metadata_ready_error_ = GRPC_ERROR_REF(error);
    GRPC_CALL_COMBINER_STOP(calld->call_combiner_,
                            "Deferring RecvTrailingMetadataReady until after "
                            "RecvInitialMetadataReady");
    return;
  }
  error = grpc_error_add_child(GRPC_ERROR_REF(error), calld->error_);
  calld->error_ = GRPC_ERROR_NONE;
  grpc_closure* closure = calld->original_recv_trailing_metadata_ready_;
  calld->original_recv_trailing_metadata_ready_ = nullptr;
  Closure::Run(DEBUG_LOCATION, closure, error);
}

void CallData::MaybeResumeRecvTrailingMetadataReady() {
  if (seen_recv_trailing_metadata_ready_) {
    seen_recv_trailing_metadata_ready_ = false;
    grpc_error_handle error = recv_trailing_metadata_ready_error_;
    recv_trailing_metadata_ready_error_ = GRPC_ERROR_NONE;
    GRPC_CALL_COMBINER_START(call_combiner_, &recv_trailing_metadata_ready_,
                             error, "Continuing RecvTrailingMetadataReady");
  }
}

}  // namespace

const grpc_channel_filter kXdsServerConfigSelectorFilter = {
    CallData::StartTransportStreamOpBatch,
    grpc_channel_next_op,
    sizeof(CallData),
    CallData::Init,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    CallData::Destroy,
    sizeof(ChannelData),
    ChannelData::Init,
    ChannelData::Destroy,
    grpc_channel_next_get_info,
    "xds_server_config_selector_filter",
};

}  // namespace grpc_core
