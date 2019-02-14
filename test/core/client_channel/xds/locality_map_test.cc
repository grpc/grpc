#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <string.h>
#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/ext/filters/client_channel/lb_policy/xds/xds.h"
#include "test/core/util/test_config.h"

// Creation of empty entry in locality map
static void test_locality_create(char* l) {
  LocalityMap test_map;
  GPR_ASSERT(nullptr != test_map.CreateOrUpdateLocality(l, nullptr, nullptr));
  GPR_ASSERT(nullptr != test_map.GetLocalityEntry(l));
  GPR_ASSERT(nullptr == test_map.RetrieveChildPolicy(l));
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  char* policy1 = static_cast<char*>(gpr_malloc(100));
  strncpy(policy1, "policy1", strlen("policy1") + 1);
  test_locality_create(policy1);
  grpc_shutdown();
}
