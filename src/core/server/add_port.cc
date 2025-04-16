// Copyright 2025 gRPC authors.
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

#include <grpc/grpc.h>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "src/core/config/core_configuration.h"
#include "src/core/credentials/transport/transport_credentials.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/server/server.h"

// TODO(ctiller): rename to grpc_server_add_listener_port.
int grpc_server_add_http2_port(grpc_server* server, const char* addr,
                               grpc_server_credentials* creds) {
  grpc_core::ExecCtx exec_ctx;
  grpc_core::RefCountedPtr<grpc_server_security_connector> sc;
  grpc_core::Server* core_server = grpc_core::Server::FromC(server);
  grpc_core::ChannelArgs args = core_server->channel_args();
  GRPC_TRACE_LOG(api, INFO) << "grpc_server_add_http2_port(server=" << server
                            << ", addr=" << addr << ", creds=" << creds << ")";
  if (creds == nullptr) {
    LOG(ERROR)
        << "Failed to add port to server: No credentials specified for secure "
           "server port (creds==NULL)";
    return 0;
  }
  // TODO(yashykt): Ideally, we would not want to have different behavior here
  // based on whether a config fetcher is configured or not. Currently, we have
  // a feature for SSL credentials reloading with an application callback that
  // assumes that there is a single security connector. If we delay the creation
  // of the security connector to after the creation of the listener(s), we
  // would have potentially multiple security connectors which breaks the
  // assumption for SSL creds reloading. When the API for SSL creds reloading is
  // rewritten, we would be able to make this workaround go away by removing
  // that assumption. As an immediate drawback of this workaround, config
  // fetchers need to be registered before adding ports to the server.
  if (core_server->config_fetcher() != nullptr) {
    // Create channel args.
    args = args.SetObject(creds->Ref());
  } else {
    sc = creds->create_security_connector(grpc_core::ChannelArgs());
    if (sc == nullptr) {
      LOG(ERROR) << "Unable to create secure server with credentials of type ",
          creds->type().name();
      return 0;
    }
    args = args.SetObject(creds->Ref()).SetObject(sc);
  }
  std::vector<absl::string_view> transport_preferences = absl::StrSplit(
      args.GetString(GRPC_ARG_PREFERRED_TRANSPORT_PROTOCOLS).value_or("h2"),
      ',');
  if (transport_preferences.size() != 1) {
    LOG(ERROR) << "Failed to add port to server: "
                  "Only one preferred transport name is currently supported: "
                  "requested='"
               << *args.GetString(GRPC_ARG_PREFERRED_TRANSPORT_PROTOCOLS)
               << "'";
    return 0;
  }
  auto* transport = grpc_core::CoreConfiguration::Get()
                        .endpoint_transport_registry()
                        .GetTransport(transport_preferences[0]);
  if (transport == nullptr) {
    LOG(ERROR) << "Failed to add port to server: unknown protocol '"
               << transport_preferences[0] << "'";
    return 0;
  }
  auto r = transport->AddPort(core_server, addr, args);
  if (!r.ok()) {
    LOG(ERROR) << "Failed to add port to server: " << r.status().message();
    return 0;
  }
  return *r;
}
