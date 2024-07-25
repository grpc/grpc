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

#include "src/core/resolver/resolver_registry.h"

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

#include <grpc/support/port_platform.h>

namespace grpc_core {

//
// ResolverRegistry::Builder
//

ResolverRegistry::Builder::Builder() { Reset(); }

void ResolverRegistry::Builder::SetDefaultPrefix(std::string default_prefix) {
  state_.default_prefix = std::move(default_prefix);
}

namespace {

bool IsLowerCase(absl::string_view str) {
  for (unsigned char c : str) {
    if (absl::ascii_isalpha(c) && !absl::ascii_islower(c)) return false;
  }
  return true;
}

}  // namespace

void ResolverRegistry::Builder::RegisterResolverFactory(
    std::unique_ptr<ResolverFactory> factory) {
  CHECK(IsLowerCase(factory->scheme()));
  auto p = state_.factories.emplace(factory->scheme(), std::move(factory));
  CHECK(p.second);
}

bool ResolverRegistry::Builder::HasResolverFactory(
    absl::string_view scheme) const {
  return state_.factories.find(scheme) != state_.factories.end();
}

void ResolverRegistry::Builder::Reset() {
  state_.factories.clear();
  state_.default_prefix = "dns:///";
}

ResolverRegistry ResolverRegistry::Builder::Build() {
  return ResolverRegistry(std::move(state_));
}

//
// ResolverRegistry
//

bool ResolverRegistry::IsValidTarget(absl::string_view target) const {
  std::string canonical_target;
  URI uri;
  ResolverFactory* factory =
      FindResolverFactory(target, &uri, &canonical_target);
  if (factory == nullptr) return false;
  return factory->IsValidUri(uri);
}

OrphanablePtr<Resolver> ResolverRegistry::CreateResolver(
    absl::string_view target, const ChannelArgs& args,
    grpc_pollset_set* pollset_set,
    std::shared_ptr<WorkSerializer> work_serializer,
    std::unique_ptr<Resolver::ResultHandler> result_handler) const {
  std::string canonical_target;
  ResolverArgs resolver_args;
  ResolverFactory* factory =
      FindResolverFactory(target, &resolver_args.uri, &canonical_target);
  if (factory == nullptr) return nullptr;
  resolver_args.args = args;
  resolver_args.pollset_set = pollset_set;
  resolver_args.work_serializer = std::move(work_serializer);
  resolver_args.result_handler = std::move(result_handler);
  return factory->CreateResolver(std::move(resolver_args));
}

std::string ResolverRegistry::GetDefaultAuthority(
    absl::string_view target) const {
  std::string canonical_target;
  URI uri;
  ResolverFactory* factory =
      FindResolverFactory(target, &uri, &canonical_target);
  if (factory == nullptr) return "";
  return factory->GetDefaultAuthority(uri);
}

std::string ResolverRegistry::AddDefaultPrefixIfNeeded(
    absl::string_view target) const {
  std::string canonical_target;
  URI uri;
  FindResolverFactory(target, &uri, &canonical_target);
  return canonical_target.empty() ? std::string(target) : canonical_target;
}

ResolverFactory* ResolverRegistry::LookupResolverFactory(
    absl::string_view scheme) const {
  auto it = state_.factories.find(scheme);
  if (it == state_.factories.end()) return nullptr;
  return it->second.get();
}

// Returns the factory for the scheme of \a target.  If \a target does
// not parse as a URI, prepends \a default_prefix_ and tries again.
// If URI parsing is successful (in either attempt), sets \a uri to
// point to the parsed URI.
ResolverFactory* ResolverRegistry::FindResolverFactory(
    absl::string_view target, URI* uri, std::string* canonical_target) const {
  CHECK_NE(uri, nullptr);
  absl::StatusOr<URI> tmp_uri = URI::Parse(target);
  ResolverFactory* factory =
      tmp_uri.ok() ? LookupResolverFactory(tmp_uri->scheme()) : nullptr;
  if (factory != nullptr) {
    *uri = std::move(*tmp_uri);
    return factory;
  }
  *canonical_target = absl::StrCat(state_.default_prefix, target);
  absl::StatusOr<URI> tmp_uri2 = URI::Parse(*canonical_target);
  factory = tmp_uri2.ok() ? LookupResolverFactory(tmp_uri2->scheme()) : nullptr;
  if (factory != nullptr) {
    *uri = std::move(*tmp_uri2);
    return factory;
  }
  if (!tmp_uri.ok() || !tmp_uri2.ok()) {
    LOG(ERROR) << "Error parsing URI(s). '" << target
               << "':" << tmp_uri.status() << "; '" << *canonical_target
               << "':" << tmp_uri2.status();
    return nullptr;
  }
  LOG(ERROR) << "Don't know how to resolve '" << target << "' or '"
             << *canonical_target << "'.";
  return nullptr;
}

}  // namespace grpc_core
