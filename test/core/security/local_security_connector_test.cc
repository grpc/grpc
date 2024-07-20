//
//
// Copyright 2024 gRPC authors.
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

#include "src/core/lib/security/security_connector/tls/tls_security_connector.h"

#include <stdlib.h>
#include <string.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/log/check.h"

#include <grpc/credentials.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/config_vars.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/unique_type_name.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_provider.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h"
#include "src/core/lib/security/credentials/tls/tls_credentials.h"
#include "src/core/tsi/transport_security.h"
#include "test/core/test_util/test_config.h"
#include "test/core/test_util/tls_utils.h"
#include "googletest/include/gtest/gtest.h"

namespace grpc_core {
namespace testing {

class LocalSecurityConnectorTest : public ::testing::Test {
 protected:
  LocalSecurityConnectorTest() {}
};

static void me_read(grpc_endpoint* ep, grpc_slice_buffer* slices,
                    grpc_closure* cb, bool urgent, int min_progress_size) {}

static void me_write(grpc_endpoint* ep, grpc_slice_buffer* slices,
                     grpc_closure* cb, void* arg, int max_frame_size) {}

static void me_add_to_pollset(grpc_endpoint* /*ep*/,
                              grpc_pollset* /*pollset*/) {}

static void me_add_to_pollset_set(grpc_endpoint* /*ep*/,
                                  grpc_pollset_set* /*pollset*/) {}

static void me_delete_from_pollset_set(grpc_endpoint* /*ep*/,
                                       grpc_pollset_set* /*pollset*/) {}

static void me_destroy(grpc_endpoint* ep) {}

static absl::string_view me_get_peer(grpc_endpoint* /*ep*/) {
  return "";
}

static absl::string_view me_get_local_address_unix(grpc_endpoint* /*ep*/) {
  return "unix:";
}

static int me_get_fd(grpc_endpoint* /*ep*/) { return -1; }

static bool me_can_track_err(grpc_endpoint* /*ep*/) { return false; }

static const grpc_endpoint_vtable vtable_unix = {me_read,
                                            me_write,
                                            me_add_to_pollset,
                                            me_add_to_pollset_set,
                                            me_delete_from_pollset_set,
                                            me_destroy,
                                            me_get_peer,
                                            me_get_local_address_unix,
                                            me_get_fd,
                                            me_can_track_err};

static absl::string_view me_get_local_address_local(grpc_endpoint* /*ep*/) {
  return "ipv4:127.0.0.1:12667";
}

static const grpc_endpoint_vtable vtable_local = {me_read,
                                            me_write,
                                            me_add_to_pollset,
                                            me_add_to_pollset_set,
                                            me_delete_from_pollset_set,
                                            me_destroy,
                                            me_get_peer,
                                            me_get_local_address_local,
                                            me_get_fd,
                                            me_can_track_err};

static void PrintAuthContext(const grpc_auth_context* ctx) {
  const grpc_auth_property* p;
  grpc_auth_property_iterator it;
  LOG(INFO) << "\tauthenticated: "
            << (grpc_auth_context_peer_is_authenticated(ctx) ? "YES" : "NO");
  it = grpc_auth_context_peer_identity(ctx);
  while ((p = grpc_auth_property_iterator_next(&it)) != nullptr) {
    LOG(INFO) << "\t\t" << p->name << ": " << p->value;
  }
  LOG(INFO) << "\tall properties:";
  it = grpc_auth_context_property_iterator(ctx);
  while ((p = grpc_auth_property_iterator_next(&it)) != nullptr) {
    LOG(INFO) << "\t\t" << p->name << ": " << p->value;
  }
}

static void check_tsi_security_level(grpc_local_connect_type connect_type,
                                 tsi_security_level level, grpc_endpoint ep) {
  auto server_creds = grpc_local_server_credentials_create(connect_type);
  ChannelArgs args;
  EXPECT_NE(server_creds, nullptr);
  RefCountedPtr<grpc_server_security_connector> connector = server_creds->
      create_security_connector(args);
  EXPECT_NE(connector, nullptr);
  tsi_peer peer;
  CHECK(tsi_construct_peer(0, &peer) == TSI_OK);

  RefCountedPtr<grpc_auth_context> auth_context;
  connector->check_peer(peer, &ep, args, &auth_context, nullptr);
  PrintAuthContext(auth_context.get());
  auto it = grpc_auth_context_find_properties_by_name(
      auth_context.get(), GRPC_TRANSPORT_SECURITY_LEVEL_PROPERTY_NAME);
  const grpc_auth_property* prop = grpc_auth_property_iterator_next(&it);
  ASSERT_NE(prop, nullptr);
  EXPECT_STREQ(prop->value, tsi_security_level_to_string(level));
}

//
// Tests for Certificate Providers in ChannelSecurityConnector.
//

TEST_F(LocalSecurityConnectorTest, CheckUDSType) {
//  auto server_creds = grpc_local_server_credentials_create(LOCAL_TCP);
  grpc_endpoint ep = {
        .vtable = &vtable_unix,
    };
  check_tsi_security_level(UDS, TSI_PRIVACY_AND_INTEGRITY, ep);
}

TEST_F(LocalSecurityConnectorTest, CheckLocalType) {
  grpc_endpoint ep = {
        .vtable = &vtable_local,
    };
  check_tsi_security_level(LOCAL_TCP, TSI_SECURITY_NONE, ep);
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_core::ConfigVars::Overrides overrides;
  grpc_core::ConfigVars::SetOverrides(overrides);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
