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
"""Starts a local DNS server for use in tests"""

import argparse
import os
import platform
import signal
import sys
import threading
import time

import twisted
import twisted.internet
import twisted.internet.defer
import twisted.internet.protocol
import twisted.internet.reactor
import twisted.internet.threads
import twisted.names
from twisted.names import authority
from twisted.names import client
from twisted.names import common
from twisted.names import dns
from twisted.names import server
import twisted.names.client
import twisted.names.dns
import twisted.names.server
import yaml

_SERVER_HEALTH_CHECK_RECORD_NAME = (  # missing end '.' for twisted syntax
    "health-check-local-dns-server-is-alive.resolver-tests.grpctestingexp"
)
_SERVER_HEALTH_CHECK_RECORD_DATA = "123.123.123.123"


class NoFileAuthority(authority.FileAuthority):
    def __init__(self, soa, records):
        # skip FileAuthority
        common.ResolverBase.__init__(self)
        self.soa = soa
        self.records = records


def start_local_dns_server(args):
    all_records = {}

    def _push_record(name, r):
        name = name.encode("ascii")
        print("pushing record: |%s|" % name)
        if all_records.get(name) is not None:
            all_records[name].append(r)
            return
        all_records[name] = [r]

    def _maybe_split_up_txt_data(name, txt_data, r_ttl):
        txt_data = txt_data.encode("ascii")
        start = 0
        txt_data_list = []
        while len(txt_data[start:]) > 0:
            next_read = len(txt_data[start:])
            if next_read > 255:
                next_read = 255
            txt_data_list.append(txt_data[start : start + next_read])
            start += next_read
        _push_record(name, dns.Record_TXT(*txt_data_list, ttl=r_ttl))

    with open(args.records_config_path) as config:
        test_records_config = yaml.safe_load(config)
    common_zone_name = test_records_config["resolver_tests_common_zone_name"]
    for group in test_records_config["resolver_component_tests"]:
        for name in group["records"].keys():
            for record in group["records"][name]:
                r_type = record["type"]
                r_data = record["data"]
                r_ttl = int(record["TTL"])
                record_full_name = "%s.%s" % (name, common_zone_name)
                assert record_full_name[-1] == "."
                record_full_name = record_full_name[:-1]
                if r_type == "A":
                    _push_record(
                        record_full_name, dns.Record_A(r_data, ttl=r_ttl)
                    )
                if r_type == "AAAA":
                    _push_record(
                        record_full_name, dns.Record_AAAA(r_data, ttl=r_ttl)
                    )
                if r_type == "SRV":
                    p, w, port, target = r_data.split(" ")
                    p = int(p)
                    w = int(w)
                    port = int(port)
                    target_full_name = (
                        "%s.%s" % (target, common_zone_name)
                    ).encode("ascii")
                    _push_record(
                        record_full_name,
                        dns.Record_SRV(p, w, port, target_full_name, ttl=r_ttl),
                    )
                if r_type == "TXT":
                    _maybe_split_up_txt_data(record_full_name, r_data, r_ttl)
    # Add an optional IPv4 record is specified
    if args.add_a_record:
        extra_host, extra_host_ipv4 = args.add_a_record.split(":")
        _push_record(extra_host, dns.Record_A(extra_host_ipv4, ttl=0))
    # Server health check record
    _push_record(
        _SERVER_HEALTH_CHECK_RECORD_NAME,
        dns.Record_A(_SERVER_HEALTH_CHECK_RECORD_DATA, ttl=0),
    )
    soa_record = dns.Record_SOA(mname=common_zone_name.encode("ascii"))
    test_domain_com = NoFileAuthority(
        soa=(common_zone_name.encode("ascii"), soa_record),
        records=all_records,
    )
    server = twisted.names.server.DNSServerFactory(
        authorities=[test_domain_com], verbose=2
    )
    server.noisy = 2
    twisted.internet.reactor.listenTCP(args.port, server)
    dns_proto = twisted.names.dns.DNSDatagramProtocol(server)
    dns_proto.noisy = 2
    twisted.internet.reactor.listenUDP(args.port, dns_proto)
    print("starting local dns server on 127.0.0.1:%s" % args.port)
    print("starting twisted.internet.reactor")
    twisted.internet.reactor.suggestThreadPoolSize(1)
    twisted.internet.reactor.run()


def _quit_on_signal(signum, _frame):
    print("Received SIGNAL %d. Quitting with exit code 0" % signum)
    twisted.internet.reactor.stop()
    sys.stdout.flush()
    sys.exit(0)


def flush_stdout_loop():
    num_timeouts_so_far = 0
    sleep_time = 1
    # Prevent zombies. Tests that use this server are short-lived.
    max_timeouts = 60 * 10
    while num_timeouts_so_far < max_timeouts:
        sys.stdout.flush()
        time.sleep(sleep_time)
        num_timeouts_so_far += 1
    print("Process timeout reached, or cancelled. Exitting 0.")
    os.kill(os.getpid(), signal.SIGTERM)


def main():
    argp = argparse.ArgumentParser(
        description="Local DNS Server for resolver tests"
    )
    argp.add_argument(
        "-p",
        "--port",
        default=None,
        type=int,
        help="Port for DNS server to listen on for TCP and UDP.",
    )
    argp.add_argument(
        "-r",
        "--records_config_path",
        default=None,
        type=str,
        help=(
            "Directory of resolver_test_record_groups.yaml file. "
            "Defaults to path needed when the test is invoked as part "
            "of run_tests.py."
        ),
    )
    argp.add_argument(
        "--add_a_record",
        default=None,
        type=str,
        help=(
            "Add an A record via the command line. Useful for when we "
            "need to serve a one-off A record that is under a "
            "different domain then the rest the records configured in "
            "--records_config_path (which all need to be under the "
            "same domain). Format: <name>:<ipv4 address>"
        ),
    )
    args = argp.parse_args()
    signal.signal(signal.SIGTERM, _quit_on_signal)
    signal.signal(signal.SIGINT, _quit_on_signal)
    output_flush_thread = threading.Thread(target=flush_stdout_loop)
    output_flush_thread.setDaemon(True)
    output_flush_thread.start()
    start_local_dns_server(args)


if __name__ == "__main__":
    main()
