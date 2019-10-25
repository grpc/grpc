/*
 *
 * Copyright 2015 gRPC authors.
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

#include "src/core/ext/filters/client_channel/resolver_registry.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

namespace grpc_core {

namespace {

class RegistryState {
 public:
  RegistryState() : default_prefix_(gpr_strdup("dns:///")) {}

  void SetDefaultPrefix(const char* default_resolver_prefix) {
    GPR_ASSERT(default_resolver_prefix != nullptr);
    GPR_ASSERT(*default_resolver_prefix != '\0');
    default_prefix_.reset(gpr_strdup(default_resolver_prefix));
  }

  void RegisterResolverFactory(UniquePtr<ResolverFactory> factory) {
    for (size_t i = 0; i < factories_.size(); ++i) {
      GPR_ASSERT(strcmp(factories_[i]->scheme(), factory->scheme()) != 0);
    }
    factories_.push_back(std::move(factory));
  }

  ResolverFactory* LookupResolverFactory(const char* scheme) const {
    for (size_t i = 0; i < factories_.size(); ++i) {
      if (strcmp(scheme, factories_[i]->scheme()) == 0) {
        return factories_[i].get();
      }
    }
    return nullptr;
  }

  // Returns the factory for the scheme of \a target.  If \a target does
  // not parse as a URI, prepends \a default_prefix_ and tries again.
  // If URI parsing is successful (in either attempt), sets \a uri to
  // point to the parsed URI.
  // If \a default_prefix_ needs to be prepended, sets \a canonical_target
  // to the canonical target string.
  ResolverFactory* FindResolverFactory(const char* target, grpc_uri** uri,
                                       char** canonical_target) const {
    GPR_ASSERT(uri != nullptr);
    *uri = grpc_uri_parse(target, 1);
    ResolverFactory* factory =
        *uri == nullptr ? nullptr : LookupResolverFactory((*uri)->scheme);
    if (factory == nullptr) {
      grpc_uri_destroy(*uri);
      gpr_asprintf(canonical_target, "%s%s", default_prefix_.get(), target);
      *uri = grpc_uri_parse(*canonical_target, 1);
      factory =
          *uri == nullptr ? nullptr : LookupResolverFactory((*uri)->scheme);
      if (factory == nullptr) {
        grpc_uri_destroy(grpc_uri_parse(target, 0));
        grpc_uri_destroy(grpc_uri_parse(*canonical_target, 0));
        gpr_log(GPR_ERROR, "don't know how to resolve '%s' or '%s'", target,
                *canonical_target);
      }
    }
    return factory;
  }

 private:
  // We currently support 10 factories without doing additional
  // allocation.  This number could be raised if there is a case where
  // more factories are needed and the additional allocations are
  // hurting performance (which is unlikely, since these allocations
  // only occur at gRPC initialization time).
  InlinedVector<UniquePtr<ResolverFactory>, 10> factories_;
  UniquePtr<char> default_prefix_;
};

static RegistryState* g_state = nullptr;

}  // namespace

//
// ResolverRegistry::Builder
//

void ResolverRegistry::Builder::InitRegistry() {
  if (g_state == nullptr) g_state = New<RegistryState>();
}

void ResolverRegistry::Builder::ShutdownRegistry() {
  Delete(g_state);
  g_state = nullptr;
}

void ResolverRegistry::Builder::SetDefaultPrefix(
    const char* default_resolver_prefix) {
  InitRegistry();
  g_state->SetDefaultPrefix(default_resolver_prefix);
}

void ResolverRegistry::Builder::RegisterResolverFactory(
    UniquePtr<ResolverFactory> factory) {
  InitRegistry();
  g_state->RegisterResolverFactory(std::move(factory));
}

//
// ResolverRegistry
//

ResolverFactory* ResolverRegistry::LookupResolverFactory(const char* scheme) {
  GPR_ASSERT(g_state != nullptr);
  return g_state->LookupResolverFactory(scheme);
}

bool ResolverRegistry::IsValidTarget(const char* target) {
  grpc_uri* uri = nullptr;
  char* canonical_target = nullptr;
  ResolverFactory* factory =
      g_state->FindResolverFactory(target, &uri, &canonical_target);
  bool result = factory == nullptr ? false : factory->IsValidUri(uri);
  grpc_uri_destroy(uri);
  gpr_free(canonical_target);
  return result;
}

OrphanablePtr<Resolver> ResolverRegistry::CreateResolver(
    const char* target, const grpc_channel_args* args,
    grpc_pollset_set* pollset_set, Combiner* combiner,
    UniquePtr<Resolver::ResultHandler> result_handler) {
  GPR_ASSERT(g_state != nullptr);
  grpc_uri* uri = nullptr;
  char* canonical_target = nullptr;
  ResolverFactory* factory =
      g_state->FindResolverFactory(target, &uri, &canonical_target);
  ResolverArgs resolver_args;
  resolver_args.uri = uri;
  resolver_args.args = args;
  resolver_args.pollset_set = pollset_set;
  resolver_args.combiner = combiner;
  resolver_args.result_handler = std::move(result_handler);
  OrphanablePtr<Resolver> resolver =
      factory == nullptr ? nullptr
                         : factory->CreateResolver(std::move(resolver_args));
  grpc_uri_destroy(uri);
  gpr_free(canonical_target);
  return resolver;
}

UniquePtr<char> ResolverRegistry::GetDefaultAuthority(const char* target) {
  GPR_ASSERT(g_state != nullptr);
  grpc_uri* uri = nullptr;
  char* canonical_target = nullptr;
  ResolverFactory* factory =
      g_state->FindResolverFactory(target, &uri, &canonical_target);
  UniquePtr<char> authority =
      factory == nullptr ? nullptr : factory->GetDefaultAuthority(uri);
  grpc_uri_destroy(uri);
  gpr_free(canonical_target);
  return authority;
}

UniquePtr<char> ResolverRegistry::AddDefaultPrefixIfNeeded(const char* target) {
  GPR_ASSERT(g_state != nullptr);
  grpc_uri* uri = nullptr;
  char* canonical_target = nullptr;
  g_state->FindResolverFactory(target, &uri, &canonical_target);
  grpc_uri_destroy(uri);
  return UniquePtr<char>(canonical_target == nullptr ? gpr_strdup(target)
                                                     : canonical_target);
}

}  // namespace grpc_core
