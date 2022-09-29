/*
 *
 * Copyright 2018 gRPC authors.
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

#include "src/core/lib/iomgr/resolve_address.h"

#include "absl/strings/str_cat.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gprpp/no_destruct.h"

namespace grpc_core {
const char* kDefaultSecurePort = "https";

namespace {
NoDestruct<std::shared_ptr<DNSResolver>> g_dns_resolver;
}

constexpr DNSResolver::TaskHandle DNSResolver::kNullHandle;

void ResetDNSResolver(std::shared_ptr<DNSResolver> resolver) {
  *g_dns_resolver = std::move(resolver);
}

std::shared_ptr<DNSResolver> GetDNSResolver() { return *g_dns_resolver; }

std::string DNSResolver::HandleToString(TaskHandle handle) {
  return absl::StrCat("{", handle.keys[0], ",", handle.keys[1], "}");
}

}  // namespace grpc_core
