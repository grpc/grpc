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

    out['libs'] = [{
        'name':
            'ares',
        'defaults':
            'ares',
        'build':
            'private',
        'language':
            'c',
        'secure':
            False,
        'src': [
            "third_party/cares/cares/src/lib/ares__read_line.c",
            "third_party/cares/cares/src/lib/ares__get_hostent.c",
            "third_party/cares/cares/src/lib/ares__close_sockets.c",
            "third_party/cares/cares/src/lib/ares__timeval.c",
            "third_party/cares/cares/src/lib/ares_gethostbyaddr.c",
            "third_party/cares/cares/src/lib/ares_getenv.c",
            "third_party/cares/cares/src/lib/ares_free_string.c",
            "third_party/cares/cares/src/lib/ares_free_hostent.c",
            "third_party/cares/cares/src/lib/ares_fds.c",
            "third_party/cares/cares/src/lib/ares_expand_string.c",
            "third_party/cares/cares/src/lib/ares_create_query.c",
            "third_party/cares/cares/src/lib/ares_cancel.c",
            "third_party/cares/cares/src/lib/ares_android.c",
            "third_party/cares/cares/src/lib/ares_parse_txt_reply.c",
            "third_party/cares/cares/src/lib/ares_parse_srv_reply.c",
            "third_party/cares/cares/src/lib/ares_parse_soa_reply.c",
            "third_party/cares/cares/src/lib/ares_parse_ptr_reply.c",
            "third_party/cares/cares/src/lib/ares_parse_ns_reply.c",
            "third_party/cares/cares/src/lib/ares_parse_naptr_reply.c",
            "third_party/cares/cares/src/lib/ares_parse_mx_reply.c",
            "third_party/cares/cares/src/lib/ares_parse_caa_reply.c",
            "third_party/cares/cares/src/lib/ares_options.c",
            "third_party/cares/cares/src/lib/ares_nowarn.c",
            "third_party/cares/cares/src/lib/ares_mkquery.c",
            "third_party/cares/cares/src/lib/ares_llist.c",
            "third_party/cares/cares/src/lib/ares_getsock.c",
            "third_party/cares/cares/src/lib/ares_getnameinfo.c",
            "third_party/cares/cares/src/lib/bitncmp.c",
            "third_party/cares/cares/src/lib/ares_writev.c",
            "third_party/cares/cares/src/lib/ares_version.c",
            "third_party/cares/cares/src/lib/ares_timeout.c",
            "third_party/cares/cares/src/lib/ares_strerror.c",
            "third_party/cares/cares/src/lib/ares_strcasecmp.c",
            "third_party/cares/cares/src/lib/ares_search.c",
            "third_party/cares/cares/src/lib/ares_platform.c",
            "third_party/cares/cares/src/lib/windows_port.c",
            "third_party/cares/cares/src/lib/inet_ntop.c",
            "third_party/cares/cares/src/lib/ares__sortaddrinfo.c",
            "third_party/cares/cares/src/lib/ares__readaddrinfo.c",
            "third_party/cares/cares/src/lib/ares_parse_uri_reply.c",
            "third_party/cares/cares/src/lib/ares__parse_into_addrinfo.c",
            "third_party/cares/cares/src/lib/ares_parse_a_reply.c",
            "third_party/cares/cares/src/lib/ares_parse_aaaa_reply.c",
            "third_party/cares/cares/src/lib/ares_library_init.c",
            "third_party/cares/cares/src/lib/ares_init.c",
            "third_party/cares/cares/src/lib/ares_gethostbyname.c",
            "third_party/cares/cares/src/lib/ares_getaddrinfo.c",
            "third_party/cares/cares/src/lib/ares_freeaddrinfo.c",
            "third_party/cares/cares/src/lib/ares_expand_name.c",
            "third_party/cares/cares/src/lib/ares_destroy.c",
        ],
        'headers': [
            "third_party/cares/ares_build.h",
            "third_party/cares/cares/ares_config.h",
            "third_party/cares/cares/include/ares_version.h",
            "third_party/cares/cares/include/ares.h",
            "third_party/cares/cares/include/ares_rules.h",
            "third_party/cares/cares/include/ares_dns.h",
            "third_party/cares/cares/include/ares_nameser.h",
            "third_party/cares/cares/src/tools/ares_getopt.h",
            "third_party/cares/cares/src/lib/ares_strsplit.h",
            "third_party/cares/cares/src/lib/ares_android.h",
            "third_party/cares/cares/src/lib/ares_private.h",
            "third_party/cares/cares/src/lib/ares_llist.h",
            "third_party/cares/cares/src/lib/ares_platform.h",
            "third_party/cares/cares/src/lib/ares_ipv6.h",
            "third_party/cares/cares/src/lib/config-dos.h",
            "third_party/cares/cares/src/lib/bitncmp.h",
            "third_party/cares/cares/src/lib/ares_strcasecmp.h",
            "third_party/cares/cares/src/lib/setup_once.h",
            "third_party/cares/cares/src/lib/ares_inet_net_pton.h",
            "third_party/cares/cares/src/lib/ares_data.h",
            "third_party/cares/cares/src/lib/ares_getenv.h",
            "third_party/cares/cares/src/lib/config-win32.h",
            "third_party/cares/cares/src/lib/ares_strdup.h",
            "third_party/cares/cares/src/lib/ares_iphlpapi.h",
            "third_party/cares/cares/src/lib/ares_setup.h",
            "third_party/cares/cares/src/lib/ares_writev.h",
            "third_party/cares/cares/src/lib/ares_nowarn.h",
        ],
    }]
except:
    pass

print(yaml.dump(out))
