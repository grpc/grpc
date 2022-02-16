//
// Copyright 2015 gRPC authors.
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

#include "src/core/lib/resolver/resolver_registry.h"

#include <string.h>

#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

namespace grpc_core {

namespace {

struct RegistryState {
  std::map<absl::string_view, std::unique_ptr<ResolverFactory>> factories;
  std::string default_prefix = "dns:///";
};

RegistryState* g_state = nullptr;

}  // namespace

//
// ResolverRegistry::Builder
//

void ResolverRegistry::Builder::InitRegistry() {
  if (g_state == nullptr) g_state = new RegistryState();
}

void ResolverRegistry::Builder::ShutdownRegistry() {
  delete g_state;
  g_state = nullptr;
}

void ResolverRegistry::Builder::SetDefaultPrefix(std::string default_prefix) {
  InitRegistry();
  GPR_ASSERT(!g_state->default_prefix.empty());
  g_state->default_prefix = std::move(default_prefix);
}

void ResolverRegistry::Builder::RegisterResolverFactory(
    std::unique_ptr<ResolverFactory> factory) {
  InitRegistry();
  auto p = g_state->factories.emplace(factory->scheme(), std::move(factory));
  GPR_ASSERT(p.second);
}

//
// ResolverRegistry
//

ResolverFactory* ResolverRegistry::LookupResolverFactory(
    absl::string_view scheme) {
  GPR_ASSERT(g_state != nullptr);
  auto it = g_state->factories.find(scheme);
  if (it == g_state->factories.end()) return nullptr;
  return it->second.get();
}

bool ResolverRegistry::IsValidTarget(absl::string_view target) {
  URI uri;
  ResolverFactory* factory = FindResolverFactory(target, &uri);
  if (factory == nullptr) return false;
  return factory->IsValidUri(uri);
}

OrphanablePtr<Resolver> ResolverRegistry::CreateResolver(
    absl::string_view target, const grpc_channel_args* args,
    grpc_pollset_set* pollset_set,
    std::shared_ptr<WorkSerializer> work_serializer,
    std::unique_ptr<Resolver::ResultHandler> result_handler) {
  ResolverArgs resolver_args;
  ResolverFactory* factory = FindResolverFactory(target, &resolver_args.uri);
  if (factory == nullptr) return nullptr;
  resolver_args.args = args;
  resolver_args.pollset_set = pollset_set;
  resolver_args.work_serializer = std::move(work_serializer);
  resolver_args.result_handler = std::move(result_handler);
  return factory->CreateResolver(std::move(resolver_args));
}

std::string ResolverRegistry::GetDefaultAuthority(absl::string_view target) {
  URI uri;
  ResolverFactory* factory = FindResolverFactory(target, &uri);
  if (factory == nullptr) return "";
  return factory->GetDefaultAuthority(uri);
}

std::string ResolverRegistry::AddDefaultPrefixIfNeeded(
    absl::string_view target) {
  URI uri;
  FindResolverFactory(target, &uri);
  return uri.ToString();
}

// Returns the factory for the scheme of \a target.  If \a target does
// not parse as a URI, prepends \a default_prefix_ and tries again.
// If URI parsing is successful (in either attempt), sets \a uri to
// point to the parsed URI.
ResolverFactory* ResolverRegistry::FindResolverFactory(absl::string_view target,
                                                       URI* uri) {
  GPR_ASSERT(uri != nullptr);
  absl::StatusOr<URI> tmp_uri = URI::Parse(target);
  ResolverFactory* factory =
      tmp_uri.ok() ? LookupResolverFactory(tmp_uri->scheme()) : nullptr;
  if (factory != nullptr) {
    *uri = std::move(*tmp_uri);
    return factory;
  }
  std::string canonical_target = absl::StrCat(g_state->default_prefix, target);
  absl::StatusOr<URI> tmp_uri2 = URI::Parse(canonical_target);
  factory =
      tmp_uri2.ok() ? LookupResolverFactory(tmp_uri2->scheme()) : nullptr;
  if (factory != nullptr) {
    *uri = std::move(*tmp_uri2);
    return factory;
  }
  if (!tmp_uri.ok() || !tmp_uri2.ok()) {
    gpr_log(GPR_ERROR, "%s",
            absl::StrFormat("Error parsing URI(s). '%s':%s; '%s':%s", target,
                            tmp_uri.status().ToString(), canonical_target,
                            tmp_uri2.status().ToString())
                .c_str());
    return nullptr;
  }
  gpr_log(GPR_ERROR, "Don't know how to resolve '%s' or '%s'.",
          std::string(target).c_str(), canonical_target.c_str());
  return nullptr;
}

}  // namespace grpc_core
