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

#include "src/core/ext/filters/client_channel/resolver_registry.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

namespace grpc_core {

static ResolverRegistry* g_registry = nullptr;

ResolverRegistry* ResolverRegistry::Global() { return g_registry; }

void ResolverRegistry::Init() { g_registry = New<ResolverRegistry>(); }

void ResolverRegistry::Shutdown() {
  Delete(g_registry);
  g_registry = nullptr;
}

ResolverRegistry::ResolverRegistry() : default_prefix_(gpr_strdup("dns:///")) {}

ResolverRegistry::~ResolverRegistry() {}

void ResolverRegistry::SetDefaultPrefix(const char* default_resolver_prefix) {
  GPR_ASSERT(default_resolver_prefix != nullptr);
  GPR_ASSERT(*default_resolver_prefix != '\0');
  default_prefix_.reset(gpr_strdup(default_resolver_prefix));
}

void ResolverRegistry::RegisterResolverFactory(
    UniquePtr<ResolverFactory> factory) {
  for (size_t i = 0; i < factories_.size(); ++i) {
    GPR_ASSERT(strcmp(factories_[i]->scheme(), factory->scheme()) != 0);
  }
  factories_.push_back(std::move(factory));
}

ResolverFactory* ResolverRegistry::LookupResolverFactory(const char* scheme) {
  for (size_t i = 0; i < factories_.size(); ++i) {
    if (strcmp(scheme, factories_[i]->scheme()) == 0) {
      return factories_[i].get();
    }
  }
  return nullptr;
}

ResolverFactory* ResolverRegistry::FindFactory(const char* target,
                                               grpc_uri** uri,
                                               char** canonical_target) {
  GPR_ASSERT(uri != nullptr);
  *uri = grpc_uri_parse(target, 1);
  ResolverFactory* factory =
      *uri == nullptr ? nullptr : LookupResolverFactory((*uri)->scheme);
  if (factory == nullptr) {
    grpc_uri_destroy(*uri);
    gpr_asprintf(canonical_target, "%s%s", default_prefix_.get(), target);
    *uri = grpc_uri_parse(*canonical_target, 1);
    factory = *uri == nullptr ? nullptr : LookupResolverFactory((*uri)->scheme);
    if (factory == nullptr) {
      grpc_uri_destroy(grpc_uri_parse(target, 0));
      grpc_uri_destroy(grpc_uri_parse(*canonical_target, 0));
      gpr_log(GPR_ERROR, "don't know how to resolve '%s' or '%s'", target,
              *canonical_target);
    }
  }
  return factory;
}

OrphanablePtr<Resolver> ResolverRegistry::CreateResolver(
    const char* target, const grpc_channel_args* args,
    grpc_pollset_set* pollset_set, grpc_combiner* combiner) {
  grpc_uri* uri = nullptr;
  char* canonical_target = nullptr;
  ResolverFactory* factory = FindFactory(target, &uri, &canonical_target);
  ResolverArgs resolver_args;
  resolver_args.uri = uri;
  resolver_args.args = args;
  resolver_args.pollset_set = pollset_set;
  resolver_args.combiner = combiner;
  OrphanablePtr<Resolver> resolver =
      factory == nullptr ? nullptr : factory->CreateResolver(resolver_args);
  grpc_uri_destroy(uri);
  gpr_free(canonical_target);
  return resolver;
}

UniquePtr<char> ResolverRegistry::GetDefaultAuthority(const char* target) {
  grpc_uri* uri = nullptr;
  char* canonical_target = nullptr;
  ResolverFactory* factory = FindFactory(target, &uri, &canonical_target);
  UniquePtr<char> authority =
      factory == nullptr ? nullptr : factory->GetDefaultAuthority(uri);
  grpc_uri_destroy(uri);
  gpr_free(canonical_target);
  return authority;
}

UniquePtr<char> ResolverRegistry::AddDefaultPrefixIfNeeded(const char* target) {
  grpc_uri* uri = nullptr;
  char* canonical_target = nullptr;
  FindFactory(target, &uri, &canonical_target);
  grpc_uri_destroy(uri);
  return UniquePtr<char>(canonical_target == nullptr ? gpr_strdup(target)
                                                     : canonical_target);
}

}  // namespace grpc_core
