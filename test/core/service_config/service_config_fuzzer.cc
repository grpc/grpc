// grpc_service_config_fuzzer.cc
//
// Fuzz target for gRPC service config parser.
// gRPC clients accept a JSON service config string (from DNS TXT records,
// or xDS management servers) that controls:
//   - method configs (timeout, wait_for_ready, max_req/resp size, retry)
//   - load balancing policy selection and parameters
//   - health checking, message size limits
//
// None of these JSON parsers are currently covered by OSS-Fuzz.
// The entry point is ServiceConfigImpl::Create() in
// src/core/service_config/service_config_impl.cc.
//
// A bug in the JSON parser or method config deserialisation could allow
// a malicious resolver (DNS rebinding, compromised xDS server) to cause
// heap corruption or DoS in gRPC clients.

#include <stddef.h>
#include <stdint.h>
#include <string>

#include "absl/strings/string_view.h"
#include "src/core/service_config/service_config_impl.h"
#include "src/core/util/ref_counted_ptr.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    std::string json_str(reinterpret_cast<const char *>(data), size);

    // ParseJsonServiceConfig validates and parses the JSON service config
    // exercising: JSON tokeniser, method config parser, LB policy parser,
    // retry policy, timeout parsing, and the registered config parsers for
    // each gRPC channel policy (round_robin, pick_first, weighted_round_robin,
    // ring_hash, xds_cluster_impl, etc.)
    grpc_core::ServiceConfigImpl::Create(
        grpc_core::ChannelArgs(), json_str).IgnoreError();

    return 0;
}
