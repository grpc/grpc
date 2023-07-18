#!/usr/bin/env python2.7
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
"""Generates the appropriate build.json data for all the naming tests."""

import collections
import hashlib
import json

import yaml

_LOCAL_DNS_SERVER_ADDRESS = "127.0.0.1:15353"


def _append_zone_name(name, zone_name):
    return "%s.%s" % (name, zone_name)


def _build_expected_addrs_cmd_arg(expected_addrs):
    out = []
    for addr in expected_addrs:
        out.append("%s,%s" % (addr["address"], str(addr["is_balancer"])))
    return ";".join(out)


def _resolver_test_cases(resolver_component_data):
    out = []
    for test_case in resolver_component_data["resolver_component_tests"]:
        target_name = _append_zone_name(
            test_case["record_to_resolve"],
            resolver_component_data["resolver_tests_common_zone_name"],
        )
        out.append(
            {
                "test_title": target_name,
                "arg_names_and_values": [
                    ("target_name", target_name),
                    (
                        "do_ordered_address_comparison",
                        test_case["do_ordered_address_comparison"],
                    ),
                    (
                        "expected_addrs",
                        _build_expected_addrs_cmd_arg(
                            test_case["expected_addrs"]
                        ),
                    ),
                    (
                        "expected_chosen_service_config",
                        (test_case["expected_chosen_service_config"] or ""),
                    ),
                    (
                        "expected_service_config_error",
                        (test_case["expected_service_config_error"] or ""),
                    ),
                    (
                        "expected_lb_policy",
                        (test_case["expected_lb_policy"] or ""),
                    ),
                    ("enable_srv_queries", test_case["enable_srv_queries"]),
                    ("enable_txt_queries", test_case["enable_txt_queries"]),
                    (
                        "inject_broken_nameserver_list",
                        test_case["inject_broken_nameserver_list"],
                    ),
                ],
            }
        )
    return out


def main():
    resolver_component_data = ""
    with open("test/cpp/naming/resolver_test_record_groups.yaml") as f:
        resolver_component_data = yaml.safe_load(f)

    json = {
        "resolver_tests_common_zone_name": resolver_component_data[
            "resolver_tests_common_zone_name"
        ],
        # this data is required by the resolver_component_tests_runner.py.template
        "resolver_component_test_cases": _resolver_test_cases(
            resolver_component_data
        ),
    }

    print(yaml.safe_dump(json))


if __name__ == "__main__":
    main()
