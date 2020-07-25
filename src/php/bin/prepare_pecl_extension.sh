#!/bin/bash
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

# This is a generated file. DO NOT EDIT!

# See more details at https://github.com/grpc/grpc/issues/23307.
#
# The PHP build process, particularly the `./configure` step, will
# turn pair of files with names like "a.upb.c" and "a.upbdefs.c" into
# conflicting Makefile targets like "a.lo".
#
# In order to avoid the "multiple definition of <symbol>" problem, this
# script will do some renaming first, before we build the PECL extension
# source distribution archive.
#
# Caller of this script is expected to run `pear package` to build the
# grpc-<version>.tgz PECL extension archive after running this script.
# After building the .tgz extension archive, caller is expected to
# clean up those temporary changes caused by this script separately.

set -e
cd $(dirname $0)/../../..

# Rename files from *.upbdefs.c|h to *_upbdefs.c|h
mv src/core/ext/upb-generated/envoy/type/percent.upbdefs.h \
   src/core/ext/upb-generated/envoy/type/percent_upbdefs.h
mv src/core/ext/upb-generated/gogoproto/gogo.upbdefs.h \
   src/core/ext/upb-generated/gogoproto/gogo_upbdefs.h
mv src/core/ext/upb-generated/envoy/api/v2/scoped_route.upbdefs.c \
   src/core/ext/upb-generated/envoy/api/v2/scoped_route_upbdefs.c
mv src/core/ext/upb-generated/envoy/api/v2/scoped_route.upbdefs.h \
   src/core/ext/upb-generated/envoy/api/v2/scoped_route_upbdefs.h
mv src/core/ext/upb-generated/gogoproto/gogo.upbdefs.c \
   src/core/ext/upb-generated/gogoproto/gogo_upbdefs.c
mv src/core/ext/upb-generated/envoy/api/v2/route/route_components.upbdefs.h \
   src/core/ext/upb-generated/envoy/api/v2/route/route_components_upbdefs.h
mv src/core/ext/upb-generated/envoy/api/v2/route/route.upbdefs.h \
   src/core/ext/upb-generated/envoy/api/v2/route/route_upbdefs.h
mv src/core/ext/upb-generated/envoy/api/v2/cds.upbdefs.c \
   src/core/ext/upb-generated/envoy/api/v2/cds_upbdefs.c
mv src/core/ext/upb-generated/envoy/config/trace/v2/http_tracer.upbdefs.c \
   src/core/ext/upb-generated/envoy/config/trace/v2/http_tracer_upbdefs.c
mv src/core/ext/upb-generated/envoy/api/v2/route/route_components.upbdefs.c \
   src/core/ext/upb-generated/envoy/api/v2/route/route_components_upbdefs.c
mv src/core/ext/upb-generated/google/api/http.upbdefs.h \
   src/core/ext/upb-generated/google/api/http_upbdefs.h
mv src/core/ext/upb-generated/envoy/api/v2/srds.upbdefs.h \
   src/core/ext/upb-generated/envoy/api/v2/srds_upbdefs.h
mv src/core/ext/upb-generated/envoy/api/v2/cds.upbdefs.h \
   src/core/ext/upb-generated/envoy/api/v2/cds_upbdefs.h
mv src/core/ext/upb-generated/envoy/api/v2/route/route.upbdefs.c \
   src/core/ext/upb-generated/envoy/api/v2/route/route_upbdefs.c
mv src/core/ext/upb-generated/envoy/api/v2/listener/udp_listener_config.upbdefs.h \
   src/core/ext/upb-generated/envoy/api/v2/listener/udp_listener_config_upbdefs.h
mv src/core/ext/upb-generated/envoy/api/v2/listener/udp_listener_config.upbdefs.c \
   src/core/ext/upb-generated/envoy/api/v2/listener/udp_listener_config_upbdefs.c
mv src/core/ext/upb-generated/envoy/api/v2/core/http_uri.upbdefs.h \
   src/core/ext/upb-generated/envoy/api/v2/core/http_uri_upbdefs.h
mv src/core/ext/upb-generated/envoy/service/discovery/v2/ads.upbdefs.h \
   src/core/ext/upb-generated/envoy/service/discovery/v2/ads_upbdefs.h
mv src/core/ext/upb-generated/envoy/service/discovery/v2/ads.upbdefs.c \
   src/core/ext/upb-generated/envoy/service/discovery/v2/ads_upbdefs.c
mv src/core/ext/upb-generated/envoy/api/v2/core/http_uri.upbdefs.c \
   src/core/ext/upb-generated/envoy/api/v2/core/http_uri_upbdefs.c
mv src/core/ext/upb-generated/udpa/annotations/status.upbdefs.c \
   src/core/ext/upb-generated/udpa/annotations/status_upbdefs.c
mv src/core/ext/upb-generated/envoy/config/filter/accesslog/v2/accesslog.upbdefs.c \
   src/core/ext/upb-generated/envoy/config/filter/accesslog/v2/accesslog_upbdefs.c
mv src/core/ext/upb-generated/envoy/api/v2/endpoint/load_report.upbdefs.h \
   src/core/ext/upb-generated/envoy/api/v2/endpoint/load_report_upbdefs.h
mv src/core/ext/upb-generated/envoy/type/matcher/string.upbdefs.c \
   src/core/ext/upb-generated/envoy/type/matcher/string_upbdefs.c
mv src/core/ext/upb-generated/envoy/api/v2/endpoint/load_report.upbdefs.c \
   src/core/ext/upb-generated/envoy/api/v2/endpoint/load_report_upbdefs.c
mv src/core/ext/upb-generated/envoy/config/filter/accesslog/v2/accesslog.upbdefs.h \
   src/core/ext/upb-generated/envoy/config/filter/accesslog/v2/accesslog_upbdefs.h
mv src/core/ext/upb-generated/udpa/annotations/status.upbdefs.h \
   src/core/ext/upb-generated/udpa/annotations/status_upbdefs.h
mv src/core/ext/upb-generated/udpa/annotations/migrate.upbdefs.h \
   src/core/ext/upb-generated/udpa/annotations/migrate_upbdefs.h
mv src/core/ext/upb-generated/envoy/api/v2/core/config_source.upbdefs.c \
   src/core/ext/upb-generated/envoy/api/v2/core/config_source_upbdefs.c
mv src/core/ext/upb-generated/envoy/api/v2/core/config_source.upbdefs.h \
   src/core/ext/upb-generated/envoy/api/v2/core/config_source_upbdefs.h
mv src/core/ext/upb-generated/udpa/annotations/migrate.upbdefs.c \
   src/core/ext/upb-generated/udpa/annotations/migrate_upbdefs.c
mv src/core/ext/upb-generated/google/protobuf/empty.upbdefs.h \
   src/core/ext/upb-generated/google/protobuf/empty_upbdefs.h
mv src/core/ext/upb-generated/envoy/api/v2/discovery.upbdefs.h \
   src/core/ext/upb-generated/envoy/api/v2/discovery_upbdefs.h
mv src/core/ext/upb-generated/google/protobuf/empty.upbdefs.c \
   src/core/ext/upb-generated/google/protobuf/empty_upbdefs.c
mv src/core/ext/upb-generated/envoy/type/http.upbdefs.h \
   src/core/ext/upb-generated/envoy/type/http_upbdefs.h
mv src/core/ext/upb-generated/envoy/api/v2/discovery.upbdefs.c \
   src/core/ext/upb-generated/envoy/api/v2/discovery_upbdefs.c
mv src/core/ext/upb-generated/envoy/api/v2/rds.upbdefs.c \
   src/core/ext/upb-generated/envoy/api/v2/rds_upbdefs.c
mv src/core/ext/upb-generated/envoy/api/v2/rds.upbdefs.h \
   src/core/ext/upb-generated/envoy/api/v2/rds_upbdefs.h
mv src/core/ext/upb-generated/envoy/api/v2/core/address.upbdefs.c \
   src/core/ext/upb-generated/envoy/api/v2/core/address_upbdefs.c
mv src/core/ext/upb-generated/google/protobuf/descriptor.upbdefs.h \
   src/core/ext/upb-generated/google/protobuf/descriptor_upbdefs.h
mv src/core/ext/upb-generated/envoy/api/v2/auth/tls.upbdefs.c \
   src/core/ext/upb-generated/envoy/api/v2/auth/tls_upbdefs.c
mv src/core/ext/upb-generated/envoy/api/v2/auth/tls.upbdefs.h \
   src/core/ext/upb-generated/envoy/api/v2/auth/tls_upbdefs.h
mv src/core/ext/upb-generated/envoy/type/http.upbdefs.c \
   src/core/ext/upb-generated/envoy/type/http_upbdefs.c
mv src/core/ext/upb-generated/envoy/api/v2/srds.upbdefs.c \
   src/core/ext/upb-generated/envoy/api/v2/srds_upbdefs.c
mv src/core/ext/upb-generated/envoy/api/v2/cluster/circuit_breaker.upbdefs.h \
   src/core/ext/upb-generated/envoy/api/v2/cluster/circuit_breaker_upbdefs.h
mv src/core/ext/upb-generated/envoy/api/v2/cluster/circuit_breaker.upbdefs.c \
   src/core/ext/upb-generated/envoy/api/v2/cluster/circuit_breaker_upbdefs.c
mv src/core/ext/upb-generated/envoy/api/v2/auth/cert.upbdefs.h \
   src/core/ext/upb-generated/envoy/api/v2/auth/cert_upbdefs.h
mv src/core/ext/upb-generated/envoy/api/v2/core/base.upbdefs.c \
   src/core/ext/upb-generated/envoy/api/v2/core/base_upbdefs.c
mv src/core/ext/upb-generated/udpa/annotations/sensitive.upbdefs.c \
   src/core/ext/upb-generated/udpa/annotations/sensitive_upbdefs.c
mv src/core/ext/upb-generated/envoy/api/v2/core/base.upbdefs.h \
   src/core/ext/upb-generated/envoy/api/v2/core/base_upbdefs.h
mv src/core/ext/upb-generated/envoy/config/trace/v2/http_tracer.upbdefs.h \
   src/core/ext/upb-generated/envoy/config/trace/v2/http_tracer_upbdefs.h
mv src/core/ext/upb-generated/envoy/api/v2/auth/cert.upbdefs.c \
   src/core/ext/upb-generated/envoy/api/v2/auth/cert_upbdefs.c
mv src/core/ext/upb-generated/udpa/annotations/sensitive.upbdefs.h \
   src/core/ext/upb-generated/udpa/annotations/sensitive_upbdefs.h
mv src/core/ext/upb-generated/envoy/config/listener/v2/api_listener.upbdefs.c \
   src/core/ext/upb-generated/envoy/config/listener/v2/api_listener_upbdefs.c
mv src/core/ext/upb-generated/envoy/api/v2/eds.upbdefs.h \
   src/core/ext/upb-generated/envoy/api/v2/eds_upbdefs.h
mv src/core/ext/upb-generated/envoy/config/listener/v2/api_listener.upbdefs.h \
   src/core/ext/upb-generated/envoy/config/listener/v2/api_listener_upbdefs.h
mv src/core/ext/upb-generated/google/protobuf/timestamp.upbdefs.c \
   src/core/ext/upb-generated/google/protobuf/timestamp_upbdefs.c
mv src/core/ext/upb-generated/google/protobuf/timestamp.upbdefs.h \
   src/core/ext/upb-generated/google/protobuf/timestamp_upbdefs.h
mv src/core/ext/upb-generated/envoy/api/v2/eds.upbdefs.c \
   src/core/ext/upb-generated/envoy/api/v2/eds_upbdefs.c
mv src/core/ext/upb-generated/envoy/api/v2/endpoint/endpoint_components.upbdefs.c \
   src/core/ext/upb-generated/envoy/api/v2/endpoint/endpoint_components_upbdefs.c
mv src/core/ext/upb-generated/envoy/api/v2/endpoint/endpoint_components.upbdefs.h \
   src/core/ext/upb-generated/envoy/api/v2/endpoint/endpoint_components_upbdefs.h
mv src/core/ext/upb-generated/envoy/api/v2/core/health_check.upbdefs.h \
   src/core/ext/upb-generated/envoy/api/v2/core/health_check_upbdefs.h
mv src/core/ext/upb-generated/envoy/type/range.upbdefs.c \
   src/core/ext/upb-generated/envoy/type/range_upbdefs.c
mv src/core/ext/upb-generated/envoy/api/v2/core/health_check.upbdefs.c \
   src/core/ext/upb-generated/envoy/api/v2/core/health_check_upbdefs.c
mv src/core/ext/upb-generated/envoy/api/v2/endpoint/endpoint.upbdefs.h \
   src/core/ext/upb-generated/envoy/api/v2/endpoint/endpoint_upbdefs.h
mv src/core/ext/upb-generated/envoy/api/v2/endpoint/endpoint.upbdefs.c \
   src/core/ext/upb-generated/envoy/api/v2/endpoint/endpoint_upbdefs.c
mv src/core/ext/upb-generated/envoy/api/v2/route.upbdefs.c \
   src/core/ext/upb-generated/envoy/api/v2/route_upbdefs.c
mv src/core/ext/upb-generated/envoy/api/v2/route.upbdefs.h \
   src/core/ext/upb-generated/envoy/api/v2/route_upbdefs.h
mv src/core/ext/upb-generated/envoy/type/range.upbdefs.h \
   src/core/ext/upb-generated/envoy/type/range_upbdefs.h
mv src/core/ext/upb-generated/google/protobuf/wrappers.upbdefs.c \
   src/core/ext/upb-generated/google/protobuf/wrappers_upbdefs.c
mv src/core/ext/upb-generated/envoy/api/v2/core/address.upbdefs.h \
   src/core/ext/upb-generated/envoy/api/v2/core/address_upbdefs.h
mv src/core/ext/upb-generated/envoy/api/v2/core/grpc_service.upbdefs.h \
   src/core/ext/upb-generated/envoy/api/v2/core/grpc_service_upbdefs.h
mv src/core/ext/upb-generated/envoy/api/v2/auth/common.upbdefs.c \
   src/core/ext/upb-generated/envoy/api/v2/auth/common_upbdefs.c
mv src/core/ext/upb-generated/envoy/type/tracing/v2/custom_tag.upbdefs.c \
   src/core/ext/upb-generated/envoy/type/tracing/v2/custom_tag_upbdefs.c
mv src/core/ext/upb-generated/google/protobuf/wrappers.upbdefs.h \
   src/core/ext/upb-generated/google/protobuf/wrappers_upbdefs.h
mv src/core/ext/upb-generated/envoy/type/tracing/v2/custom_tag.upbdefs.h \
   src/core/ext/upb-generated/envoy/type/tracing/v2/custom_tag_upbdefs.h
mv src/core/ext/upb-generated/envoy/api/v2/auth/common.upbdefs.h \
   src/core/ext/upb-generated/envoy/api/v2/auth/common_upbdefs.h
mv src/core/ext/upb-generated/google/protobuf/duration.upbdefs.c \
   src/core/ext/upb-generated/google/protobuf/duration_upbdefs.c
mv src/core/ext/upb-generated/envoy/api/v2/listener/listener_components.upbdefs.c \
   src/core/ext/upb-generated/envoy/api/v2/listener/listener_components_upbdefs.c
mv src/core/ext/upb-generated/envoy/api/v2/core/event_service_config.upbdefs.h \
   src/core/ext/upb-generated/envoy/api/v2/core/event_service_config_upbdefs.h
mv src/core/ext/upb-generated/envoy/api/v2/core/event_service_config.upbdefs.c \
   src/core/ext/upb-generated/envoy/api/v2/core/event_service_config_upbdefs.c
mv src/core/ext/upb-generated/envoy/api/v2/listener/listener_components.upbdefs.h \
   src/core/ext/upb-generated/envoy/api/v2/listener/listener_components_upbdefs.h
mv src/core/ext/upb-generated/google/protobuf/duration.upbdefs.h \
   src/core/ext/upb-generated/google/protobuf/duration_upbdefs.h
mv src/core/ext/upb-generated/envoy/api/v2/core/grpc_service.upbdefs.c \
   src/core/ext/upb-generated/envoy/api/v2/core/grpc_service_upbdefs.c
mv src/core/ext/upb-generated/envoy/type/metadata/v2/metadata.upbdefs.c \
   src/core/ext/upb-generated/envoy/type/metadata/v2/metadata_upbdefs.c
mv src/core/ext/upb-generated/google/protobuf/descriptor.upbdefs.c \
   src/core/ext/upb-generated/google/protobuf/descriptor_upbdefs.c
mv src/core/ext/upb-generated/envoy/type/metadata/v2/metadata.upbdefs.h \
   src/core/ext/upb-generated/envoy/type/metadata/v2/metadata_upbdefs.h
mv src/core/ext/upb-generated/google/api/annotations.upbdefs.h \
   src/core/ext/upb-generated/google/api/annotations_upbdefs.h
mv src/core/ext/upb-generated/envoy/api/v2/listener/listener.upbdefs.c \
   src/core/ext/upb-generated/envoy/api/v2/listener/listener_upbdefs.c
mv src/core/ext/upb-generated/envoy/api/v2/listener/listener.upbdefs.h \
   src/core/ext/upb-generated/envoy/api/v2/listener/listener_upbdefs.h
mv src/core/ext/upb-generated/google/api/annotations.upbdefs.c \
   src/core/ext/upb-generated/google/api/annotations_upbdefs.c
mv src/core/ext/upb-generated/envoy/api/v2/cluster/outlier_detection.upbdefs.h \
   src/core/ext/upb-generated/envoy/api/v2/cluster/outlier_detection_upbdefs.h
mv src/core/ext/upb-generated/envoy/annotations/resource.upbdefs.c \
   src/core/ext/upb-generated/envoy/annotations/resource_upbdefs.c
mv src/core/ext/upb-generated/envoy/type/semantic_version.upbdefs.h \
   src/core/ext/upb-generated/envoy/type/semantic_version_upbdefs.h
mv src/core/ext/upb-generated/envoy/annotations/resource.upbdefs.h \
   src/core/ext/upb-generated/envoy/annotations/resource_upbdefs.h
mv src/core/ext/upb-generated/envoy/api/v2/cluster/outlier_detection.upbdefs.c \
   src/core/ext/upb-generated/envoy/api/v2/cluster/outlier_detection_upbdefs.c
mv src/core/ext/upb-generated/envoy/type/semantic_version.upbdefs.c \
   src/core/ext/upb-generated/envoy/type/semantic_version_upbdefs.c
mv src/core/ext/upb-generated/envoy/api/v2/lds.upbdefs.h \
   src/core/ext/upb-generated/envoy/api/v2/lds_upbdefs.h
mv src/core/ext/upb-generated/envoy/api/v2/auth/secret.upbdefs.c \
   src/core/ext/upb-generated/envoy/api/v2/auth/secret_upbdefs.c
mv src/core/ext/upb-generated/envoy/api/v2/auth/secret.upbdefs.h \
   src/core/ext/upb-generated/envoy/api/v2/auth/secret_upbdefs.h
mv src/core/ext/upb-generated/envoy/api/v2/lds.upbdefs.c \
   src/core/ext/upb-generated/envoy/api/v2/lds_upbdefs.c
mv src/core/ext/upb-generated/envoy/api/v2/cluster/filter.upbdefs.h \
   src/core/ext/upb-generated/envoy/api/v2/cluster/filter_upbdefs.h
mv src/core/ext/upb-generated/envoy/api/v2/endpoint.upbdefs.h \
   src/core/ext/upb-generated/envoy/api/v2/endpoint_upbdefs.h
mv src/core/ext/upb-generated/google/protobuf/struct.upbdefs.c \
   src/core/ext/upb-generated/google/protobuf/struct_upbdefs.c
mv src/core/ext/upb-generated/envoy/api/v2/endpoint.upbdefs.c \
   src/core/ext/upb-generated/envoy/api/v2/endpoint_upbdefs.c
mv src/core/ext/upb-generated/envoy/api/v2/cluster/filter.upbdefs.c \
   src/core/ext/upb-generated/envoy/api/v2/cluster/filter_upbdefs.c
mv src/core/ext/upb-generated/google/protobuf/struct.upbdefs.h \
   src/core/ext/upb-generated/google/protobuf/struct_upbdefs.h
mv src/core/ext/upb-generated/envoy/service/load_stats/v2/lrs.upbdefs.c \
   src/core/ext/upb-generated/envoy/service/load_stats/v2/lrs_upbdefs.c
mv src/core/ext/upb-generated/envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.upbdefs.c \
   src/core/ext/upb-generated/envoy/config/filter/network/http_connection_manager/v2/http_connection_manager_upbdefs.c
mv src/core/ext/upb-generated/envoy/service/load_stats/v2/lrs.upbdefs.h \
   src/core/ext/upb-generated/envoy/service/load_stats/v2/lrs_upbdefs.h
mv src/core/ext/upb-generated/envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.upbdefs.h \
   src/core/ext/upb-generated/envoy/config/filter/network/http_connection_manager/v2/http_connection_manager_upbdefs.h
mv src/core/ext/upb-generated/envoy/api/v2/core/backoff.upbdefs.h \
   src/core/ext/upb-generated/envoy/api/v2/core/backoff_upbdefs.h
mv src/core/ext/upb-generated/envoy/api/v2/core/backoff.upbdefs.c \
   src/core/ext/upb-generated/envoy/api/v2/core/backoff_upbdefs.c
mv src/core/ext/upb-generated/envoy/type/matcher/regex.upbdefs.h \
   src/core/ext/upb-generated/envoy/type/matcher/regex_upbdefs.h
mv src/core/ext/upb-generated/envoy/type/matcher/regex.upbdefs.c \
   src/core/ext/upb-generated/envoy/type/matcher/regex_upbdefs.c
mv src/core/ext/upb-generated/validate/validate.upbdefs.h \
   src/core/ext/upb-generated/validate/validate_upbdefs.h
mv src/core/ext/upb-generated/envoy/type/matcher/string.upbdefs.h \
   src/core/ext/upb-generated/envoy/type/matcher/string_upbdefs.h
mv src/core/ext/upb-generated/envoy/api/v2/cluster.upbdefs.h \
   src/core/ext/upb-generated/envoy/api/v2/cluster_upbdefs.h
mv src/core/ext/upb-generated/validate/validate.upbdefs.c \
   src/core/ext/upb-generated/validate/validate_upbdefs.c
mv src/core/ext/upb-generated/envoy/api/v2/cluster.upbdefs.c \
   src/core/ext/upb-generated/envoy/api/v2/cluster_upbdefs.c
mv src/core/ext/upb-generated/envoy/type/percent.upbdefs.c \
   src/core/ext/upb-generated/envoy/type/percent_upbdefs.c
mv src/core/ext/upb-generated/envoy/annotations/deprecation.upbdefs.h \
   src/core/ext/upb-generated/envoy/annotations/deprecation_upbdefs.h
mv src/core/ext/upb-generated/google/protobuf/any.upbdefs.c \
   src/core/ext/upb-generated/google/protobuf/any_upbdefs.c
mv src/core/ext/upb-generated/envoy/annotations/deprecation.upbdefs.c \
   src/core/ext/upb-generated/envoy/annotations/deprecation_upbdefs.c
mv src/core/ext/upb-generated/google/api/http.upbdefs.c \
   src/core/ext/upb-generated/google/api/http_upbdefs.c
mv src/core/ext/upb-generated/google/protobuf/any.upbdefs.h \
   src/core/ext/upb-generated/google/protobuf/any_upbdefs.h
mv src/core/ext/upb-generated/envoy/api/v2/listener.upbdefs.h \
   src/core/ext/upb-generated/envoy/api/v2/listener_upbdefs.h
mv src/core/ext/upb-generated/envoy/api/v2/listener.upbdefs.c \
   src/core/ext/upb-generated/envoy/api/v2/listener_upbdefs.c
mv src/core/ext/upb-generated/google/rpc/status.upbdefs.c \
   src/core/ext/upb-generated/google/rpc/status_upbdefs.c
mv src/core/ext/upb-generated/envoy/api/v2/core/protocol.upbdefs.c \
   src/core/ext/upb-generated/envoy/api/v2/core/protocol_upbdefs.c
mv src/core/ext/upb-generated/envoy/api/v2/core/socket_option.upbdefs.h \
   src/core/ext/upb-generated/envoy/api/v2/core/socket_option_upbdefs.h
mv src/core/ext/upb-generated/google/rpc/status.upbdefs.h \
   src/core/ext/upb-generated/google/rpc/status_upbdefs.h
mv src/core/ext/upb-generated/envoy/api/v2/core/socket_option.upbdefs.c \
   src/core/ext/upb-generated/envoy/api/v2/core/socket_option_upbdefs.c
mv src/core/ext/upb-generated/envoy/api/v2/core/protocol.upbdefs.h \
   src/core/ext/upb-generated/envoy/api/v2/core/protocol_upbdefs.h

output_file=$(mktemp)

# Rename the references to these _upbdefs.c|h files in the following
# PHP manifest files
sed 's/\.upbdefs/_upbdefs/g' config.m4 > $output_file
mv $output_file config.m4
sed 's/\.upbdefs/_upbdefs/g' package.xml > $output_file
mv $output_file package.xml

# Rename the #include references within all source files
for file in \
  src/core/ext/filters/census/grpc_context.cc \
  src/core/ext/filters/client_channel/backend_metric.cc \
  src/core/ext/filters/client_channel/backend_metric.h \
  src/core/ext/filters/client_channel/backup_poller.cc \
  src/core/ext/filters/client_channel/backup_poller.h \
  src/core/ext/filters/client_channel/channel_connectivity.cc \
  src/core/ext/filters/client_channel/client_channel.cc \
  src/core/ext/filters/client_channel/client_channel.h \
  src/core/ext/filters/client_channel/client_channel_channelz.cc \
  src/core/ext/filters/client_channel/client_channel_channelz.h \
  src/core/ext/filters/client_channel/client_channel_factory.cc \
  src/core/ext/filters/client_channel/client_channel_factory.h \
  src/core/ext/filters/client_channel/client_channel_plugin.cc \
  src/core/ext/filters/client_channel/config_selector.cc \
  src/core/ext/filters/client_channel/config_selector.h \
  src/core/ext/filters/client_channel/connector.h \
  src/core/ext/filters/client_channel/global_subchannel_pool.cc \
  src/core/ext/filters/client_channel/global_subchannel_pool.h \
  src/core/ext/filters/client_channel/health/health_check_client.cc \
  src/core/ext/filters/client_channel/health/health_check_client.h \
  src/core/ext/filters/client_channel/http_connect_handshaker.cc \
  src/core/ext/filters/client_channel/http_connect_handshaker.h \
  src/core/ext/filters/client_channel/http_proxy.cc \
  src/core/ext/filters/client_channel/http_proxy.h \
  src/core/ext/filters/client_channel/lb_policy.cc \
  src/core/ext/filters/client_channel/lb_policy.h \
  src/core/ext/filters/client_channel/lb_policy/address_filtering.cc \
  src/core/ext/filters/client_channel/lb_policy/address_filtering.h \
  src/core/ext/filters/client_channel/lb_policy/child_policy_handler.cc \
  src/core/ext/filters/client_channel/lb_policy/child_policy_handler.h \
  src/core/ext/filters/client_channel/lb_policy/grpclb/client_load_reporting_filter.cc \
  src/core/ext/filters/client_channel/lb_policy/grpclb/client_load_reporting_filter.h \
  src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb.cc \
  src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb.h \
  src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_balancer_addresses.cc \
  src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_balancer_addresses.h \
  src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_channel.h \
  src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_channel_secure.cc \
  src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_client_stats.cc \
  src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_client_stats.h \
  src/core/ext/filters/client_channel/lb_policy/grpclb/load_balancer_api.cc \
  src/core/ext/filters/client_channel/lb_policy/grpclb/load_balancer_api.h \
  src/core/ext/filters/client_channel/lb_policy/pick_first/pick_first.cc \
  src/core/ext/filters/client_channel/lb_policy/priority/priority.cc \
  src/core/ext/filters/client_channel/lb_policy/round_robin/round_robin.cc \
  src/core/ext/filters/client_channel/lb_policy/subchannel_list.h \
  src/core/ext/filters/client_channel/lb_policy/weighted_target/weighted_target.cc \
  src/core/ext/filters/client_channel/lb_policy/xds/cds.cc \
  src/core/ext/filters/client_channel/lb_policy/xds/eds.cc \
  src/core/ext/filters/client_channel/lb_policy/xds/lrs.cc \
  src/core/ext/filters/client_channel/lb_policy/xds/xds.h \
  src/core/ext/filters/client_channel/lb_policy/xds/xds_routing.cc \
  src/core/ext/filters/client_channel/lb_policy_factory.h \
  src/core/ext/filters/client_channel/lb_policy_registry.cc \
  src/core/ext/filters/client_channel/lb_policy_registry.h \
  src/core/ext/filters/client_channel/local_subchannel_pool.cc \
  src/core/ext/filters/client_channel/local_subchannel_pool.h \
  src/core/ext/filters/client_channel/parse_address.cc \
  src/core/ext/filters/client_channel/parse_address.h \
  src/core/ext/filters/client_channel/proxy_mapper.h \
  src/core/ext/filters/client_channel/proxy_mapper_registry.cc \
  src/core/ext/filters/client_channel/proxy_mapper_registry.h \
  src/core/ext/filters/client_channel/resolver.cc \
  src/core/ext/filters/client_channel/resolver.h \
  src/core/ext/filters/client_channel/resolver/dns/c_ares/dns_resolver_ares.cc \
  src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver.cc \
  src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver.h \
  src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver_libuv.cc \
  src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver_posix.cc \
  src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver_windows.cc \
  src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.cc \
  src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h \
  src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper_fallback.cc \
  src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper_libuv.cc \
  src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper_posix.cc \
  src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper_windows.cc \
  src/core/ext/filters/client_channel/resolver/dns/dns_resolver_selection.cc \
  src/core/ext/filters/client_channel/resolver/dns/dns_resolver_selection.h \
  src/core/ext/filters/client_channel/resolver/dns/native/dns_resolver.cc \
  src/core/ext/filters/client_channel/resolver/fake/fake_resolver.cc \
  src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h \
  src/core/ext/filters/client_channel/resolver/sockaddr/sockaddr_resolver.cc \
  src/core/ext/filters/client_channel/resolver/xds/xds_resolver.cc \
  src/core/ext/filters/client_channel/resolver_factory.h \
  src/core/ext/filters/client_channel/resolver_registry.cc \
  src/core/ext/filters/client_channel/resolver_registry.h \
  src/core/ext/filters/client_channel/resolver_result_parsing.cc \
  src/core/ext/filters/client_channel/resolver_result_parsing.h \
  src/core/ext/filters/client_channel/resolving_lb_policy.cc \
  src/core/ext/filters/client_channel/resolving_lb_policy.h \
  src/core/ext/filters/client_channel/retry_throttle.cc \
  src/core/ext/filters/client_channel/retry_throttle.h \
  src/core/ext/filters/client_channel/server_address.cc \
  src/core/ext/filters/client_channel/server_address.h \
  src/core/ext/filters/client_channel/service_config.cc \
  src/core/ext/filters/client_channel/service_config.h \
  src/core/ext/filters/client_channel/service_config_call_data.h \
  src/core/ext/filters/client_channel/service_config_channel_arg_filter.cc \
  src/core/ext/filters/client_channel/service_config_parser.cc \
  src/core/ext/filters/client_channel/service_config_parser.h \
  src/core/ext/filters/client_channel/subchannel.cc \
  src/core/ext/filters/client_channel/subchannel.h \
  src/core/ext/filters/client_channel/subchannel_interface.h \
  src/core/ext/filters/client_channel/subchannel_pool_interface.cc \
  src/core/ext/filters/client_channel/subchannel_pool_interface.h \
  src/core/ext/filters/client_channel/xds/xds_api.cc \
  src/core/ext/filters/client_channel/xds/xds_api.h \
  src/core/ext/filters/client_channel/xds/xds_bootstrap.cc \
  src/core/ext/filters/client_channel/xds/xds_bootstrap.h \
  src/core/ext/filters/client_channel/xds/xds_channel.h \
  src/core/ext/filters/client_channel/xds/xds_channel_args.h \
  src/core/ext/filters/client_channel/xds/xds_channel_secure.cc \
  src/core/ext/filters/client_channel/xds/xds_client.cc \
  src/core/ext/filters/client_channel/xds/xds_client.h \
  src/core/ext/filters/client_channel/xds/xds_client_stats.cc \
  src/core/ext/filters/client_channel/xds/xds_client_stats.h \
  src/core/ext/filters/client_idle/client_idle_filter.cc \
  src/core/ext/filters/deadline/deadline_filter.cc \
  src/core/ext/filters/deadline/deadline_filter.h \
  src/core/ext/filters/http/client/http_client_filter.cc \
  src/core/ext/filters/http/client/http_client_filter.h \
  src/core/ext/filters/http/client_authority_filter.cc \
  src/core/ext/filters/http/client_authority_filter.h \
  src/core/ext/filters/http/http_filters_plugin.cc \
  src/core/ext/filters/http/message_compress/message_compress_filter.cc \
  src/core/ext/filters/http/message_compress/message_compress_filter.h \
  src/core/ext/filters/http/message_compress/message_decompress_filter.cc \
  src/core/ext/filters/http/message_compress/message_decompress_filter.h \
  src/core/ext/filters/http/server/http_server_filter.cc \
  src/core/ext/filters/http/server/http_server_filter.h \
  src/core/ext/filters/max_age/max_age_filter.cc \
  src/core/ext/filters/max_age/max_age_filter.h \
  src/core/ext/filters/message_size/message_size_filter.cc \
  src/core/ext/filters/message_size/message_size_filter.h \
  src/core/ext/filters/workarounds/workaround_cronet_compression_filter.cc \
  src/core/ext/filters/workarounds/workaround_cronet_compression_filter.h \
  src/core/ext/filters/workarounds/workaround_utils.cc \
  src/core/ext/filters/workarounds/workaround_utils.h \
  src/core/ext/transport/chttp2/alpn/alpn.cc \
  src/core/ext/transport/chttp2/alpn/alpn.h \
  src/core/ext/transport/chttp2/client/authority.cc \
  src/core/ext/transport/chttp2/client/authority.h \
  src/core/ext/transport/chttp2/client/chttp2_connector.cc \
  src/core/ext/transport/chttp2/client/chttp2_connector.h \
  src/core/ext/transport/chttp2/client/insecure/channel_create.cc \
  src/core/ext/transport/chttp2/client/insecure/channel_create_posix.cc \
  src/core/ext/transport/chttp2/client/secure/secure_channel_create.cc \
  src/core/ext/transport/chttp2/server/chttp2_server.cc \
  src/core/ext/transport/chttp2/server/chttp2_server.h \
  src/core/ext/transport/chttp2/server/insecure/server_chttp2.cc \
  src/core/ext/transport/chttp2/server/insecure/server_chttp2_posix.cc \
  src/core/ext/transport/chttp2/server/secure/server_secure_chttp2.cc \
  src/core/ext/transport/chttp2/transport/bin_decoder.cc \
  src/core/ext/transport/chttp2/transport/bin_decoder.h \
  src/core/ext/transport/chttp2/transport/bin_encoder.cc \
  src/core/ext/transport/chttp2/transport/bin_encoder.h \
  src/core/ext/transport/chttp2/transport/chttp2_plugin.cc \
  src/core/ext/transport/chttp2/transport/chttp2_transport.cc \
  src/core/ext/transport/chttp2/transport/chttp2_transport.h \
  src/core/ext/transport/chttp2/transport/context_list.cc \
  src/core/ext/transport/chttp2/transport/context_list.h \
  src/core/ext/transport/chttp2/transport/flow_control.cc \
  src/core/ext/transport/chttp2/transport/flow_control.h \
  src/core/ext/transport/chttp2/transport/frame.h \
  src/core/ext/transport/chttp2/transport/frame_data.cc \
  src/core/ext/transport/chttp2/transport/frame_data.h \
  src/core/ext/transport/chttp2/transport/frame_goaway.cc \
  src/core/ext/transport/chttp2/transport/frame_goaway.h \
  src/core/ext/transport/chttp2/transport/frame_ping.cc \
  src/core/ext/transport/chttp2/transport/frame_ping.h \
  src/core/ext/transport/chttp2/transport/frame_rst_stream.cc \
  src/core/ext/transport/chttp2/transport/frame_rst_stream.h \
  src/core/ext/transport/chttp2/transport/frame_settings.cc \
  src/core/ext/transport/chttp2/transport/frame_settings.h \
  src/core/ext/transport/chttp2/transport/frame_window_update.cc \
  src/core/ext/transport/chttp2/transport/frame_window_update.h \
  src/core/ext/transport/chttp2/transport/hpack_encoder.cc \
  src/core/ext/transport/chttp2/transport/hpack_encoder.h \
  src/core/ext/transport/chttp2/transport/hpack_parser.cc \
  src/core/ext/transport/chttp2/transport/hpack_parser.h \
  src/core/ext/transport/chttp2/transport/hpack_table.cc \
  src/core/ext/transport/chttp2/transport/hpack_table.h \
  src/core/ext/transport/chttp2/transport/http2_settings.cc \
  src/core/ext/transport/chttp2/transport/http2_settings.h \
  src/core/ext/transport/chttp2/transport/huffsyms.cc \
  src/core/ext/transport/chttp2/transport/huffsyms.h \
  src/core/ext/transport/chttp2/transport/incoming_metadata.cc \
  src/core/ext/transport/chttp2/transport/incoming_metadata.h \
  src/core/ext/transport/chttp2/transport/internal.h \
  src/core/ext/transport/chttp2/transport/parsing.cc \
  src/core/ext/transport/chttp2/transport/stream_lists.cc \
  src/core/ext/transport/chttp2/transport/stream_map.cc \
  src/core/ext/transport/chttp2/transport/stream_map.h \
  src/core/ext/transport/chttp2/transport/varint.cc \
  src/core/ext/transport/chttp2/transport/varint.h \
  src/core/ext/transport/chttp2/transport/writing.cc \
  src/core/ext/transport/inproc/inproc_plugin.cc \
  src/core/ext/transport/inproc/inproc_transport.cc \
  src/core/ext/transport/inproc/inproc_transport.h \
  src/core/ext/upb-generated/envoy/annotations/deprecation.upb.c \
  src/core/ext/upb-generated/envoy/annotations/deprecation.upb.h \
  src/core/ext/upb-generated/envoy/annotations/deprecation_upbdefs.c \
  src/core/ext/upb-generated/envoy/annotations/deprecation_upbdefs.h \
  src/core/ext/upb-generated/envoy/annotations/resource.upb.c \
  src/core/ext/upb-generated/envoy/annotations/resource.upb.h \
  src/core/ext/upb-generated/envoy/annotations/resource_upbdefs.c \
  src/core/ext/upb-generated/envoy/annotations/resource_upbdefs.h \
  src/core/ext/upb-generated/envoy/api/v2/auth/cert.upb.c \
  src/core/ext/upb-generated/envoy/api/v2/auth/cert.upb.h \
  src/core/ext/upb-generated/envoy/api/v2/auth/cert_upbdefs.c \
  src/core/ext/upb-generated/envoy/api/v2/auth/cert_upbdefs.h \
  src/core/ext/upb-generated/envoy/api/v2/auth/common.upb.c \
  src/core/ext/upb-generated/envoy/api/v2/auth/common.upb.h \
  src/core/ext/upb-generated/envoy/api/v2/auth/common_upbdefs.c \
  src/core/ext/upb-generated/envoy/api/v2/auth/common_upbdefs.h \
  src/core/ext/upb-generated/envoy/api/v2/auth/secret.upb.c \
  src/core/ext/upb-generated/envoy/api/v2/auth/secret.upb.h \
  src/core/ext/upb-generated/envoy/api/v2/auth/secret_upbdefs.c \
  src/core/ext/upb-generated/envoy/api/v2/auth/secret_upbdefs.h \
  src/core/ext/upb-generated/envoy/api/v2/auth/tls.upb.c \
  src/core/ext/upb-generated/envoy/api/v2/auth/tls.upb.h \
  src/core/ext/upb-generated/envoy/api/v2/auth/tls_upbdefs.c \
  src/core/ext/upb-generated/envoy/api/v2/auth/tls_upbdefs.h \
  src/core/ext/upb-generated/envoy/api/v2/cds.upb.c \
  src/core/ext/upb-generated/envoy/api/v2/cds.upb.h \
  src/core/ext/upb-generated/envoy/api/v2/cds_upbdefs.c \
  src/core/ext/upb-generated/envoy/api/v2/cds_upbdefs.h \
  src/core/ext/upb-generated/envoy/api/v2/cluster.upb.c \
  src/core/ext/upb-generated/envoy/api/v2/cluster.upb.h \
  src/core/ext/upb-generated/envoy/api/v2/cluster_upbdefs.c \
  src/core/ext/upb-generated/envoy/api/v2/cluster_upbdefs.h \
  src/core/ext/upb-generated/envoy/api/v2/cluster/circuit_breaker.upb.c \
  src/core/ext/upb-generated/envoy/api/v2/cluster/circuit_breaker.upb.h \
  src/core/ext/upb-generated/envoy/api/v2/cluster/circuit_breaker_upbdefs.c \
  src/core/ext/upb-generated/envoy/api/v2/cluster/circuit_breaker_upbdefs.h \
  src/core/ext/upb-generated/envoy/api/v2/cluster/filter.upb.c \
  src/core/ext/upb-generated/envoy/api/v2/cluster/filter.upb.h \
  src/core/ext/upb-generated/envoy/api/v2/cluster/filter_upbdefs.c \
  src/core/ext/upb-generated/envoy/api/v2/cluster/filter_upbdefs.h \
  src/core/ext/upb-generated/envoy/api/v2/cluster/outlier_detection.upb.c \
  src/core/ext/upb-generated/envoy/api/v2/cluster/outlier_detection.upb.h \
  src/core/ext/upb-generated/envoy/api/v2/cluster/outlier_detection_upbdefs.c \
  src/core/ext/upb-generated/envoy/api/v2/cluster/outlier_detection_upbdefs.h \
  src/core/ext/upb-generated/envoy/api/v2/core/address.upb.c \
  src/core/ext/upb-generated/envoy/api/v2/core/address.upb.h \
  src/core/ext/upb-generated/envoy/api/v2/core/address_upbdefs.c \
  src/core/ext/upb-generated/envoy/api/v2/core/address_upbdefs.h \
  src/core/ext/upb-generated/envoy/api/v2/core/backoff.upb.c \
  src/core/ext/upb-generated/envoy/api/v2/core/backoff.upb.h \
  src/core/ext/upb-generated/envoy/api/v2/core/backoff_upbdefs.c \
  src/core/ext/upb-generated/envoy/api/v2/core/backoff_upbdefs.h \
  src/core/ext/upb-generated/envoy/api/v2/core/base.upb.c \
  src/core/ext/upb-generated/envoy/api/v2/core/base.upb.h \
  src/core/ext/upb-generated/envoy/api/v2/core/base_upbdefs.c \
  src/core/ext/upb-generated/envoy/api/v2/core/base_upbdefs.h \
  src/core/ext/upb-generated/envoy/api/v2/core/config_source.upb.c \
  src/core/ext/upb-generated/envoy/api/v2/core/config_source.upb.h \
  src/core/ext/upb-generated/envoy/api/v2/core/config_source_upbdefs.c \
  src/core/ext/upb-generated/envoy/api/v2/core/config_source_upbdefs.h \
  src/core/ext/upb-generated/envoy/api/v2/core/event_service_config.upb.c \
  src/core/ext/upb-generated/envoy/api/v2/core/event_service_config.upb.h \
  src/core/ext/upb-generated/envoy/api/v2/core/event_service_config_upbdefs.c \
  src/core/ext/upb-generated/envoy/api/v2/core/event_service_config_upbdefs.h \
  src/core/ext/upb-generated/envoy/api/v2/core/grpc_service.upb.c \
  src/core/ext/upb-generated/envoy/api/v2/core/grpc_service.upb.h \
  src/core/ext/upb-generated/envoy/api/v2/core/grpc_service_upbdefs.c \
  src/core/ext/upb-generated/envoy/api/v2/core/grpc_service_upbdefs.h \
  src/core/ext/upb-generated/envoy/api/v2/core/health_check.upb.c \
  src/core/ext/upb-generated/envoy/api/v2/core/health_check.upb.h \
  src/core/ext/upb-generated/envoy/api/v2/core/health_check_upbdefs.c \
  src/core/ext/upb-generated/envoy/api/v2/core/health_check_upbdefs.h \
  src/core/ext/upb-generated/envoy/api/v2/core/http_uri.upb.c \
  src/core/ext/upb-generated/envoy/api/v2/core/http_uri.upb.h \
  src/core/ext/upb-generated/envoy/api/v2/core/http_uri_upbdefs.c \
  src/core/ext/upb-generated/envoy/api/v2/core/http_uri_upbdefs.h \
  src/core/ext/upb-generated/envoy/api/v2/core/protocol.upb.c \
  src/core/ext/upb-generated/envoy/api/v2/core/protocol.upb.h \
  src/core/ext/upb-generated/envoy/api/v2/core/protocol_upbdefs.c \
  src/core/ext/upb-generated/envoy/api/v2/core/protocol_upbdefs.h \
  src/core/ext/upb-generated/envoy/api/v2/core/socket_option.upb.c \
  src/core/ext/upb-generated/envoy/api/v2/core/socket_option.upb.h \
  src/core/ext/upb-generated/envoy/api/v2/core/socket_option_upbdefs.c \
  src/core/ext/upb-generated/envoy/api/v2/core/socket_option_upbdefs.h \
  src/core/ext/upb-generated/envoy/api/v2/discovery.upb.c \
  src/core/ext/upb-generated/envoy/api/v2/discovery.upb.h \
  src/core/ext/upb-generated/envoy/api/v2/discovery_upbdefs.c \
  src/core/ext/upb-generated/envoy/api/v2/discovery_upbdefs.h \
  src/core/ext/upb-generated/envoy/api/v2/eds.upb.c \
  src/core/ext/upb-generated/envoy/api/v2/eds.upb.h \
  src/core/ext/upb-generated/envoy/api/v2/eds_upbdefs.c \
  src/core/ext/upb-generated/envoy/api/v2/eds_upbdefs.h \
  src/core/ext/upb-generated/envoy/api/v2/endpoint.upb.c \
  src/core/ext/upb-generated/envoy/api/v2/endpoint.upb.h \
  src/core/ext/upb-generated/envoy/api/v2/endpoint_upbdefs.c \
  src/core/ext/upb-generated/envoy/api/v2/endpoint_upbdefs.h \
  src/core/ext/upb-generated/envoy/api/v2/endpoint/endpoint.upb.c \
  src/core/ext/upb-generated/envoy/api/v2/endpoint/endpoint.upb.h \
  src/core/ext/upb-generated/envoy/api/v2/endpoint/endpoint_upbdefs.c \
  src/core/ext/upb-generated/envoy/api/v2/endpoint/endpoint_upbdefs.h \
  src/core/ext/upb-generated/envoy/api/v2/endpoint/endpoint_components.upb.c \
  src/core/ext/upb-generated/envoy/api/v2/endpoint/endpoint_components.upb.h \
  src/core/ext/upb-generated/envoy/api/v2/endpoint/endpoint_components_upbdefs.c \
  src/core/ext/upb-generated/envoy/api/v2/endpoint/endpoint_components_upbdefs.h \
  src/core/ext/upb-generated/envoy/api/v2/endpoint/load_report.upb.c \
  src/core/ext/upb-generated/envoy/api/v2/endpoint/load_report.upb.h \
  src/core/ext/upb-generated/envoy/api/v2/endpoint/load_report_upbdefs.c \
  src/core/ext/upb-generated/envoy/api/v2/endpoint/load_report_upbdefs.h \
  src/core/ext/upb-generated/envoy/api/v2/lds.upb.c \
  src/core/ext/upb-generated/envoy/api/v2/lds.upb.h \
  src/core/ext/upb-generated/envoy/api/v2/lds_upbdefs.c \
  src/core/ext/upb-generated/envoy/api/v2/lds_upbdefs.h \
  src/core/ext/upb-generated/envoy/api/v2/listener.upb.c \
  src/core/ext/upb-generated/envoy/api/v2/listener.upb.h \
  src/core/ext/upb-generated/envoy/api/v2/listener_upbdefs.c \
  src/core/ext/upb-generated/envoy/api/v2/listener_upbdefs.h \
  src/core/ext/upb-generated/envoy/api/v2/listener/listener.upb.c \
  src/core/ext/upb-generated/envoy/api/v2/listener/listener.upb.h \
  src/core/ext/upb-generated/envoy/api/v2/listener/listener_upbdefs.c \
  src/core/ext/upb-generated/envoy/api/v2/listener/listener_upbdefs.h \
  src/core/ext/upb-generated/envoy/api/v2/listener/listener_components.upb.c \
  src/core/ext/upb-generated/envoy/api/v2/listener/listener_components.upb.h \
  src/core/ext/upb-generated/envoy/api/v2/listener/listener_components_upbdefs.c \
  src/core/ext/upb-generated/envoy/api/v2/listener/listener_components_upbdefs.h \
  src/core/ext/upb-generated/envoy/api/v2/listener/udp_listener_config.upb.c \
  src/core/ext/upb-generated/envoy/api/v2/listener/udp_listener_config.upb.h \
  src/core/ext/upb-generated/envoy/api/v2/listener/udp_listener_config_upbdefs.c \
  src/core/ext/upb-generated/envoy/api/v2/listener/udp_listener_config_upbdefs.h \
  src/core/ext/upb-generated/envoy/api/v2/rds.upb.c \
  src/core/ext/upb-generated/envoy/api/v2/rds.upb.h \
  src/core/ext/upb-generated/envoy/api/v2/rds_upbdefs.c \
  src/core/ext/upb-generated/envoy/api/v2/rds_upbdefs.h \
  src/core/ext/upb-generated/envoy/api/v2/route.upb.c \
  src/core/ext/upb-generated/envoy/api/v2/route.upb.h \
  src/core/ext/upb-generated/envoy/api/v2/route_upbdefs.c \
  src/core/ext/upb-generated/envoy/api/v2/route_upbdefs.h \
  src/core/ext/upb-generated/envoy/api/v2/route/route.upb.c \
  src/core/ext/upb-generated/envoy/api/v2/route/route.upb.h \
  src/core/ext/upb-generated/envoy/api/v2/route/route_upbdefs.c \
  src/core/ext/upb-generated/envoy/api/v2/route/route_upbdefs.h \
  src/core/ext/upb-generated/envoy/api/v2/route/route_components.upb.c \
  src/core/ext/upb-generated/envoy/api/v2/route/route_components.upb.h \
  src/core/ext/upb-generated/envoy/api/v2/route/route_components_upbdefs.c \
  src/core/ext/upb-generated/envoy/api/v2/route/route_components_upbdefs.h \
  src/core/ext/upb-generated/envoy/api/v2/scoped_route.upb.c \
  src/core/ext/upb-generated/envoy/api/v2/scoped_route.upb.h \
  src/core/ext/upb-generated/envoy/api/v2/scoped_route_upbdefs.c \
  src/core/ext/upb-generated/envoy/api/v2/scoped_route_upbdefs.h \
  src/core/ext/upb-generated/envoy/api/v2/srds.upb.c \
  src/core/ext/upb-generated/envoy/api/v2/srds.upb.h \
  src/core/ext/upb-generated/envoy/api/v2/srds_upbdefs.c \
  src/core/ext/upb-generated/envoy/api/v2/srds_upbdefs.h \
  src/core/ext/upb-generated/envoy/config/filter/accesslog/v2/accesslog.upb.c \
  src/core/ext/upb-generated/envoy/config/filter/accesslog/v2/accesslog.upb.h \
  src/core/ext/upb-generated/envoy/config/filter/accesslog/v2/accesslog_upbdefs.c \
  src/core/ext/upb-generated/envoy/config/filter/accesslog/v2/accesslog_upbdefs.h \
  src/core/ext/upb-generated/envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.upb.c \
  src/core/ext/upb-generated/envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.upb.h \
  src/core/ext/upb-generated/envoy/config/filter/network/http_connection_manager/v2/http_connection_manager_upbdefs.c \
  src/core/ext/upb-generated/envoy/config/filter/network/http_connection_manager/v2/http_connection_manager_upbdefs.h \
  src/core/ext/upb-generated/envoy/config/listener/v2/api_listener.upb.c \
  src/core/ext/upb-generated/envoy/config/listener/v2/api_listener.upb.h \
  src/core/ext/upb-generated/envoy/config/listener/v2/api_listener_upbdefs.c \
  src/core/ext/upb-generated/envoy/config/listener/v2/api_listener_upbdefs.h \
  src/core/ext/upb-generated/envoy/config/trace/v2/http_tracer.upb.c \
  src/core/ext/upb-generated/envoy/config/trace/v2/http_tracer.upb.h \
  src/core/ext/upb-generated/envoy/config/trace/v2/http_tracer_upbdefs.c \
  src/core/ext/upb-generated/envoy/config/trace/v2/http_tracer_upbdefs.h \
  src/core/ext/upb-generated/envoy/service/discovery/v2/ads.upb.c \
  src/core/ext/upb-generated/envoy/service/discovery/v2/ads.upb.h \
  src/core/ext/upb-generated/envoy/service/discovery/v2/ads_upbdefs.c \
  src/core/ext/upb-generated/envoy/service/discovery/v2/ads_upbdefs.h \
  src/core/ext/upb-generated/envoy/service/load_stats/v2/lrs.upb.c \
  src/core/ext/upb-generated/envoy/service/load_stats/v2/lrs.upb.h \
  src/core/ext/upb-generated/envoy/service/load_stats/v2/lrs_upbdefs.c \
  src/core/ext/upb-generated/envoy/service/load_stats/v2/lrs_upbdefs.h \
  src/core/ext/upb-generated/envoy/type/http.upb.c \
  src/core/ext/upb-generated/envoy/type/http.upb.h \
  src/core/ext/upb-generated/envoy/type/http_upbdefs.c \
  src/core/ext/upb-generated/envoy/type/http_upbdefs.h \
  src/core/ext/upb-generated/envoy/type/matcher/regex.upb.c \
  src/core/ext/upb-generated/envoy/type/matcher/regex.upb.h \
  src/core/ext/upb-generated/envoy/type/matcher/regex_upbdefs.c \
  src/core/ext/upb-generated/envoy/type/matcher/regex_upbdefs.h \
  src/core/ext/upb-generated/envoy/type/matcher/string.upb.c \
  src/core/ext/upb-generated/envoy/type/matcher/string.upb.h \
  src/core/ext/upb-generated/envoy/type/matcher/string_upbdefs.c \
  src/core/ext/upb-generated/envoy/type/matcher/string_upbdefs.h \
  src/core/ext/upb-generated/envoy/type/metadata/v2/metadata.upb.c \
  src/core/ext/upb-generated/envoy/type/metadata/v2/metadata.upb.h \
  src/core/ext/upb-generated/envoy/type/metadata/v2/metadata_upbdefs.c \
  src/core/ext/upb-generated/envoy/type/metadata/v2/metadata_upbdefs.h \
  src/core/ext/upb-generated/envoy/type/percent.upb.c \
  src/core/ext/upb-generated/envoy/type/percent.upb.h \
  src/core/ext/upb-generated/envoy/type/percent_upbdefs.c \
  src/core/ext/upb-generated/envoy/type/percent_upbdefs.h \
  src/core/ext/upb-generated/envoy/type/range.upb.c \
  src/core/ext/upb-generated/envoy/type/range.upb.h \
  src/core/ext/upb-generated/envoy/type/range_upbdefs.c \
  src/core/ext/upb-generated/envoy/type/range_upbdefs.h \
  src/core/ext/upb-generated/envoy/type/semantic_version.upb.c \
  src/core/ext/upb-generated/envoy/type/semantic_version.upb.h \
  src/core/ext/upb-generated/envoy/type/semantic_version_upbdefs.c \
  src/core/ext/upb-generated/envoy/type/semantic_version_upbdefs.h \
  src/core/ext/upb-generated/envoy/type/tracing/v2/custom_tag.upb.c \
  src/core/ext/upb-generated/envoy/type/tracing/v2/custom_tag.upb.h \
  src/core/ext/upb-generated/envoy/type/tracing/v2/custom_tag_upbdefs.c \
  src/core/ext/upb-generated/envoy/type/tracing/v2/custom_tag_upbdefs.h \
  src/core/ext/upb-generated/gogoproto/gogo.upb.c \
  src/core/ext/upb-generated/gogoproto/gogo.upb.h \
  src/core/ext/upb-generated/gogoproto/gogo_upbdefs.c \
  src/core/ext/upb-generated/gogoproto/gogo_upbdefs.h \
  src/core/ext/upb-generated/google/api/annotations.upb.c \
  src/core/ext/upb-generated/google/api/annotations.upb.h \
  src/core/ext/upb-generated/google/api/annotations_upbdefs.c \
  src/core/ext/upb-generated/google/api/annotations_upbdefs.h \
  src/core/ext/upb-generated/google/api/http.upb.c \
  src/core/ext/upb-generated/google/api/http.upb.h \
  src/core/ext/upb-generated/google/api/http_upbdefs.c \
  src/core/ext/upb-generated/google/api/http_upbdefs.h \
  src/core/ext/upb-generated/google/protobuf/any.upb.c \
  src/core/ext/upb-generated/google/protobuf/any.upb.h \
  src/core/ext/upb-generated/google/protobuf/any_upbdefs.c \
  src/core/ext/upb-generated/google/protobuf/any_upbdefs.h \
  src/core/ext/upb-generated/google/protobuf/descriptor_upbdefs.c \
  src/core/ext/upb-generated/google/protobuf/descriptor_upbdefs.h \
  src/core/ext/upb-generated/google/protobuf/duration.upb.c \
  src/core/ext/upb-generated/google/protobuf/duration.upb.h \
  src/core/ext/upb-generated/google/protobuf/duration_upbdefs.c \
  src/core/ext/upb-generated/google/protobuf/duration_upbdefs.h \
  src/core/ext/upb-generated/google/protobuf/empty.upb.c \
  src/core/ext/upb-generated/google/protobuf/empty.upb.h \
  src/core/ext/upb-generated/google/protobuf/empty_upbdefs.c \
  src/core/ext/upb-generated/google/protobuf/empty_upbdefs.h \
  src/core/ext/upb-generated/google/protobuf/struct.upb.c \
  src/core/ext/upb-generated/google/protobuf/struct.upb.h \
  src/core/ext/upb-generated/google/protobuf/struct_upbdefs.c \
  src/core/ext/upb-generated/google/protobuf/struct_upbdefs.h \
  src/core/ext/upb-generated/google/protobuf/timestamp.upb.c \
  src/core/ext/upb-generated/google/protobuf/timestamp.upb.h \
  src/core/ext/upb-generated/google/protobuf/timestamp_upbdefs.c \
  src/core/ext/upb-generated/google/protobuf/timestamp_upbdefs.h \
  src/core/ext/upb-generated/google/protobuf/wrappers.upb.c \
  src/core/ext/upb-generated/google/protobuf/wrappers.upb.h \
  src/core/ext/upb-generated/google/protobuf/wrappers_upbdefs.c \
  src/core/ext/upb-generated/google/protobuf/wrappers_upbdefs.h \
  src/core/ext/upb-generated/google/rpc/status.upb.c \
  src/core/ext/upb-generated/google/rpc/status.upb.h \
  src/core/ext/upb-generated/google/rpc/status_upbdefs.c \
  src/core/ext/upb-generated/google/rpc/status_upbdefs.h \
  src/core/ext/upb-generated/src/proto/grpc/gcp/altscontext.upb.c \
  src/core/ext/upb-generated/src/proto/grpc/gcp/altscontext.upb.h \
  src/core/ext/upb-generated/src/proto/grpc/gcp/handshaker.upb.c \
  src/core/ext/upb-generated/src/proto/grpc/gcp/handshaker.upb.h \
  src/core/ext/upb-generated/src/proto/grpc/gcp/transport_security_common.upb.c \
  src/core/ext/upb-generated/src/proto/grpc/gcp/transport_security_common.upb.h \
  src/core/ext/upb-generated/src/proto/grpc/health/v1/health.upb.c \
  src/core/ext/upb-generated/src/proto/grpc/health/v1/health.upb.h \
  src/core/ext/upb-generated/src/proto/grpc/lb/v1/load_balancer.upb.c \
  src/core/ext/upb-generated/src/proto/grpc/lb/v1/load_balancer.upb.h \
  src/core/ext/upb-generated/udpa/annotations/migrate.upb.c \
  src/core/ext/upb-generated/udpa/annotations/migrate.upb.h \
  src/core/ext/upb-generated/udpa/annotations/migrate_upbdefs.c \
  src/core/ext/upb-generated/udpa/annotations/migrate_upbdefs.h \
  src/core/ext/upb-generated/udpa/annotations/sensitive.upb.c \
  src/core/ext/upb-generated/udpa/annotations/sensitive.upb.h \
  src/core/ext/upb-generated/udpa/annotations/sensitive_upbdefs.c \
  src/core/ext/upb-generated/udpa/annotations/sensitive_upbdefs.h \
  src/core/ext/upb-generated/udpa/annotations/status.upb.c \
  src/core/ext/upb-generated/udpa/annotations/status.upb.h \
  src/core/ext/upb-generated/udpa/annotations/status_upbdefs.c \
  src/core/ext/upb-generated/udpa/annotations/status_upbdefs.h \
  src/core/ext/upb-generated/udpa/data/orca/v1/orca_load_report.upb.c \
  src/core/ext/upb-generated/udpa/data/orca/v1/orca_load_report.upb.h \
  src/core/ext/upb-generated/validate/validate.upb.c \
  src/core/ext/upb-generated/validate/validate.upb.h \
  src/core/ext/upb-generated/validate/validate_upbdefs.c \
  src/core/ext/upb-generated/validate/validate_upbdefs.h \
  src/core/lib/avl/avl.cc \
  src/core/lib/avl/avl.h \
  src/core/lib/backoff/backoff.cc \
  src/core/lib/backoff/backoff.h \
  src/core/lib/channel/channel_args.cc \
  src/core/lib/channel/channel_args.h \
  src/core/lib/channel/channel_stack.cc \
  src/core/lib/channel/channel_stack.h \
  src/core/lib/channel/channel_stack_builder.cc \
  src/core/lib/channel/channel_stack_builder.h \
  src/core/lib/channel/channel_trace.cc \
  src/core/lib/channel/channel_trace.h \
  src/core/lib/channel/channelz.cc \
  src/core/lib/channel/channelz.h \
  src/core/lib/channel/channelz_registry.cc \
  src/core/lib/channel/channelz_registry.h \
  src/core/lib/channel/connected_channel.cc \
  src/core/lib/channel/connected_channel.h \
  src/core/lib/channel/context.h \
  src/core/lib/channel/handshaker.cc \
  src/core/lib/channel/handshaker.h \
  src/core/lib/channel/handshaker_factory.h \
  src/core/lib/channel/handshaker_registry.cc \
  src/core/lib/channel/handshaker_registry.h \
  src/core/lib/channel/status_util.cc \
  src/core/lib/channel/status_util.h \
  src/core/lib/compression/algorithm_metadata.h \
  src/core/lib/compression/compression.cc \
  src/core/lib/compression/compression_args.cc \
  src/core/lib/compression/compression_args.h \
  src/core/lib/compression/compression_internal.cc \
  src/core/lib/compression/compression_internal.h \
  src/core/lib/compression/message_compress.cc \
  src/core/lib/compression/message_compress.h \
  src/core/lib/compression/stream_compression.cc \
  src/core/lib/compression/stream_compression.h \
  src/core/lib/compression/stream_compression_gzip.cc \
  src/core/lib/compression/stream_compression_gzip.h \
  src/core/lib/compression/stream_compression_identity.cc \
  src/core/lib/compression/stream_compression_identity.h \
  src/core/lib/debug/stats.cc \
  src/core/lib/debug/stats.h \
  src/core/lib/debug/stats_data.cc \
  src/core/lib/debug/stats_data.h \
  src/core/lib/debug/trace.cc \
  src/core/lib/debug/trace.h \
  src/core/lib/gprpp/atomic.h \
  src/core/lib/gprpp/debug_location.h \
  src/core/lib/gprpp/orphanable.h \
  src/core/lib/gprpp/ref_counted.h \
  src/core/lib/gprpp/ref_counted_ptr.h \
  src/core/lib/http/format_request.cc \
  src/core/lib/http/format_request.h \
  src/core/lib/http/httpcli.cc \
  src/core/lib/http/httpcli.h \
  src/core/lib/http/httpcli_security_connector.cc \
  src/core/lib/http/parser.cc \
  src/core/lib/http/parser.h \
  src/core/lib/iomgr/block_annotate.h \
  src/core/lib/iomgr/buffer_list.cc \
  src/core/lib/iomgr/buffer_list.h \
  src/core/lib/iomgr/call_combiner.cc \
  src/core/lib/iomgr/call_combiner.h \
  src/core/lib/iomgr/cfstream_handle.cc \
  src/core/lib/iomgr/cfstream_handle.h \
  src/core/lib/iomgr/closure.h \
  src/core/lib/iomgr/combiner.cc \
  src/core/lib/iomgr/combiner.h \
  src/core/lib/iomgr/dualstack_socket_posix.cc \
  src/core/lib/iomgr/dynamic_annotations.h \
  src/core/lib/iomgr/endpoint.cc \
  src/core/lib/iomgr/endpoint.h \
  src/core/lib/iomgr/endpoint_cfstream.cc \
  src/core/lib/iomgr/endpoint_cfstream.h \
  src/core/lib/iomgr/endpoint_pair.h \
  src/core/lib/iomgr/endpoint_pair_posix.cc \
  src/core/lib/iomgr/endpoint_pair_uv.cc \
  src/core/lib/iomgr/endpoint_pair_windows.cc \
  src/core/lib/iomgr/error.cc \
  src/core/lib/iomgr/error.h \
  src/core/lib/iomgr/error_cfstream.cc \
  src/core/lib/iomgr/error_cfstream.h \
  src/core/lib/iomgr/error_internal.h \
  src/core/lib/iomgr/ev_apple.cc \
  src/core/lib/iomgr/ev_apple.h \
  src/core/lib/iomgr/ev_epoll1_linux.cc \
  src/core/lib/iomgr/ev_epoll1_linux.h \
  src/core/lib/iomgr/ev_epollex_linux.cc \
  src/core/lib/iomgr/ev_epollex_linux.h \
  src/core/lib/iomgr/ev_poll_posix.cc \
  src/core/lib/iomgr/ev_poll_posix.h \
  src/core/lib/iomgr/ev_posix.cc \
  src/core/lib/iomgr/ev_posix.h \
  src/core/lib/iomgr/ev_windows.cc \
  src/core/lib/iomgr/exec_ctx.cc \
  src/core/lib/iomgr/exec_ctx.h \
  src/core/lib/iomgr/executor.cc \
  src/core/lib/iomgr/executor.h \
  src/core/lib/iomgr/executor/mpmcqueue.cc \
  src/core/lib/iomgr/executor/mpmcqueue.h \
  src/core/lib/iomgr/executor/threadpool.cc \
  src/core/lib/iomgr/executor/threadpool.h \
  src/core/lib/iomgr/fork_posix.cc \
  src/core/lib/iomgr/fork_windows.cc \
  src/core/lib/iomgr/gethostname.h \
  src/core/lib/iomgr/gethostname_fallback.cc \
  src/core/lib/iomgr/gethostname_host_name_max.cc \
  src/core/lib/iomgr/gethostname_sysconf.cc \
  src/core/lib/iomgr/grpc_if_nametoindex.h \
  src/core/lib/iomgr/grpc_if_nametoindex_posix.cc \
  src/core/lib/iomgr/grpc_if_nametoindex_unsupported.cc \
  src/core/lib/iomgr/internal_errqueue.cc \
  src/core/lib/iomgr/internal_errqueue.h \
  src/core/lib/iomgr/iocp_windows.cc \
  src/core/lib/iomgr/iocp_windows.h \
  src/core/lib/iomgr/iomgr.cc \
  src/core/lib/iomgr/iomgr.h \
  src/core/lib/iomgr/iomgr_custom.cc \
  src/core/lib/iomgr/iomgr_custom.h \
  src/core/lib/iomgr/iomgr_internal.cc \
  src/core/lib/iomgr/iomgr_internal.h \
  src/core/lib/iomgr/iomgr_posix.cc \
  src/core/lib/iomgr/iomgr_posix.h \
  src/core/lib/iomgr/iomgr_posix_cfstream.cc \
  src/core/lib/iomgr/iomgr_uv.cc \
  src/core/lib/iomgr/iomgr_windows.cc \
  src/core/lib/iomgr/is_epollexclusive_available.cc \
  src/core/lib/iomgr/is_epollexclusive_available.h \
  src/core/lib/iomgr/load_file.cc \
  src/core/lib/iomgr/load_file.h \
  src/core/lib/iomgr/lockfree_event.cc \
  src/core/lib/iomgr/lockfree_event.h \
  src/core/lib/iomgr/nameser.h \
  src/core/lib/iomgr/poller/eventmanager_libuv.cc \
  src/core/lib/iomgr/poller/eventmanager_libuv.h \
  src/core/lib/iomgr/polling_entity.cc \
  src/core/lib/iomgr/polling_entity.h \
  src/core/lib/iomgr/pollset.cc \
  src/core/lib/iomgr/pollset.h \
  src/core/lib/iomgr/pollset_custom.cc \
  src/core/lib/iomgr/pollset_custom.h \
  src/core/lib/iomgr/pollset_set.cc \
  src/core/lib/iomgr/pollset_set.h \
  src/core/lib/iomgr/pollset_set_custom.cc \
  src/core/lib/iomgr/pollset_set_custom.h \
  src/core/lib/iomgr/pollset_set_windows.cc \
  src/core/lib/iomgr/pollset_set_windows.h \
  src/core/lib/iomgr/pollset_uv.cc \
  src/core/lib/iomgr/pollset_uv.h \
  src/core/lib/iomgr/pollset_windows.cc \
  src/core/lib/iomgr/pollset_windows.h \
  src/core/lib/iomgr/port.h \
  src/core/lib/iomgr/python_util.h \
  src/core/lib/iomgr/resolve_address.cc \
  src/core/lib/iomgr/resolve_address.h \
  src/core/lib/iomgr/resolve_address_custom.cc \
  src/core/lib/iomgr/resolve_address_custom.h \
  src/core/lib/iomgr/resolve_address_posix.cc \
  src/core/lib/iomgr/resolve_address_windows.cc \
  src/core/lib/iomgr/resource_quota.cc \
  src/core/lib/iomgr/resource_quota.h \
  src/core/lib/iomgr/sockaddr.h \
  src/core/lib/iomgr/sockaddr_custom.h \
  src/core/lib/iomgr/sockaddr_posix.h \
  src/core/lib/iomgr/sockaddr_utils.cc \
  src/core/lib/iomgr/sockaddr_utils.h \
  src/core/lib/iomgr/sockaddr_windows.h \
  src/core/lib/iomgr/socket_factory_posix.cc \
  src/core/lib/iomgr/socket_factory_posix.h \
  src/core/lib/iomgr/socket_mutator.cc \
  src/core/lib/iomgr/socket_mutator.h \
  src/core/lib/iomgr/socket_utils.h \
  src/core/lib/iomgr/socket_utils_common_posix.cc \
  src/core/lib/iomgr/socket_utils_linux.cc \
  src/core/lib/iomgr/socket_utils_posix.cc \
  src/core/lib/iomgr/socket_utils_posix.h \
  src/core/lib/iomgr/socket_utils_uv.cc \
  src/core/lib/iomgr/socket_utils_windows.cc \
  src/core/lib/iomgr/socket_windows.cc \
  src/core/lib/iomgr/socket_windows.h \
  src/core/lib/iomgr/sys_epoll_wrapper.h \
  src/core/lib/iomgr/tcp_client.cc \
  src/core/lib/iomgr/tcp_client.h \
  src/core/lib/iomgr/tcp_client_cfstream.cc \
  src/core/lib/iomgr/tcp_client_custom.cc \
  src/core/lib/iomgr/tcp_client_posix.cc \
  src/core/lib/iomgr/tcp_client_posix.h \
  src/core/lib/iomgr/tcp_client_windows.cc \
  src/core/lib/iomgr/tcp_custom.cc \
  src/core/lib/iomgr/tcp_custom.h \
  src/core/lib/iomgr/tcp_posix.cc \
  src/core/lib/iomgr/tcp_posix.h \
  src/core/lib/iomgr/tcp_server.cc \
  src/core/lib/iomgr/tcp_server.h \
  src/core/lib/iomgr/tcp_server_custom.cc \
  src/core/lib/iomgr/tcp_server_posix.cc \
  src/core/lib/iomgr/tcp_server_utils_posix.h \
  src/core/lib/iomgr/tcp_server_utils_posix_common.cc \
  src/core/lib/iomgr/tcp_server_utils_posix_ifaddrs.cc \
  src/core/lib/iomgr/tcp_server_utils_posix_noifaddrs.cc \
  src/core/lib/iomgr/tcp_server_windows.cc \
  src/core/lib/iomgr/tcp_uv.cc \
  src/core/lib/iomgr/tcp_windows.cc \
  src/core/lib/iomgr/tcp_windows.h \
  src/core/lib/iomgr/time_averaged_stats.cc \
  src/core/lib/iomgr/time_averaged_stats.h \
  src/core/lib/iomgr/timer.cc \
  src/core/lib/iomgr/timer.h \
  src/core/lib/iomgr/timer_custom.cc \
  src/core/lib/iomgr/timer_custom.h \
  src/core/lib/iomgr/timer_generic.cc \
  src/core/lib/iomgr/timer_generic.h \
  src/core/lib/iomgr/timer_heap.cc \
  src/core/lib/iomgr/timer_heap.h \
  src/core/lib/iomgr/timer_manager.cc \
  src/core/lib/iomgr/timer_manager.h \
  src/core/lib/iomgr/timer_uv.cc \
  src/core/lib/iomgr/udp_server.cc \
  src/core/lib/iomgr/udp_server.h \
  src/core/lib/iomgr/unix_sockets_posix.cc \
  src/core/lib/iomgr/unix_sockets_posix.h \
  src/core/lib/iomgr/unix_sockets_posix_noop.cc \
  src/core/lib/iomgr/wakeup_fd_eventfd.cc \
  src/core/lib/iomgr/wakeup_fd_nospecial.cc \
  src/core/lib/iomgr/wakeup_fd_pipe.cc \
  src/core/lib/iomgr/wakeup_fd_pipe.h \
  src/core/lib/iomgr/wakeup_fd_posix.cc \
  src/core/lib/iomgr/wakeup_fd_posix.h \
  src/core/lib/iomgr/work_serializer.cc \
  src/core/lib/iomgr/work_serializer.h \
  src/core/lib/json/json.h \
  src/core/lib/json/json_reader.cc \
  src/core/lib/json/json_writer.cc \
  src/core/lib/security/context/security_context.cc \
  src/core/lib/security/context/security_context.h \
  src/core/lib/security/credentials/alts/alts_credentials.cc \
  src/core/lib/security/credentials/alts/alts_credentials.h \
  src/core/lib/security/credentials/alts/check_gcp_environment.cc \
  src/core/lib/security/credentials/alts/check_gcp_environment.h \
  src/core/lib/security/credentials/alts/check_gcp_environment_linux.cc \
  src/core/lib/security/credentials/alts/check_gcp_environment_no_op.cc \
  src/core/lib/security/credentials/alts/check_gcp_environment_windows.cc \
  src/core/lib/security/credentials/alts/grpc_alts_credentials_client_options.cc \
  src/core/lib/security/credentials/alts/grpc_alts_credentials_options.cc \
  src/core/lib/security/credentials/alts/grpc_alts_credentials_options.h \
  src/core/lib/security/credentials/alts/grpc_alts_credentials_server_options.cc \
  src/core/lib/security/credentials/composite/composite_credentials.cc \
  src/core/lib/security/credentials/composite/composite_credentials.h \
  src/core/lib/security/credentials/credentials.cc \
  src/core/lib/security/credentials/credentials.h \
  src/core/lib/security/credentials/credentials_metadata.cc \
  src/core/lib/security/credentials/fake/fake_credentials.cc \
  src/core/lib/security/credentials/fake/fake_credentials.h \
  src/core/lib/security/credentials/google_default/credentials_generic.cc \
  src/core/lib/security/credentials/google_default/google_default_credentials.cc \
  src/core/lib/security/credentials/google_default/google_default_credentials.h \
  src/core/lib/security/credentials/iam/iam_credentials.cc \
  src/core/lib/security/credentials/iam/iam_credentials.h \
  src/core/lib/security/credentials/jwt/json_token.cc \
  src/core/lib/security/credentials/jwt/json_token.h \
  src/core/lib/security/credentials/jwt/jwt_credentials.cc \
  src/core/lib/security/credentials/jwt/jwt_credentials.h \
  src/core/lib/security/credentials/jwt/jwt_verifier.cc \
  src/core/lib/security/credentials/jwt/jwt_verifier.h \
  src/core/lib/security/credentials/local/local_credentials.cc \
  src/core/lib/security/credentials/local/local_credentials.h \
  src/core/lib/security/credentials/oauth2/oauth2_credentials.cc \
  src/core/lib/security/credentials/oauth2/oauth2_credentials.h \
  src/core/lib/security/credentials/plugin/plugin_credentials.cc \
  src/core/lib/security/credentials/plugin/plugin_credentials.h \
  src/core/lib/security/credentials/ssl/ssl_credentials.cc \
  src/core/lib/security/credentials/ssl/ssl_credentials.h \
  src/core/lib/security/credentials/tls/grpc_tls_credentials_options.cc \
  src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h \
  src/core/lib/security/credentials/tls/tls_credentials.cc \
  src/core/lib/security/credentials/tls/tls_credentials.h \
  src/core/lib/security/security_connector/alts/alts_security_connector.cc \
  src/core/lib/security/security_connector/alts/alts_security_connector.h \
  src/core/lib/security/security_connector/fake/fake_security_connector.cc \
  src/core/lib/security/security_connector/fake/fake_security_connector.h \
  src/core/lib/security/security_connector/load_system_roots.h \
  src/core/lib/security/security_connector/load_system_roots_fallback.cc \
  src/core/lib/security/security_connector/load_system_roots_linux.cc \
  src/core/lib/security/security_connector/load_system_roots_linux.h \
  src/core/lib/security/security_connector/local/local_security_connector.cc \
  src/core/lib/security/security_connector/local/local_security_connector.h \
  src/core/lib/security/security_connector/security_connector.cc \
  src/core/lib/security/security_connector/security_connector.h \
  src/core/lib/security/security_connector/ssl/ssl_security_connector.cc \
  src/core/lib/security/security_connector/ssl/ssl_security_connector.h \
  src/core/lib/security/security_connector/ssl_utils.cc \
  src/core/lib/security/security_connector/ssl_utils.h \
  src/core/lib/security/security_connector/ssl_utils_config.cc \
  src/core/lib/security/security_connector/ssl_utils_config.h \
  src/core/lib/security/security_connector/tls/tls_security_connector.cc \
  src/core/lib/security/security_connector/tls/tls_security_connector.h \
  src/core/lib/security/transport/auth_filters.h \
  src/core/lib/security/transport/client_auth_filter.cc \
  src/core/lib/security/transport/secure_endpoint.cc \
  src/core/lib/security/transport/secure_endpoint.h \
  src/core/lib/security/transport/security_handshaker.cc \
  src/core/lib/security/transport/security_handshaker.h \
  src/core/lib/security/transport/server_auth_filter.cc \
  src/core/lib/security/transport/target_authority_table.cc \
  src/core/lib/security/transport/target_authority_table.h \
  src/core/lib/security/transport/tsi_error.cc \
  src/core/lib/security/transport/tsi_error.h \
  src/core/lib/security/util/json_util.cc \
  src/core/lib/security/util/json_util.h \
  src/core/lib/slice/b64.cc \
  src/core/lib/slice/b64.h \
  src/core/lib/slice/percent_encoding.cc \
  src/core/lib/slice/percent_encoding.h \
  src/core/lib/slice/slice.cc \
  src/core/lib/slice/slice_buffer.cc \
  src/core/lib/slice/slice_hash_table.h \
  src/core/lib/slice/slice_intern.cc \
  src/core/lib/slice/slice_internal.h \
  src/core/lib/slice/slice_string_helpers.cc \
  src/core/lib/slice/slice_string_helpers.h \
  src/core/lib/slice/slice_utils.h \
  src/core/lib/slice/slice_weak_hash_table.h \
  src/core/lib/surface/api_trace.cc \
  src/core/lib/surface/api_trace.h \
  src/core/lib/surface/byte_buffer.cc \
  src/core/lib/surface/byte_buffer_reader.cc \
  src/core/lib/surface/call.cc \
  src/core/lib/surface/call.h \
  src/core/lib/surface/call_details.cc \
  src/core/lib/surface/call_log_batch.cc \
  src/core/lib/surface/call_test_only.h \
  src/core/lib/surface/channel.cc \
  src/core/lib/surface/channel.h \
  src/core/lib/surface/channel_init.cc \
  src/core/lib/surface/channel_init.h \
  src/core/lib/surface/channel_ping.cc \
  src/core/lib/surface/channel_stack_type.cc \
  src/core/lib/surface/channel_stack_type.h \
  src/core/lib/surface/completion_queue.cc \
  src/core/lib/surface/completion_queue.h \
  src/core/lib/surface/completion_queue_factory.cc \
  src/core/lib/surface/completion_queue_factory.h \
  src/core/lib/surface/event_string.cc \
  src/core/lib/surface/event_string.h \
  src/core/lib/surface/init.cc \
  src/core/lib/surface/init.h \
  src/core/lib/surface/init_secure.cc \
  src/core/lib/surface/lame_client.cc \
  src/core/lib/surface/lame_client.h \
  src/core/lib/surface/metadata_array.cc \
  src/core/lib/surface/server.cc \
  src/core/lib/surface/server.h \
  src/core/lib/surface/validate_metadata.cc \
  src/core/lib/surface/validate_metadata.h \
  src/core/lib/surface/version.cc \
  src/core/lib/transport/bdp_estimator.cc \
  src/core/lib/transport/bdp_estimator.h \
  src/core/lib/transport/byte_stream.cc \
  src/core/lib/transport/byte_stream.h \
  src/core/lib/transport/connectivity_state.cc \
  src/core/lib/transport/connectivity_state.h \
  src/core/lib/transport/error_utils.cc \
  src/core/lib/transport/error_utils.h \
  src/core/lib/transport/http2_errors.h \
  src/core/lib/transport/metadata.cc \
  src/core/lib/transport/metadata.h \
  src/core/lib/transport/metadata_batch.cc \
  src/core/lib/transport/metadata_batch.h \
  src/core/lib/transport/pid_controller.cc \
  src/core/lib/transport/pid_controller.h \
  src/core/lib/transport/static_metadata.cc \
  src/core/lib/transport/static_metadata.h \
  src/core/lib/transport/status_conversion.cc \
  src/core/lib/transport/status_conversion.h \
  src/core/lib/transport/status_metadata.cc \
  src/core/lib/transport/status_metadata.h \
  src/core/lib/transport/timeout_encoding.cc \
  src/core/lib/transport/timeout_encoding.h \
  src/core/lib/transport/transport.cc \
  src/core/lib/transport/transport.h \
  src/core/lib/transport/transport_impl.h \
  src/core/lib/transport/transport_op_string.cc \
  src/core/lib/uri/uri_parser.cc \
  src/core/lib/uri/uri_parser.h \
  src/core/plugin_registry/grpc_plugin_registry.cc \
  src/core/tsi/alts/crypt/aes_gcm.cc \
  src/core/tsi/alts/crypt/gsec.cc \
  src/core/tsi/alts/crypt/gsec.h \
  src/core/tsi/alts/frame_protector/alts_counter.cc \
  src/core/tsi/alts/frame_protector/alts_counter.h \
  src/core/tsi/alts/frame_protector/alts_crypter.cc \
  src/core/tsi/alts/frame_protector/alts_crypter.h \
  src/core/tsi/alts/frame_protector/alts_frame_protector.cc \
  src/core/tsi/alts/frame_protector/alts_frame_protector.h \
  src/core/tsi/alts/frame_protector/alts_record_protocol_crypter_common.cc \
  src/core/tsi/alts/frame_protector/alts_record_protocol_crypter_common.h \
  src/core/tsi/alts/frame_protector/alts_seal_privacy_integrity_crypter.cc \
  src/core/tsi/alts/frame_protector/alts_unseal_privacy_integrity_crypter.cc \
  src/core/tsi/alts/frame_protector/frame_handler.cc \
  src/core/tsi/alts/frame_protector/frame_handler.h \
  src/core/tsi/alts/handshaker/alts_handshaker_client.cc \
  src/core/tsi/alts/handshaker/alts_handshaker_client.h \
  src/core/tsi/alts/handshaker/alts_shared_resource.cc \
  src/core/tsi/alts/handshaker/alts_shared_resource.h \
  src/core/tsi/alts/handshaker/alts_tsi_handshaker.cc \
  src/core/tsi/alts/handshaker/alts_tsi_handshaker.h \
  src/core/tsi/alts/handshaker/alts_tsi_handshaker_private.h \
  src/core/tsi/alts/handshaker/alts_tsi_utils.cc \
  src/core/tsi/alts/handshaker/alts_tsi_utils.h \
  src/core/tsi/alts/handshaker/transport_security_common_api.cc \
  src/core/tsi/alts/handshaker/transport_security_common_api.h \
  src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_integrity_only_record_protocol.cc \
  src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_integrity_only_record_protocol.h \
  src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_privacy_integrity_record_protocol.cc \
  src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_privacy_integrity_record_protocol.h \
  src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_record_protocol.h \
  src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_record_protocol_common.cc \
  src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_record_protocol_common.h \
  src/core/tsi/alts/zero_copy_frame_protector/alts_iovec_record_protocol.cc \
  src/core/tsi/alts/zero_copy_frame_protector/alts_iovec_record_protocol.h \
  src/core/tsi/alts/zero_copy_frame_protector/alts_zero_copy_grpc_protector.cc \
  src/core/tsi/alts/zero_copy_frame_protector/alts_zero_copy_grpc_protector.h \
  src/core/tsi/fake_transport_security.cc \
  src/core/tsi/fake_transport_security.h \
  src/core/tsi/local_transport_security.cc \
  src/core/tsi/local_transport_security.h \
  src/core/tsi/ssl/session_cache/ssl_session.h \
  src/core/tsi/ssl/session_cache/ssl_session_boringssl.cc \
  src/core/tsi/ssl/session_cache/ssl_session_cache.cc \
  src/core/tsi/ssl/session_cache/ssl_session_cache.h \
  src/core/tsi/ssl/session_cache/ssl_session_openssl.cc \
  src/core/tsi/ssl_transport_security.cc \
  src/core/tsi/ssl_transport_security.h \
  src/core/tsi/ssl_types.h \
  src/core/tsi/transport_security.cc \
  src/core/tsi/transport_security.h \
  src/core/tsi/transport_security_grpc.cc \
  src/core/tsi/transport_security_grpc.h \
  src/core/tsi/transport_security_interface.h \
  ; do
  sed 's/\.upbdefs/_upbdefs/g' $file > $output_file
  mv $output_file $file
done
