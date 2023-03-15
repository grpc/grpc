//
//
// Copyright 2016 gRPC authors.
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

#include <string.h>

#include <functional>
#include <initializer_list>
#include <memory>
#include <string>

#include "absl/strings/str_format.h"
#include "absl/types/optional.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/host_port.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/end2end/fixtures/http_proxy_fixture.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

class HttpProxyFilter : public CoreTestFixture {
 public:
  explicit HttpProxyFilter(const grpc_core::ChannelArgs& client_args)
      : proxy_(grpc_end2end_http_proxy_create(client_args.ToC().get())) {}
  ~HttpProxyFilter() override {
    // Need to shut down the proxy users before closing the proxy (otherwise we
    // become stuck).
    ShutdownClient();
    ShutdownServer();
    grpc_end2end_http_proxy_destroy(proxy_);
  }

 private:
  grpc_server* MakeServer(const grpc_core::ChannelArgs& args) override {
    auto* server = grpc_server_create(args.ToC().get(), nullptr);
    grpc_server_register_completion_queue(server, cq(), nullptr);
    grpc_server_credentials* server_creds =
        grpc_insecure_server_credentials_create();
    GPR_ASSERT(
        grpc_server_add_http2_port(server, server_addr_.c_str(), server_creds));
    grpc_server_credentials_release(server_creds);
    grpc_server_start(server);
    return server;
  }

  grpc_channel* MakeClient(const grpc_core::ChannelArgs& args) override {
    // If testing for proxy auth, add credentials to proxy uri
    absl::optional<std::string> proxy_auth_str =
        args.GetOwnedString(GRPC_ARG_HTTP_PROXY_AUTH_CREDS);
    std::string proxy_uri;
    if (!proxy_auth_str.has_value()) {
      proxy_uri = absl::StrFormat(
          "http://%s", grpc_end2end_http_proxy_get_proxy_name(proxy_));
    } else {
      proxy_uri =
          absl::StrFormat("http://%s@%s", proxy_auth_str->c_str(),
                          grpc_end2end_http_proxy_get_proxy_name(proxy_));
    }
    grpc_channel_credentials* creds = grpc_insecure_credentials_create();
    auto* client = grpc_channel_create(
        server_addr_.c_str(), creds,
        args.Set(GRPC_ARG_HTTP_PROXY, proxy_uri).ToC().get());
    grpc_channel_credentials_release(creds);
    GPR_ASSERT(client);
    return client;
  }

  std::string server_addr_ =
      grpc_core::JoinHostPort("localhost", grpc_pick_unused_port_or_die());
  grpc_end2end_http_proxy* proxy_;
};

// All test configurations
static CoreTestConfiguration configs[] = {
    {"chttp2/fullstack",
     FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
         FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
         FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER,
     nullptr,
     [](const grpc_core::ChannelArgs& client_args,
        const grpc_core::ChannelArgs&) {
       return std::make_unique<HttpProxyFilter>(client_args);
     }},
};

int main(int argc, char** argv) {
  size_t i;

  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_end2end_tests_pre_init();
  grpc_init();

  for (i = 0; i < sizeof(configs) / sizeof(*configs); i++) {
    grpc_end2end_tests(argc, argv, configs[i]);
  }

  grpc_shutdown();

  return 0;
}
