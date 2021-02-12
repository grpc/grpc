// NOLINT(namespace-envoy)
#include <cstdlib>
#include <iostream>

// We don't use all the headers in the test below, but including them anyway as
// a cheap way to get some C++ compiler sanity checking.
#include "envoy/api/v2/cluster.pb.validate.h"
#include "envoy/api/v2/endpoint.pb.validate.h"
#include "envoy/api/v2/listener.pb.validate.h"
#include "envoy/api/v2/route.pb.validate.h"
#include "envoy/api/v2/core/protocol.pb.validate.h"
#include "envoy/config/health_checker/redis/v2/redis.pb.validate.h"
#include "envoy/config/filter/accesslog/v2/accesslog.pb.validate.h"
#include "envoy/config/filter/http/buffer/v2/buffer.pb.validate.h"
#include "envoy/config/filter/http/fault/v2/fault.pb.validate.h"
#include "envoy/config/filter/http/gzip/v2/gzip.pb.validate.h"
#include "envoy/config/filter/http/health_check/v2/health_check.pb.validate.h"
#include "envoy/config/filter/http/header_to_metadata/v2/header_to_metadata.pb.validate.h"
#include "envoy/config/filter/http/ip_tagging/v2/ip_tagging.pb.validate.h"
#include "envoy/config/filter/http/lua/v2/lua.pb.validate.h"
#include "envoy/config/filter/http/router/v2/router.pb.validate.h"
#include "envoy/config/filter/http/squash/v2/squash.pb.validate.h"
#include "envoy/config/filter/http/transcoder/v2/transcoder.pb.validate.h"
#include "envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.pb.validate.h"
#include "envoy/config/filter/network/mongo_proxy/v2/mongo_proxy.pb.validate.h"
#include "envoy/config/filter/network/redis_proxy/v2/redis_proxy.pb.validate.h"
#include "envoy/config/filter/network/tcp_proxy/v2/tcp_proxy.pb.validate.h"
#include "envoy/api/v2/listener/listener.pb.validate.h"
#include "envoy/api/v2/route/route.pb.validate.h"
#include "envoy/config/bootstrap/v2/bootstrap.pb.validate.h"

#include "google/protobuf/text_format.h"

template <class Proto> struct TestCase {
  void run() {
    std::string err;
    if (Validate(invalid_message, &err)) {
      std::cerr << "Unexpected successful validation of invalid message: "
                << invalid_message.DebugString() << std::endl;
      exit(EXIT_FAILURE);
    }
    if (!Validate(valid_message, &err)) {
      std::cerr << "Unexpected failed validation of valid message: " << valid_message.DebugString()
                << ", " << err << std::endl;
      exit(EXIT_FAILURE);
    }
  }

  Proto& invalid_message;
  Proto& valid_message;
};

// Basic protoc-gen-validate C++ validation header inclusion and Validate calls
// from data plane API.
int main(int argc, char* argv[]) {
  envoy::config::bootstrap::v2::Bootstrap invalid_bootstrap;
  invalid_bootstrap.mutable_static_resources()->add_clusters();
  // This is a baseline test of the validation features we care about. It's
  // probably not worth adding in every filter and field that we want to valid
  // in the API upfront, but as regressions occur, this is the place to add the
  // specific case.
  const std::string valid_bootstrap_text = R"EOF(
  node {}
  cluster_manager {}
  admin {
    access_log_path: "/dev/null"
    address { pipe { path: "/" } }
  }
  )EOF";
  envoy::config::bootstrap::v2::Bootstrap valid_bootstrap;
  if (!google::protobuf::TextFormat::ParseFromString(valid_bootstrap_text, &valid_bootstrap)) {
    std::cerr << "Unable to parse text proto: " << valid_bootstrap_text << std::endl;
    exit(EXIT_FAILURE);
  }
  TestCase<envoy::config::bootstrap::v2::Bootstrap>{invalid_bootstrap, valid_bootstrap}.run();

  exit(EXIT_SUCCESS);
}
