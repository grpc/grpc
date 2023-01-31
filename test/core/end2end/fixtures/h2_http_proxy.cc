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

#include <initializer_list>
#include <string>

#include "absl/strings/str_format.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/host_port.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/end2end/fixtures/http_proxy_fixture.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

struct fullstack_fixture_data {
  ~fullstack_fixture_data() { grpc_end2end_http_proxy_destroy(proxy); }
  std::string server_addr;
  grpc_end2end_http_proxy* proxy = nullptr;
};

static grpc_end2end_test_fixture chttp2_create_fixture_fullstack(
    const grpc_channel_args* client_args,
    const grpc_channel_args* /*server_args*/) {
  grpc_end2end_test_fixture f;
  memset(&f, 0, sizeof(f));
  fullstack_fixture_data* ffd = new fullstack_fixture_data();
  const int server_port = grpc_pick_unused_port_or_die();
  ffd->server_addr = grpc_core::JoinHostPort("localhost", server_port);

  // Passing client_args to proxy_create for the case of checking for proxy auth
  //
  ffd->proxy = grpc_end2end_http_proxy_create(client_args);

  f.fixture_data = ffd;
  f.cq = grpc_completion_queue_create_for_next(nullptr);

  return f;
}

void chttp2_init_client_fullstack(grpc_end2end_test_fixture* f,
                                  const grpc_channel_args* client_args) {
  fullstack_fixture_data* ffd =
      static_cast<fullstack_fixture_data*>(f->fixture_data);
  // If testing for proxy auth, add credentials to proxy uri
  const char* proxy_auth_str = grpc_channel_args_find_string(
      client_args, GRPC_ARG_HTTP_PROXY_AUTH_CREDS);
  std::string proxy_uri;
  if (proxy_auth_str == nullptr) {
    proxy_uri = absl::StrFormat(
        "http://%s", grpc_end2end_http_proxy_get_proxy_name(ffd->proxy));
  } else {
    proxy_uri =
        absl::StrFormat("http://%s@%s", proxy_auth_str,
                        grpc_end2end_http_proxy_get_proxy_name(ffd->proxy));
  }
  grpc_channel_credentials* creds = grpc_insecure_credentials_create();
  f->client = grpc_channel_create(ffd->server_addr.c_str(), creds,
                                  grpc_core::ChannelArgs::FromC(client_args)
                                      .Set(GRPC_ARG_HTTP_PROXY, proxy_uri)
                                      .ToC()
                                      .get());
  grpc_channel_credentials_release(creds);
  GPR_ASSERT(f->client);
}

void chttp2_init_server_fullstack(grpc_end2end_test_fixture* f,
                                  const grpc_channel_args* server_args) {
  fullstack_fixture_data* ffd =
      static_cast<fullstack_fixture_data*>(f->fixture_data);
  if (f->server) {
    grpc_server_destroy(f->server);
  }
  f->server = grpc_server_create(server_args, nullptr);
  grpc_server_register_completion_queue(f->server, f->cq, nullptr);
  grpc_server_credentials* server_creds =
      grpc_insecure_server_credentials_create();
  GPR_ASSERT(grpc_server_add_http2_port(f->server, ffd->server_addr.c_str(),
                                        server_creds));
  grpc_server_credentials_release(server_creds);
  grpc_server_start(f->server);
}

void chttp2_tear_down_fullstack(grpc_end2end_test_fixture* f) {
  fullstack_fixture_data* ffd =
      static_cast<fullstack_fixture_data*>(f->fixture_data);
  delete ffd;
}

// All test configurations
static grpc_end2end_test_config configs[] = {
    {"chttp2/fullstack",
     FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
         FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
         FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER,
     nullptr, chttp2_create_fixture_fullstack, chttp2_init_client_fullstack,
     chttp2_init_server_fullstack, chttp2_tear_down_fullstack},
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
