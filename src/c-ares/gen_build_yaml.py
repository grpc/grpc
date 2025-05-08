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

import re
import os
import sys
import yaml

os.chdir(os.path.dirname(sys.argv[0]) + "/../..")

out = {}

try:

    def gen_ares_build(x):
        subprocess.call("third_party/cares/cares/buildconf", shell=True)
        subprocess.call("third_party/cares/cares/configure", shell=True)

    def config_platform(x):
        if "darwin" in sys.platform:
            return "src/cares/cares/config_darwin/ares_config.h"
        if "freebsd" in sys.platform:
            return "src/cares/cares/config_freebsd/ares_config.h"
        if "linux" in sys.platform:
            return "src/cares/cares/config_linux/ares_config.h"
        if "openbsd" in sys.platform:
            return "src/cares/cares/config_openbsd/ares_config.h"
        if not os.path.isfile("third_party/cares/cares/ares_config.h"):
            gen_ares_build(x)
        return "third_party/cares/cares/ares_config.h"

    def ares_build(x):
        if os.path.isfile("src/cares/cares/ares_build.h"):
            return "src/cares/cares/ares_build.h"
        if not os.path.isfile("third_party/cares/cares/include/ares_build.h"):
            gen_ares_build(x)
        return "third_party/cares/cares/include/ares_build.h"

    out["libs"] = [
        {
            "name": "cares",
            "defaults": "cares",
            "build": "private",
            "language": "c",
            "secure": False,
            "src": [
                "third_party/cares/cares/src/lib/inet_ntop.c",
                "third_party/cares/cares/src/lib/event/ares_event_configchg.c",
                "third_party/cares/cares/src/lib/event/ares_event_poll.c",
                "third_party/cares/cares/src/lib/event/ares_event_epoll.c",
                "third_party/cares/cares/src/lib/event/ares_event_wake_pipe.c",
                "third_party/cares/cares/src/lib/event/ares_event_select.c",
                "third_party/cares/cares/src/lib/event/ares_event_kqueue.c",
                "third_party/cares/cares/src/lib/event/ares_event_win32.c",
                "third_party/cares/cares/src/lib/event/ares_event_thread.c",
                "third_party/cares/cares/src/lib/ares_gethostbyaddr.c",
                "third_party/cares/cares/src/lib/ares_set_socket_functions.c",
                "third_party/cares/cares/src/lib/ares_options.c",
                "third_party/cares/cares/src/lib/ares_getaddrinfo.c",
                "third_party/cares/cares/src/lib/ares_init.c",
                "third_party/cares/cares/src/lib/ares_android.c",
                "third_party/cares/cares/src/lib/ares_destroy.c",
                "third_party/cares/cares/src/lib/ares_getenv.c",
                "third_party/cares/cares/src/lib/ares_addrinfo_localhost.c",
                "third_party/cares/cares/src/lib/ares_send.c",
                "third_party/cares/cares/src/lib/ares_free_hostent.c",
                "third_party/cares/cares/src/lib/ares_data.c",
                "third_party/cares/cares/src/lib/ares_parse_into_addrinfo.c",
                "third_party/cares/cares/src/lib/ares_cancel.c",
                "third_party/cares/cares/src/lib/ares_library_init.c",
                "third_party/cares/cares/src/lib/ares_search.c",
                "third_party/cares/cares/src/lib/ares_sysconfig_win.c",
                "third_party/cares/cares/src/lib/ares_gethostbyname.c",
                "third_party/cares/cares/src/lib/ares_addrinfo2hostent.c",
                "third_party/cares/cares/src/lib/ares_freeaddrinfo.c",
                "third_party/cares/cares/src/lib/ares_sysconfig.c",
                "third_party/cares/cares/src/lib/ares_hosts_file.c",
                "third_party/cares/cares/src/lib/ares_sortaddrinfo.c",
                "third_party/cares/cares/src/lib/ares_free_string.c",
                "third_party/cares/cares/src/lib/ares_qcache.c",
                "third_party/cares/cares/src/lib/str/ares_str.c",
                "third_party/cares/cares/src/lib/str/ares_strsplit.c",
                "third_party/cares/cares/src/lib/str/ares_buf.c",
                "third_party/cares/cares/src/lib/windows_port.c",
                "third_party/cares/cares/src/lib/ares_socket.c",
                "third_party/cares/cares/src/lib/ares_timeout.c",
                "third_party/cares/cares/src/lib/ares_cookie.c",
                "third_party/cares/cares/src/lib/dsa/ares_htable_vpstr.c",
                "third_party/cares/cares/src/lib/dsa/ares_htable_strvp.c",
                "third_party/cares/cares/src/lib/dsa/ares_htable_dict.c",
                "third_party/cares/cares/src/lib/dsa/ares_htable_szvp.c",
                "third_party/cares/cares/src/lib/dsa/ares_htable_vpvp.c",
                "third_party/cares/cares/src/lib/dsa/ares_llist.c",
                "third_party/cares/cares/src/lib/dsa/ares_htable.c",
                "third_party/cares/cares/src/lib/dsa/ares_htable_asvp.c",
                "third_party/cares/cares/src/lib/dsa/ares_array.c",
                "third_party/cares/cares/src/lib/dsa/ares_slist.c",
            ],
            "headers": [
                "third_party/cares/ares_build.h",
                "third_party/cares/config_darwin/ares_config.h",
                "third_party/cares/config_freebsd/ares_config.h",
                "third_party/cares/config_linux/ares_config.h",
                "third_party/cares/config_openbsd/ares_config.h",
                "third_party/cares/cares/include/ares_nameser.h",
                "third_party/cares/cares/include/ares_dns_record.h",
                "third_party/cares/cares/include/ares.h",
                "third_party/cares/cares/include/ares_version.h",
                "third_party/cares/cares/include/ares_dns.h",
                "third_party/cares/cares/src/lib/event/ares_event_win32.h",
                "third_party/cares/cares/src/lib/event/ares_event.h",
                "third_party/cares/cares/src/lib/include/ares_htable_asvp.h",
                "third_party/cares/cares/src/lib/include/ares_llist.h",
                "third_party/cares/cares/src/lib/include/ares_htable_vpstr.h",
                "third_party/cares/cares/src/lib/include/ares_htable_strvp.h",
                "third_party/cares/cares/src/lib/include/ares_htable_vpvp.h",
                "third_party/cares/cares/src/lib/include/ares_htable_szvp.h",
                "third_party/cares/cares/src/lib/include/ares_mem.h",
                "third_party/cares/cares/src/lib/include/ares_htable_dict.h",
                "third_party/cares/cares/src/lib/include/ares_array.h",
                "third_party/cares/cares/src/lib/include/ares_buf.h",
                "third_party/cares/cares/src/lib/include/ares_str.h",
                "third_party/cares/cares/src/lib/ares_android.h",
                "third_party/cares/cares/src/lib/ares_private.h",
                "third_party/cares/cares/src/lib/ares_socket.h",
                "third_party/cares/cares/src/lib/ares_ipv6.h",
                "third_party/cares/cares/src/lib/config-dos.h",
                "third_party/cares/cares/src/lib/ares_inet_net_pton.h",
                "third_party/cares/cares/src/lib/str/ares_strsplit.h",
                "third_party/cares/cares/src/lib/dsa/ares_slist.h",
                "third_party/cares/cares/src/lib/dsa/ares_htable.h",
                "third_party/cares/cares/src/lib/ares_data.h",
                "third_party/cares/cares/src/lib/ares_getenv.h",
                "third_party/cares/cares/src/lib/config-win32.h",
                "third_party/cares/cares/src/lib/ares_conn.h",
                "third_party/cares/cares/src/lib/ares_setup.h",
                "third_party/cares/cares/src/lib/thirdparty/apple/dnsinfo.h",
                "third_party/cares/cares/src/lib/util/ares_threads.h",
                "third_party/cares/cares/src/lib/util/ares_time.h",
                "third_party/cares/cares/src/lib/util/ares_iface_ips.h",
                "third_party/cares/cares/src/lib/util/ares_math.h",
                "third_party/cares/cares/src/lib/util/ares_rand.h",
                "third_party/cares/cares/src/lib/util/ares_uri.h",
                "third_party/cares/cares/src/lib/record/ares_dns_multistring.h",
                "third_party/cares/cares/src/lib/record/ares_dns_private.h",
            ],
        }
    ]
except:
    pass

print(yaml.dump(out))
