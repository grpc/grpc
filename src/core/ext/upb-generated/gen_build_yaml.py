#!/usr/bin/env python2.7

# Copyright 2020 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from __future__ import print_function
import re
import os
import sys
import yaml

out = {}

out['libs'] = [
      {
        'name': 'google_api_upb',
        'build': 'all',
        'language': 'c',
        'src': [
            "src/core/ext/upb-generated/google/api/annotations.upb.c",
            "src/core/ext/upb-generated/google/api/http.upb.c",
            "src/core/ext/upb-generated/google/protobuf/any.upb.c",
            "src/core/ext/upb-generated/google/protobuf/descriptor.upb.c",
            "src/core/ext/upb-generated/google/protobuf/duration.upb.c",
            "src/core/ext/upb-generated/google/protobuf/empty.upb.c",
            "src/core/ext/upb-generated/google/protobuf/struct.upb.c",
            "src/core/ext/upb-generated/google/protobuf/timestamp.upb.c",
            "src/core/ext/upb-generated/google/protobuf/wrappers.upb.c",
            "src/core/ext/upb-generated/google/rpc/status.upb.c",
        ],
        'headers': [
            "src/core/ext/upb-generated/google/api/annotations.upb.h",
            "src/core/ext/upb-generated/google/api/http.upb.h",
            "src/core/ext/upb-generated/google/protobuf/any.upb.h",
            "src/core/ext/upb-generated/google/protobuf/descriptor.upb.h",
            "src/core/ext/upb-generated/google/protobuf/duration.upb.h",
            "src/core/ext/upb-generated/google/protobuf/empty.upb.h",
            "src/core/ext/upb-generated/google/protobuf/struct.upb.h",
            "src/core/ext/upb-generated/google/protobuf/timestamp.upb.h",
            "src/core/ext/upb-generated/google/protobuf/wrappers.upb.h",
            "src/core/ext/upb-generated/google/rpc/status.upb.h",
        ],
        'secure': False,
        'deps': [
            'upb_lib',
        ],
    },

      {
        'name': 'proto_gen_validate_upb',
        'build': 'all',
        'language': 'c',
        'src': [
            "src/core/ext/upb-generated/gogoproto/gogo.upb.c",
            "src/core/ext/upb-generated/validate/validate.upb.c",
        ],
        'headers': [
            "src/core/ext/upb-generated/gogoproto/gogo.upb.h",
            "src/core/ext/upb-generated/validate/validate.upb.h",
        ],
        'secure': False,
        'deps': [
            'google_api_upb',
            'upb_lib',
        ],
    },

      {
        'name': 'envoy_annotations_upb',
        'build': 'all',
        'language': 'c',
        'src': [
            "src/core/ext/upb-generated/envoy/annotations/deprecation.upb.c",
            "src/core/ext/upb-generated/envoy/annotations/resource.upb.c",
        ],
        'headers': [
            "src/core/ext/upb-generated/envoy/annotations/deprecation.upb.h",
            "src/core/ext/upb-generated/envoy/annotations/resource.upb.h",
        ],
        'secure': False,
        'deps': [
            'google_api_upb',
            'upb_lib',
        ],
    },

      {
        'name': 'udpa_annotations_upb',
        'build': 'all',
        'language': 'c',
        'src': [
        "src/core/ext/upb-generated/udpa/annotations/migrate.upb.c",
        "src/core/ext/upb-generated/udpa/annotations/sensitive.upb.c",
        "src/core/ext/upb-generated/udpa/annotations/status.upb.c",
        ],
        'headers': [
        "src/core/ext/upb-generated/udpa/annotations/migrate.upb.h",
        "src/core/ext/upb-generated/udpa/annotations/sensitive.upb.h",
        "src/core/ext/upb-generated/udpa/annotations/status.upb.h",
        ],
        'secure': False,
        'deps': [
            'google_api_upb',
            'upb_lib',
        ],
    },

      {
        'name': 'envoy_type_upb',
        'build': 'all',
        'language': 'c',
        'src': [
         "src/core/ext/upb-generated/envoy/type/http.upb.c",
        "src/core/ext/upb-generated/envoy/type/matcher/regex.upb.c",
        "src/core/ext/upb-generated/envoy/type/matcher/string.upb.c",
        "src/core/ext/upb-generated/envoy/type/metadata/v2/metadata.upb.c",
        "src/core/ext/upb-generated/envoy/type/percent.upb.c",
        "src/core/ext/upb-generated/envoy/type/range.upb.c",
        "src/core/ext/upb-generated/envoy/type/semantic_version.upb.c",
        "src/core/ext/upb-generated/envoy/type/tracing/v2/custom_tag.upb.c",

        ],
        'headers': [
         "src/core/ext/upb-generated/envoy/type/http.upb.h",
        "src/core/ext/upb-generated/envoy/type/matcher/regex.upb.h",
        "src/core/ext/upb-generated/envoy/type/matcher/string.upb.h",
        "src/core/ext/upb-generated/envoy/type/metadata/v2/metadata.upb.h",
        "src/core/ext/upb-generated/envoy/type/percent.upb.h",
        "src/core/ext/upb-generated/envoy/type/range.upb.h",
        "src/core/ext/upb-generated/envoy/type/semantic_version.upb.h",
        "src/core/ext/upb-generated/envoy/type/tracing/v2/custom_tag.upb.h",
        ],
        'secure': False,
        'deps': [
            'envoy_annotations_upb',
            'google_api_upb',
            'proto_gen_validate_upb',
            'udpa_annotations_upb',
            'upb_lib',
        ],
    },

      {
        'name': 'envoy_core_upb',
        'build': 'all',
        'language': 'c',
        'src': [
            "src/core/ext/upb-generated/envoy/api/v2/core/address.upb.c",
            "src/core/ext/upb-generated/envoy/api/v2/core/backoff.upb.c",
            "src/core/ext/upb-generated/envoy/api/v2/core/base.upb.c",
            "src/core/ext/upb-generated/envoy/api/v2/core/config_source.upb.c",
            "src/core/ext/upb-generated/envoy/api/v2/core/event_service_config.upb.c",
            "src/core/ext/upb-generated/envoy/api/v2/core/grpc_service.upb.c",
            "src/core/ext/upb-generated/envoy/api/v2/core/health_check.upb.c",
            "src/core/ext/upb-generated/envoy/api/v2/core/http_uri.upb.c",
            "src/core/ext/upb-generated/envoy/api/v2/core/protocol.upb.c",
            "src/core/ext/upb-generated/envoy/api/v2/core/socket_option.upb.c",
        ],
        'headers': [
            "src/core/ext/upb-generated/envoy/api/v2/core/address.upb.h",
            "src/core/ext/upb-generated/envoy/api/v2/core/backoff.upb.h",
            "src/core/ext/upb-generated/envoy/api/v2/core/base.upb.h",
            "src/core/ext/upb-generated/envoy/api/v2/core/config_source.upb.h",
            "src/core/ext/upb-generated/envoy/api/v2/core/event_service_config.upb.h",
            "src/core/ext/upb-generated/envoy/api/v2/core/grpc_service.upb.h",
            "src/core/ext/upb-generated/envoy/api/v2/core/health_check.upb.h",
            "src/core/ext/upb-generated/envoy/api/v2/core/http_uri.upb.h",
            "src/core/ext/upb-generated/envoy/api/v2/core/protocol.upb.h",
            "src/core/ext/upb-generated/envoy/api/v2/core/socket_option.upb.h",
        ],
        'secure': False,
        'deps': [
            'envoy_annotations_upb',
            'envoy_type_upb',
            'google_api_upb',
            'proto_gen_validate_upb',
            'udpa_annotations_upb',
            'upb_lib',
        ],
    },

      {
        'name': 'xds_ads_upb_proto',
        'build': 'all',
        'language': 'c',
        'src': [
            "src/core/ext/upb-generated/envoy/api/v2/auth/cert.upb.c",
            "src/core/ext/upb-generated/envoy/api/v2/auth/common.upb.c",
            "src/core/ext/upb-generated/envoy/api/v2/auth/secret.upb.c",
            "src/core/ext/upb-generated/envoy/api/v2/auth/tls.upb.c",
            "src/core/ext/upb-generated/envoy/api/v2/cds.upb.c",
            "src/core/ext/upb-generated/envoy/api/v2/cluster.upb.c",
            "src/core/ext/upb-generated/envoy/api/v2/cluster/circuit_breaker.upb.c",
            "src/core/ext/upb-generated/envoy/api/v2/cluster/filter.upb.c",
            "src/core/ext/upb-generated/envoy/api/v2/cluster/outlier_detection.upb.c",
            "src/core/ext/upb-generated/envoy/api/v2/discovery.upb.c",
            "src/core/ext/upb-generated/envoy/api/v2/eds.upb.c",
            "src/core/ext/upb-generated/envoy/api/v2/endpoint.upb.c",
            "src/core/ext/upb-generated/envoy/api/v2/endpoint/endpoint.upb.c",
            "src/core/ext/upb-generated/envoy/api/v2/endpoint/endpoint_components.upb.c",
            "src/core/ext/upb-generated/envoy/api/v2/endpoint/load_report.upb.c",
            "src/core/ext/upb-generated/envoy/api/v2/lds.upb.c",
            "src/core/ext/upb-generated/envoy/api/v2/listener.upb.c",
            "src/core/ext/upb-generated/envoy/api/v2/listener/listener.upb.c",
            "src/core/ext/upb-generated/envoy/api/v2/listener/listener_components.upb.c",
            "src/core/ext/upb-generated/envoy/api/v2/listener/udp_listener_config.upb.c",
            "src/core/ext/upb-generated/envoy/api/v2/rds.upb.c",
            "src/core/ext/upb-generated/envoy/api/v2/route.upb.c",
            "src/core/ext/upb-generated/envoy/api/v2/route/route.upb.c",
            "src/core/ext/upb-generated/envoy/api/v2/route/route_components.upb.c",
            "src/core/ext/upb-generated/envoy/api/v2/scoped_route.upb.c",
            "src/core/ext/upb-generated/envoy/api/v2/srds.upb.c",
            "src/core/ext/upb-generated/envoy/config/filter/accesslog/v2/accesslog.upb.c",
            "src/core/ext/upb-generated/envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.upb.c",
            "src/core/ext/upb-generated/envoy/config/listener/v2/api_listener.upb.c",
            "src/core/ext/upb-generated/envoy/config/trace/v2/http_tracer.upb.c",
            "src/core/ext/upb-generated/envoy/service/discovery/v2/ads.upb.c",
            "src/core/ext/upb-generated/envoy/service/load_stats/v2/lrs.upb.c",
        ],
        'headers': [
            "src/core/ext/upb-generated/envoy/api/v2/auth/cert.upb.h",
            "src/core/ext/upb-generated/envoy/api/v2/auth/common.upb.h",
            "src/core/ext/upb-generated/envoy/api/v2/auth/secret.upb.h",
            "src/core/ext/upb-generated/envoy/api/v2/auth/tls.upb.h",
            "src/core/ext/upb-generated/envoy/api/v2/cds.upb.h",
            "src/core/ext/upb-generated/envoy/api/v2/cluster.upb.h",
            "src/core/ext/upb-generated/envoy/api/v2/cluster/circuit_breaker.upb.h",
            "src/core/ext/upb-generated/envoy/api/v2/cluster/filter.upb.h",
            "src/core/ext/upb-generated/envoy/api/v2/cluster/outlier_detection.upb.h",
            "src/core/ext/upb-generated/envoy/api/v2/discovery.upb.h",
            "src/core/ext/upb-generated/envoy/api/v2/eds.upb.h",
            "src/core/ext/upb-generated/envoy/api/v2/endpoint.upb.h",
            "src/core/ext/upb-generated/envoy/api/v2/endpoint/endpoint.upb.h",
            "src/core/ext/upb-generated/envoy/api/v2/endpoint/endpoint_components.upb.h",
            "src/core/ext/upb-generated/envoy/api/v2/endpoint/load_report.upb.h",
            "src/core/ext/upb-generated/envoy/api/v2/lds.upb.h",
            "src/core/ext/upb-generated/envoy/api/v2/listener.upb.h",
            "src/core/ext/upb-generated/envoy/api/v2/listener/listener.upb.h",
            "src/core/ext/upb-generated/envoy/api/v2/listener/listener_components.upb.h",
            "src/core/ext/upb-generated/envoy/api/v2/listener/udp_listener_config.upb.h",
            "src/core/ext/upb-generated/envoy/api/v2/rds.upb.h",
            "src/core/ext/upb-generated/envoy/api/v2/route.upb.h",
            "src/core/ext/upb-generated/envoy/api/v2/route/route.upb.h",
            "src/core/ext/upb-generated/envoy/api/v2/route/route_components.upb.h",
            "src/core/ext/upb-generated/envoy/api/v2/scoped_route.upb.h",
            "src/core/ext/upb-generated/envoy/api/v2/srds.upb.h",
            "src/core/ext/upb-generated/envoy/config/filter/accesslog/v2/accesslog.upb.h",
            "src/core/ext/upb-generated/envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.upb.h",
            "src/core/ext/upb-generated/envoy/config/listener/v2/api_listener.upb.h",
            "src/core/ext/upb-generated/envoy/config/trace/v2/http_tracer.upb.h",
            "src/core/ext/upb-generated/envoy/service/discovery/v2/ads.upb.h",
            "src/core/ext/upb-generated/envoy/service/load_stats/v2/lrs.upb.h",
        ],
        'secure': False,
        'deps': [
            'envoy_annotations_upb',
            'envoy_core_upb',
            'envoy_type_upb',
            'google_api_upb',
            'proto_gen_validate_upb',
            'udpa_annotations_upb',
            'upb_lib',
        ],
    },


      {
        'name': 'xds_hcm_upb_proto',
        'build': 'all',
        'language': 'c',
        'secure': False,
        'deps': [
            'xds_ads_upb_proto',
            'upb_lib',
        ],
    },

      {
        'name': 'xds_lrs_upb_proto',
        'build': 'all',
        'language': 'c',
        'secure': False,
        'deps': [
            'xds_ads_upb_proto',
            'upb_lib',
        ],
    },


      {
        'name': 'udpa_orca_upb',
        'build': 'all',
        'language': 'c',
        'src': [
            "src/core/ext/upb-generated/udpa/data/orca/v1/orca_load_report.upb.c",
        ],
        'headers': [
            "src/core/ext/upb-generated/udpa/data/orca/v1/orca_load_report.upb.h",
        ],
        'secure': False,
        'deps': [
            'proto_gen_validate_upb',
            'upb_lib',
        ],
    },

      {
        'name': 'grpc_health_upb_proto',
        'build': 'all',
        'language': 'c',
        'src': [
        "src/core/ext/upb-generated/src/proto/grpc/health/v1/health.upb.c",
        ],
        'headers': [
        "src/core/ext/upb-generated/src/proto/grpc/health/v1/health.upb.h",
        ],
        'secure': False,
        'deps': [
            'upb_lib',
        ],
    },

      {
        'name': 'grpc_lb_upb_proto',
        'build': 'all',
        'language': 'c',
        'src': [
            "src/core/ext/upb-generated/src/proto/grpc/lb/v1/load_balancer.upb.c",
        ],
        'headers': [
            "src/core/ext/upb-generated/src/proto/grpc/lb/v1/load_balancer.upb.h",
        ],
        'secure': False,
        'deps': [
            'google_api_upb',
            'upb_lib',
        ],
    },

      {
        'name': 'alts_upb_proto',
        'build': 'all',
        'language': 'c',
        'src': [
        "src/core/ext/upb-generated/src/proto/grpc/gcp/altscontext.upb.c",
        "src/core/ext/upb-generated/src/proto/grpc/gcp/handshaker.upb.c",
        "src/core/ext/upb-generated/src/proto/grpc/gcp/transport_security_common.upb.c",

        ],
        'headers': [
        "src/core/ext/upb-generated/src/proto/grpc/gcp/altscontext.upb.h",
        "src/core/ext/upb-generated/src/proto/grpc/gcp/handshaker.upb.h",
        "src/core/ext/upb-generated/src/proto/grpc/gcp/transport_security_common.upb.h",
        ],
        'secure': False,
        'deps': [
            'upb_lib',
        ],
    },



]

print(yaml.dump(out))
