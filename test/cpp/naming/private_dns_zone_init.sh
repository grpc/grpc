#!/bin/bash
# Copyright 2015 gRPC authors.
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

# This file is auto-generated

set -ex

cd "$(dirname "$0")/../../.."

gcloud dns record-sets transaction start -z=resolver-tests-version-8-grpctestingexp-zone-id

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=no-srv-ipv4-single-target.resolver-tests-version-8.grpctestingexp. \
  --type=A \
  --ttl=2100 \
  "5.5.5.5"

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=_grpclb._tcp.srv-ipv4-single-target.resolver-tests-version-8.grpctestingexp. \
  --type=SRV \
  --ttl=2100 \
  "0 0 1234 ipv4-single-target.resolver-tests-version-8.grpctestingexp."

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=ipv4-single-target.resolver-tests-version-8.grpctestingexp. \
  --type=A \
  --ttl=2100 \
  "1.2.3.4"

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=_grpclb._tcp.srv-ipv4-multi-target.resolver-tests-version-8.grpctestingexp. \
  --type=SRV \
  --ttl=2100 \
  "0 0 1234 ipv4-multi-target.resolver-tests-version-8.grpctestingexp."

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=ipv4-multi-target.resolver-tests-version-8.grpctestingexp. \
  --type=A \
  --ttl=2100 \
  "1.2.3.5" "1.2.3.6" "1.2.3.7"

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=_grpclb._tcp.srv-ipv6-single-target.resolver-tests-version-8.grpctestingexp. \
  --type=SRV \
  --ttl=2100 \
  "0 0 1234 ipv6-single-target.resolver-tests-version-8.grpctestingexp."

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=ipv6-single-target.resolver-tests-version-8.grpctestingexp. \
  --type=AAAA \
  --ttl=2100 \
  "2607:f8b0:400a:801::1001"

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=_grpclb._tcp.srv-ipv6-multi-target.resolver-tests-version-8.grpctestingexp. \
  --type=SRV \
  --ttl=2100 \
  "0 0 1234 ipv6-multi-target.resolver-tests-version-8.grpctestingexp."

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=ipv6-multi-target.resolver-tests-version-8.grpctestingexp. \
  --type=AAAA \
  --ttl=2100 \
  "2607:f8b0:400a:801::1002" "2607:f8b0:400a:801::1003" "2607:f8b0:400a:801::1004"

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=_grpc_config.srv-ipv4-simple-service-config.resolver-tests-version-8.grpctestingexp. \
  --type=TXT \
  --ttl=2100 \
  '"grpc_config=[{\"serviceConfig\":{\"loadBalancingPolicy\":\"round_robin\",\"methodConfig\":[{\"name\":[{\"method\":\"Foo\",\"service\":\"SimpleService\"}],\"waitForReady\":true}]}}]"'

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=_grpclb._tcp.srv-ipv4-simple-service-config.resolver-tests-version-8.grpctestingexp. \
  --type=SRV \
  --ttl=2100 \
  "0 0 1234 ipv4-simple-service-config.resolver-tests-version-8.grpctestingexp."

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=ipv4-simple-service-config.resolver-tests-version-8.grpctestingexp. \
  --type=A \
  --ttl=2100 \
  "1.2.3.4"

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=ipv4-no-srv-simple-service-config.resolver-tests-version-8.grpctestingexp. \
  --type=A \
  --ttl=2100 \
  "1.2.3.4"

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=_grpc_config.ipv4-no-srv-simple-service-config.resolver-tests-version-8.grpctestingexp. \
  --type=TXT \
  --ttl=2100 \
  '"grpc_config=[{\"serviceConfig\":{\"loadBalancingPolicy\":\"round_robin\",\"methodConfig\":[{\"name\":[{\"method\":\"Foo\",\"service\":\"NoSrvSimpleService\"}],\"waitForReady\":true}]}}]"'

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=_grpc_config.ipv4-no-config-for-cpp.resolver-tests-version-8.grpctestingexp. \
  --type=TXT \
  --ttl=2100 \
  '"grpc_config=[{\"clientLanguage\":[\"python\"],\"serviceConfig\":{\"loadBalancingPolicy\":\"round_robin\",\"methodConfig\":[{\"name\":[{\"method\":\"Foo\",\"service\":\"PythonService\"}],\"waitForReady\":true}]}}]"'

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=ipv4-no-config-for-cpp.resolver-tests-version-8.grpctestingexp. \
  --type=A \
  --ttl=2100 \
  "1.2.3.4"

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=ipv4-cpp-config-has-zero-percentage.resolver-tests-version-8.grpctestingexp. \
  --type=A \
  --ttl=2100 \
  "1.2.3.4"

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=_grpc_config.ipv4-cpp-config-has-zero-percentage.resolver-tests-version-8.grpctestingexp. \
  --type=TXT \
  --ttl=2100 \
  '"grpc_config=[{\"percentage\":0,\"serviceConfig\":{\"loadBalancingPolicy\":\"round_robin\",\"methodConfig\":[{\"name\":[{\"method\":\"Foo\",\"service\":\"CppService\"}],\"waitForReady\":true}]}}]"'

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=_grpc_config.ipv4-second-language-is-cpp.resolver-tests-version-8.grpctestingexp. \
  --type=TXT \
  --ttl=2100 \
  '"grpc_config=[{\"clientLanguage\":[\"go\"],\"serviceConfig\":{\"loadBalancingPolicy\":\"round_robin\",\"methodConfig\":[{\"name\":[{\"method\":\"Foo\",\"service\":\"GoService\"}],\"waitForReady\":true}]}},{\"clientLanguage\":[\"c++\"],\"serviceConfig\":{" "\"loadBalancingPolicy\":\"round_robin\",\"methodConfig\":[{\"name\":[{\"method\":\"Foo\",\"service\":\"CppService\"}],\"waitForReady\":true}]}}]"'

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=ipv4-second-language-is-cpp.resolver-tests-version-8.grpctestingexp. \
  --type=A \
  --ttl=2100 \
  "1.2.3.4"

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=ipv4-config-with-percentages.resolver-tests-version-8.grpctestingexp. \
  --type=A \
  --ttl=2100 \
  "1.2.3.4"

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=_grpc_config.ipv4-config-with-percentages.resolver-tests-version-8.grpctestingexp. \
  --type=TXT \
  --ttl=2100 \
  '"grpc_config=[{\"percentage\":0,\"serviceConfig\":{\"loadBalancingPolicy\":\"round_robin\",\"methodConfig\":[{\"name\":[{\"method\":\"Foo\",\"service\":\"NeverPickedService\"}],\"waitForReady\":true}]}},{\"percentage\":100,\"serviceConfig\":{\"loadBalanc" "ingPolicy\":\"round_robin\",\"methodConfig\":[{\"name\":[{\"method\":\"Foo\",\"service\":\"AlwaysPickedService\"}],\"waitForReady\":true}]}}]"'

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=_grpclb._tcp.srv-ipv4-target-has-backend-and-balancer.resolver-tests-version-8.grpctestingexp. \
  --type=SRV \
  --ttl=2100 \
  "0 0 1234 balancer-for-ipv4-has-backend-and-balancer.resolver-tests-version-8.grpctestingexp."

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=balancer-for-ipv4-has-backend-and-balancer.resolver-tests-version-8.grpctestingexp. \
  --type=A \
  --ttl=2100 \
  "1.2.3.4"

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=srv-ipv4-target-has-backend-and-balancer.resolver-tests-version-8.grpctestingexp. \
  --type=A \
  --ttl=2100 \
  "1.2.3.4"

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=_grpclb._tcp.srv-ipv6-target-has-backend-and-balancer.resolver-tests-version-8.grpctestingexp. \
  --type=SRV \
  --ttl=2100 \
  "0 0 1234 balancer-for-ipv6-has-backend-and-balancer.resolver-tests-version-8.grpctestingexp."

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=balancer-for-ipv6-has-backend-and-balancer.resolver-tests-version-8.grpctestingexp. \
  --type=AAAA \
  --ttl=2100 \
  "2607:f8b0:400a:801::1002"

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=srv-ipv6-target-has-backend-and-balancer.resolver-tests-version-8.grpctestingexp. \
  --type=AAAA \
  --ttl=2100 \
  "2607:f8b0:400a:801::1002"

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=large-aaaa-triggering-tcp-fallback.resolver-tests-version-8.grpctestingexp. \
  --type=AAAA \
  --ttl=2100 \
  "::1" "100::1" "100::2" "100::3" "100::4" "100::5" "100::6" "100::7" "100::8" "100::9" "100::10" "100::11" "100::12" "100::13" "100::14" "100::15" "100::16" "100::17" "100::18" "100::19" "100::20" "100::21" "100::22" "100::23" "100::24" "100::25" "100::26" "100::27" "100::28" "100::29"

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=_grpclb._tcp.srv-ipv4-single-target-srv-disabled.resolver-tests-version-8.grpctestingexp. \
  --type=SRV \
  --ttl=2100 \
  "0 0 1234 ipv4-single-target-srv-disabled.resolver-tests-version-8.grpctestingexp."

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=srv-ipv4-single-target-srv-disabled.resolver-tests-version-8.grpctestingexp. \
  --type=A \
  --ttl=2100 \
  "2.3.4.5"

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=ipv4-single-target-srv-disabled.resolver-tests-version-8.grpctestingexp. \
  --type=A \
  --ttl=2100 \
  "1.2.3.4"

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=srv-ipv4-multi-target-srv-disabled.resolver-tests-version-8.grpctestingexp. \
  --type=A \
  --ttl=2100 \
  "9.2.3.5" "9.2.3.6" "9.2.3.7"

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=_grpclb._tcp.srv-ipv4-multi-target-srv-disabled.resolver-tests-version-8.grpctestingexp. \
  --type=SRV \
  --ttl=2100 \
  "0 0 1234 ipv4-multi-target-srv-disabled.resolver-tests-version-8.grpctestingexp."

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=ipv4-multi-target-srv-disabled.resolver-tests-version-8.grpctestingexp. \
  --type=A \
  --ttl=2100 \
  "1.2.3.5" "1.2.3.6" "1.2.3.7"

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=_grpclb._tcp.srv-ipv6-single-target-srv-disabled.resolver-tests-version-8.grpctestingexp. \
  --type=SRV \
  --ttl=2100 \
  "0 0 1234 ipv6-single-target-srv-disabled.resolver-tests-version-8.grpctestingexp."

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=ipv6-single-target-srv-disabled.resolver-tests-version-8.grpctestingexp. \
  --type=AAAA \
  --ttl=2100 \
  "2607:f8b0:400a:801::1001"

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=srv-ipv6-single-target-srv-disabled.resolver-tests-version-8.grpctestingexp. \
  --type=AAAA \
  --ttl=2100 \
  "2600::1001"

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=srv-ipv6-multi-target-srv-disabled.resolver-tests-version-8.grpctestingexp. \
  --type=AAAA \
  --ttl=2100 \
  "2600::1002" "2600::1003" "2600::1004"

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=_grpclb._tcp.srv-ipv6-multi-target-srv-disabled.resolver-tests-version-8.grpctestingexp. \
  --type=SRV \
  --ttl=2100 \
  "0 0 1234 ipv6-multi-target-srv-disabled.resolver-tests-version-8.grpctestingexp."

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=ipv6-multi-target-srv-disabled.resolver-tests-version-8.grpctestingexp. \
  --type=AAAA \
  --ttl=2100 \
  "2607:f8b0:400a:801::1002" "2607:f8b0:400a:801::1003" "2607:f8b0:400a:801::1004"

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=_grpclb._tcp.srv-ipv4-simple-service-config-srv-disabled.resolver-tests-version-8.grpctestingexp. \
  --type=SRV \
  --ttl=2100 \
  "0 0 1234 ipv4-simple-service-config-srv-disabled.resolver-tests-version-8.grpctestingexp."

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=_grpc_config.srv-ipv4-simple-service-config-srv-disabled.resolver-tests-version-8.grpctestingexp. \
  --type=TXT \
  --ttl=2100 \
  '"grpc_config=[{\"serviceConfig\":{\"loadBalancingPolicy\":\"round_robin\",\"methodConfig\":[{\"name\":[{\"method\":\"Foo\",\"service\":\"SimpleService\"}],\"waitForReady\":true}]}}]"'

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=srv-ipv4-simple-service-config-srv-disabled.resolver-tests-version-8.grpctestingexp. \
  --type=A \
  --ttl=2100 \
  "5.5.3.4"

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=ipv4-simple-service-config-srv-disabled.resolver-tests-version-8.grpctestingexp. \
  --type=A \
  --ttl=2100 \
  "1.2.3.4"

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=_grpclb._tcp.srv-ipv4-simple-service-config-txt-disabled.resolver-tests-version-8.grpctestingexp. \
  --type=SRV \
  --ttl=2100 \
  "0 0 1234 ipv4-simple-service-config-txt-disabled.resolver-tests-version-8.grpctestingexp."

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=ipv4-simple-service-config-txt-disabled.resolver-tests-version-8.grpctestingexp. \
  --type=A \
  --ttl=2100 \
  "1.2.3.4"

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=_grpc_config.srv-ipv4-simple-service-config-txt-disabled.resolver-tests-version-8.grpctestingexp. \
  --type=TXT \
  --ttl=2100 \
  '"grpc_config=[{\"serviceConfig\":{\"loadBalancingPolicy\":\"round_robin\",\"methodConfig\":[{\"name\":[{\"method\":\"Foo\",\"service\":\"SimpleService\"}],\"waitForReady\":true}]}}]"'

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=ipv4-cpp-config-has-zero-percentage-txt-disabled.resolver-tests-version-8.grpctestingexp. \
  --type=A \
  --ttl=2100 \
  "1.2.3.4"

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=_grpc_config.ipv4-cpp-config-has-zero-percentage-txt-disabled.resolver-tests-version-8.grpctestingexp. \
  --type=TXT \
  --ttl=2100 \
  '"grpc_config=[{\"percentage\":0,\"serviceConfig\":{\"loadBalancingPolicy\":\"round_robin\",\"methodConfig\":[{\"name\":[{\"method\":\"Foo\",\"service\":\"CppService\"}],\"waitForReady\":true}]}}]"'

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=ipv4-second-language-is-cpp-txt-disabled.resolver-tests-version-8.grpctestingexp. \
  --type=A \
  --ttl=2100 \
  "1.2.3.4"

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=_grpc_config.ipv4-second-language-is-cpp-txt-disabled.resolver-tests-version-8.grpctestingexp. \
  --type=TXT \
  --ttl=2100 \
  '"grpc_config=[{\"clientLanguage\":[\"go\"],\"serviceConfig\":{\"loadBalancingPolicy\":\"round_robin\",\"methodConfig\":[{\"name\":[{\"method\":\"Foo\",\"service\":\"GoService\"}],\"waitForReady\":true}]}},{\"clientLanguage\":[\"c++\"],\"serviceConfig\":{" "\"loadBalancingPolicy\":\"round_robin\",\"methodConfig\":[{\"name\":[{\"method\":\"Foo\",\"service\":\"CppService\"}],\"waitForReady\":true}]}}]"'

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=_grpc_config.ipv4-svc_cfg_bad_json.resolver-tests-version-8.grpctestingexp. \
  --type=TXT \
  --ttl=2100 \
  '"grpc_config=[{]"'

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=ipv4-svc_cfg_bad_json.resolver-tests-version-8.grpctestingexp. \
  --type=A \
  --ttl=2100 \
  "1.2.3.4"

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=ipv4-svc_cfg_bad_client_language.resolver-tests-version-8.grpctestingexp. \
  --type=A \
  --ttl=2100 \
  "1.2.3.4"

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=_grpc_config.ipv4-svc_cfg_bad_client_language.resolver-tests-version-8.grpctestingexp. \
  --type=TXT \
  --ttl=2100 \
  '"grpc_config=[{\"clientLanguage\":\"go\",\"serviceConfig\":{\"loadBalancingPolicy\":\"round_robin\",\"methodConfig\":[{\"name\":[{\"method\":\"Foo\",\"service\":\"GoService\"}],\"waitForReady\":true}]}},{\"clientLanguage\":[\"c++\"],\"serviceConfig\":{\"" "loadBalancingPolicy\":\"round_robin\",\"methodConfig\":[{\"name\":[{\"method\":\"Foo\",\"service\":\"CppService\"}],\"waitForReady\":true}]}}]"'

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=_grpc_config.ipv4-svc_cfg_bad_percentage.resolver-tests-version-8.grpctestingexp. \
  --type=TXT \
  --ttl=2100 \
  '"grpc_config=[{\"percentage\":\"0\",\"serviceConfig\":{\"loadBalancingPolicy\":\"round_robin\",\"methodConfig\":[{\"name\":[{\"method\":\"Foo\",\"service\":\"GoService\"}],\"waitForReady\":true}]}},{\"clientLanguage\":[\"c++\"],\"serviceConfig\":{\"loadB" "alancingPolicy\":\"round_robin\",\"methodConfig\":[{\"name\":[{\"method\":\"Foo\",\"service\":\"CppService\"}],\"waitForReady\":true}]}}]"'

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=ipv4-svc_cfg_bad_percentage.resolver-tests-version-8.grpctestingexp. \
  --type=A \
  --ttl=2100 \
  "1.2.3.4"

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=ipv4-svc_cfg_bad_wait_for_ready.resolver-tests-version-8.grpctestingexp. \
  --type=A \
  --ttl=2100 \
  "1.2.3.4"

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=_grpc_config.ipv4-svc_cfg_bad_wait_for_ready.resolver-tests-version-8.grpctestingexp. \
  --type=TXT \
  --ttl=2100 \
  '"grpc_config=[{\"serviceConfig\":{\"methodConfig\":[{\"name\":[{\"method\":\"Foo\",\"service\":\"CppService\"}],\"waitForReady\":\"true\"}]}}]"'

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=no-srv-ipv4-single-target-inject-broken-nameservers.resolver-tests-version-8.grpctestingexp. \
  --type=A \
  --ttl=2100 \
  "5.5.5.5"

gcloud dns record-sets transaction add \
  -z=resolver-tests-version-8-grpctestingexp-zone-id \
  --name=large-aaaa-triggering-tcp-fallback-inject-broken-nameservers.resolver-tests-version-8.grpctestingexp. \
  --type=AAAA \
  --ttl=2100 \
  "::1" "100::1" "100::2" "100::3" "100::4" "100::5" "100::6" "100::7" "100::8" "100::9" "100::10" "100::11" "100::12" "100::13" "100::14" "100::15" "100::16" "100::17" "100::18" "100::19" "100::20" "100::21" "100::22" "100::23" "100::24" "100::25" "100::26" "100::27" "100::28" "100::29"

gcloud dns record-sets transaction describe -z=resolver-tests-version-8-grpctestingexp-zone-id
gcloud dns record-sets transaction execute -z=resolver-tests-version-8-grpctestingexp-zone-id
gcloud dns record-sets list -z=resolver-tests-version-8-grpctestingexp-zone-id
