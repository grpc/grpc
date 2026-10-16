#!/usr/bin/env python3
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
"""Generates the appropriate JSON data for LB interop test scenarios."""

import json
import os

import yaml

all_scenarios = []

# TODO(https://github.com/grpc/grpc-go/issues/2347): enable
# client_falls_back_because_no_backends_* scenarios for Java/Go.

# TODO(https://github.com/grpc/grpc-java/issues/4887): enable
# *short_stream* scenarios for Java.

# TODO(https://github.com/grpc/grpc-java/issues/4912): enable
# Java TLS tests involving TLS to the balancer.


def server_sec(transport_sec):
    if transport_sec == "google_default_credentials":
        return "alts", "alts", "tls"
    return transport_sec, transport_sec, transport_sec


def generate_no_balancer_because_lb_a_record_returns_nx_domain():
    all_configs = []
    for transport_sec in [
        "insecure",
        "alts",
        "tls",
        "google_default_credentials",
    ]:
        balancer_sec, backend_sec, fallback_sec = server_sec(transport_sec)
        config = {
            "name": "no_balancer_because_lb_a_record_returns_nx_domain_%s"
            % transport_sec,
            "skip_langs": [],
            "transport_sec": transport_sec,
            "balancer_configs": [],
            "backend_configs": [],
            "fallback_configs": [
                {
                    "transport_sec": fallback_sec,
                }
            ],
            "cause_no_error_no_data_for_balancer_a_record": False,
        }
        all_configs.append(config)
    return all_configs


all_scenarios += generate_no_balancer_because_lb_a_record_returns_nx_domain()


def generate_no_balancer_because_lb_a_record_returns_no_data():
    all_configs = []
    for transport_sec in [
        "insecure",
        "alts",
        "tls",
        "google_default_credentials",
    ]:
        balancer_sec, backend_sec, fallback_sec = server_sec(transport_sec)
        config = {
            "name": "no_balancer_because_lb_a_record_returns_no_data_%s"
            % transport_sec,
            "skip_langs": [],
            "transport_sec": transport_sec,
            "balancer_configs": [],
            "backend_configs": [],
            "fallback_configs": [
                {
                    "transport_sec": fallback_sec,
                }
            ],
            "cause_no_error_no_data_for_balancer_a_record": True,
        }
        all_configs.append(config)
    return all_configs


all_scenarios += generate_no_balancer_because_lb_a_record_returns_no_data()


def generate_client_referred_to_backend():
    all_configs = []
    for balancer_short_stream in [True, False]:
        for transport_sec in [
            "insecure",
            "alts",
            "tls",
            "google_default_credentials",
        ]:
            balancer_sec, backend_sec, fallback_sec = server_sec(transport_sec)
            skip_langs = []
            if transport_sec == "tls":
                skip_langs += ["java"]
            if balancer_short_stream:
                skip_langs += ["java"]
            config = {
                "name": "client_referred_to_backend_%s_short_stream_%s"
                % (transport_sec, balancer_short_stream),
                "skip_langs": skip_langs,
                "transport_sec": transport_sec,
                "balancer_configs": [
                    {
                        "transport_sec": balancer_sec,
                        "short_stream": balancer_short_stream,
                    }
                ],
                "backend_configs": [
                    {
                        "transport_sec": backend_sec,
                    }
                ],
                "fallback_configs": [],
                "cause_no_error_no_data_for_balancer_a_record": False,
            }
            all_configs.append(config)
    return all_configs


all_scenarios += generate_client_referred_to_backend()


def generate_client_referred_to_backend_fallback_broken():
    all_configs = []
    for balancer_short_stream in [True, False]:
        for transport_sec in ["alts", "tls", "google_default_credentials"]:
            balancer_sec, backend_sec, fallback_sec = server_sec(transport_sec)
            skip_langs = []
            if transport_sec == "tls":
                skip_langs += ["java"]
            if balancer_short_stream:
                skip_langs += ["java"]
            config = {
                "name": "client_referred_to_backend_fallback_broken_%s_short_stream_%s"
                % (transport_sec, balancer_short_stream),
                "skip_langs": skip_langs,
                "transport_sec": transport_sec,
                "balancer_configs": [
                    {
                        "transport_sec": balancer_sec,
                        "short_stream": balancer_short_stream,
                    }
                ],
                "backend_configs": [
                    {
                        "transport_sec": backend_sec,
                    }
                ],
                "fallback_configs": [
                    {
                        "transport_sec": "insecure",
                    }
                ],
                "cause_no_error_no_data_for_balancer_a_record": False,
            }
            all_configs.append(config)
    return all_configs


all_scenarios += generate_client_referred_to_backend_fallback_broken()


def generate_client_referred_to_backend_multiple_backends():
    all_configs = []
    for balancer_short_stream in [True, False]:
        for transport_sec in [
            "insecure",
            "alts",
            "tls",
            "google_default_credentials",
        ]:
            balancer_sec, backend_sec, fallback_sec = server_sec(transport_sec)
            skip_langs = []
            if transport_sec == "tls":
                skip_langs += ["java"]
            if balancer_short_stream:
                skip_langs += ["java"]
            config = {
                "name": "client_referred_to_backend_multiple_backends_%s_short_stream_%s"
                % (transport_sec, balancer_short_stream),
                "skip_langs": skip_langs,
                "transport_sec": transport_sec,
                "balancer_configs": [
                    {
                        "transport_sec": balancer_sec,
                        "short_stream": balancer_short_stream,
                    }
                ],
                "backend_configs": [
                    {
                        "transport_sec": backend_sec,
                    },
                    {
                        "transport_sec": backend_sec,
                    },
                    {
                        "transport_sec": backend_sec,
                    },
                    {
                        "transport_sec": backend_sec,
                    },
                    {
                        "transport_sec": backend_sec,
                    },
                ],
                "fallback_configs": [],
                "cause_no_error_no_data_for_balancer_a_record": False,
            }
            all_configs.append(config)
    return all_configs


all_scenarios += generate_client_referred_to_backend_multiple_backends()


def generate_client_falls_back_because_no_backends():
    all_configs = []
    for balancer_short_stream in [True, False]:
        for transport_sec in [
            "insecure",
            "alts",
            "tls",
            "google_default_credentials",
        ]:
            balancer_sec, backend_sec, fallback_sec = server_sec(transport_sec)
            skip_langs = ["go", "java"]
            if transport_sec == "tls":
                skip_langs += ["java"]
            if balancer_short_stream:
                skip_langs += ["java"]
            config = {
                "name": (
                    "client_falls_back_because_no_backends_%s_short_stream_%s"
                )
                % (transport_sec, balancer_short_stream),
                "skip_langs": skip_langs,
                "transport_sec": transport_sec,
                "balancer_configs": [
                    {
                        "transport_sec": balancer_sec,
                        "short_stream": balancer_short_stream,
                    }
                ],
                "backend_configs": [],
                "fallback_configs": [
                    {
                        "transport_sec": fallback_sec,
                    }
                ],
                "cause_no_error_no_data_for_balancer_a_record": False,
            }
            all_configs.append(config)
    return all_configs


all_scenarios += generate_client_falls_back_because_no_backends()


def generate_client_falls_back_because_balancer_connection_broken():
    all_configs = []
    for transport_sec in ["alts", "tls", "google_default_credentials"]:
        balancer_sec, backend_sec, fallback_sec = server_sec(transport_sec)
        skip_langs = []
        if transport_sec == "tls":
            skip_langs = ["java"]
        config = {
            "name": "client_falls_back_because_balancer_connection_broken_%s"
            % transport_sec,
            "skip_langs": skip_langs,
            "transport_sec": transport_sec,
            "balancer_configs": [
                {
                    "transport_sec": "insecure",
                    "short_stream": False,
                }
            ],
            "backend_configs": [],
            "fallback_configs": [
                {
                    "transport_sec": fallback_sec,
                }
            ],
            "cause_no_error_no_data_for_balancer_a_record": False,
        }
        all_configs.append(config)
    return all_configs


all_scenarios += generate_client_falls_back_because_balancer_connection_broken()


def generate_client_referred_to_backend_multiple_balancers():
    all_configs = []
    for balancer_short_stream in [True, False]:
        for transport_sec in [
            "insecure",
            "alts",
            "tls",
            "google_default_credentials",
        ]:
            balancer_sec, backend_sec, fallback_sec = server_sec(transport_sec)
            skip_langs = []
            if transport_sec == "tls":
                skip_langs += ["java"]
            if balancer_short_stream:
                skip_langs += ["java"]
            config = {
                "name": "client_referred_to_backend_multiple_balancers_%s_short_stream_%s"
                % (transport_sec, balancer_short_stream),
                "skip_langs": skip_langs,
                "transport_sec": transport_sec,
                "balancer_configs": [
                    {
                        "transport_sec": balancer_sec,
                        "short_stream": balancer_short_stream,
                    },
                    {
                        "transport_sec": balancer_sec,
                        "short_stream": balancer_short_stream,
                    },
                    {
                        "transport_sec": balancer_sec,
                        "short_stream": balancer_short_stream,
                    },
                    {
                        "transport_sec": balancer_sec,
                        "short_stream": balancer_short_stream,
                    },
                    {
                        "transport_sec": balancer_sec,
                        "short_stream": balancer_short_stream,
                    },
                ],
                "backend_configs": [
                    {
                        "transport_sec": backend_sec,
                    },
                ],
                "fallback_configs": [],
                "cause_no_error_no_data_for_balancer_a_record": False,
            }
            all_configs.append(config)
    return all_configs


all_scenarios += generate_client_referred_to_backend_multiple_balancers()

print(
    (
        yaml.dump(
            {
                "lb_interop_test_scenarios": all_scenarios,
            }
        )
    )
)
