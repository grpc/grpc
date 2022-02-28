#!/usr/bin/env python3

# Copyright 2022 gRPC authors.
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

# Generator script for src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h
# Should be executed from grpc's root directory.

from __future__ import print_function
from dataclasses import dataclass

import collections
import sys

# import perfection


@dataclass
class DataMember:
    name: str
    type: str
    default_initializer: str = ''
    getter_comment: str = ''
    special_getter_return_type: str = ''
    override_getter: str = ''
    setter_comment: str = ''
    setter_move_semantics: bool = False
    special_comparator: str = ''


_DATA_MEMBERS = [
    DataMember(name='cert_request_type',
               type='grpc_ssl_client_certificate_request_type',
               default_initializer='GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE'),
    DataMember(name='verify_server_cert',
               type='bool',
               default_initializer='true'),
    DataMember(name='min_tls_version',
               type='grpc_tls_version',
               default_initializer='grpc_tls_version::TLS1_2'),
    DataMember(name='max_tls_version',
               type='grpc_tls_version',
               default_initializer='grpc_tls_version::TLS1_3'),
    DataMember(
        name='certificate_verifier',
        type='grpc_core::RefCountedPtr<grpc_tls_certificate_verifier>',
        override_getter="""grpc_tls_certificate_verifier* certificate_verifier() {
    return certificate_verifier_.get();
  }""",
        setter_move_semantics=True,
        special_comparator=
        '(certificate_verifier_ == other.certificate_verifier_ || (certificate_verifier_ != nullptr && other.certificate_verifier_ != nullptr && certificate_verifier_->Compare(other.certificate_verifier_.get()) == 0))'
    ),
    DataMember(name='check_call_host', type='bool', default_initializer='true'),
    DataMember(
        name='certificate_provider',
        type='grpc_core::RefCountedPtr<grpc_tls_certificate_provider>',
        getter_comment=
        'Returns the distributor from certificate_provider_ if it is set, nullptr otherwise.',
        override_getter=
        """grpc_tls_certificate_distributor* certificate_distributor() {
    if (certificate_provider_ != nullptr) return certificate_provider_->distributor().get();
    return nullptr;
  }""",
        setter_move_semantics=True,
        special_comparator=
        '(certificate_provider_ == other.certificate_provider_ || (certificate_provider_ != nullptr && other.certificate_provider_ != nullptr && certificate_provider_->cmp(other.certificate_provider_.get()) == 0))'
    ),
    DataMember(
        name='watch_root_cert',
        type='bool',
        default_initializer='false',
        setter_comment=
        'If need to watch the updates of root certificates with name |root_cert_name|. The default value is false. If used in tls_credentials, it should always be set to true unless the root certificates are not needed.'
    ),
    DataMember(
        name='root_cert_name',
        type='std::string',
        special_getter_return_type='const std::string&',
        setter_comment=
        'Sets the name of root certificates being watched, if |set_watch_root_cert| is called. If not set, an empty string will be used as the name.',
        setter_move_semantics=True),
    DataMember(
        name='watch_identity_pair',
        type='bool',
        default_initializer='false',
        setter_comment=
        'If need to watch the updates of identity certificates with name |identity_cert_name|. The default value is false. If used in tls_credentials, it should always be set to true unless the identity key-cert pairs are not needed.'
    ),
    DataMember(
        name='identity_cert_name',
        type='std::string',
        special_getter_return_type='const std::string&',
        setter_comment=
        'Sets the name of identity key-cert pairs being watched, if |set_watch_identity_pair| is called. If not set, an empty string will be used as the name.',
        setter_move_semantics=True),
    DataMember(name='tls_session_key_log_file_path',
               type='std::string',
               special_getter_return_type='const std::string&',
               setter_move_semantics=True),
    DataMember(
        name='crl_directory',
        type='std::string',
        special_getter_return_type='const std::string&',
        setter_comment=
        ' gRPC will enforce CRLs on all handshakes from all hashed CRL files inside of the crl_directory. If not set, an empty string will be used, which will not enable CRL checking. Only supported for OpenSSL version > 1.1.',
        setter_move_semantics=True)
]


# utility: print a comment in a file
def put_comment(f, comment):
    print('//' + comment, file=f)


# print copyright notice from this file
def put_copyright(f, year):
    print("""//
//
// Copyright %s gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//
""" % (year),
          file=f)


H = open('src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h',
         'w')
put_copyright(H, '2018')
put_comment(
    H, 'Generated by tools/codegen/core/gen_grpc_tls_credentials_options.py\n')
print(
    """#ifndef GRPC_CORE_LIB_SECURITY_CREDENTIALS_TLS_GRPC_TLS_CREDENTIALS_OPTIONS_H
#define GRPC_CORE_LIB_SECURITY_CREDENTIALS_TLS_GRPC_TLS_CREDENTIALS_OPTIONS_H

#include <grpc/support/port_platform.h>

#include \"absl/container/inlined_vector.h\"

#include <grpc/grpc_security.h>

#include \"src/core/lib/gprpp/ref_counted.h"
#include \"src/core/lib/security/credentials/tls/grpc_tls_certificate_distributor.h\"
#include \"src/core/lib/security/credentials/tls/grpc_tls_certificate_provider.h\"
#include \"src/core/lib/security/credentials/tls/grpc_tls_certificate_verifier.h\"
#include \"src/core/lib/security/security_connector/ssl_utils.h\"

// Contains configurable options specified by callers to configure their certain
// security features supported in TLS.
// TODO(ZhenLian): consider making this not ref-counted.
struct grpc_tls_credentials_options
    : public grpc_core::RefCounted<grpc_tls_credentials_options> {
 public:
  ~grpc_tls_credentials_options() override = default;
""",
    file=H)

# Print out getters for all data members
put_comment(H, " Getters for member fields.")
for data_member in _DATA_MEMBERS:
    if data_member.getter_comment != '':
        put_comment(H, data_member.getter_comment)
    if data_member.override_getter:
        print("  " + data_member.override_getter, file=H)
    else:
        print(
            "  %s %s() const { return %s; }" %
            (data_member.special_getter_return_type if
             data_member.special_getter_return_type != '' else data_member.type,
             data_member.name, data_member.name + '_'),
            file=H)

# Print out setters for all data members
print("", file=H)
put_comment(H, "Setters for member fields.")
for data_member in _DATA_MEMBERS:
    if data_member.setter_comment != '':
        put_comment(H, data_member.setter_comment)
    if (data_member.setter_move_semantics):
        print("  void set_%s(%s %s) { %s_ = std::move(%s); }" %
              (data_member.name, data_member.type, data_member.name,
               data_member.name, data_member.name),
              file=H)
    else:
        print("  void set_%s(%s %s) { %s_ = %s; }" %
              (data_member.name, data_member.type, data_member.name,
               data_member.name, data_member.name),
              file=H)

# Write out operator==
print(
    "  bool operator==(const grpc_tls_credentials_options& other) const {\n  return ",
    file=H)
for i in range(len(_DATA_MEMBERS)):
    if (_DATA_MEMBERS[i].special_comparator != ''):
        print(_DATA_MEMBERS[i].special_comparator, file=H)
    else:
        print('%s_ == other.%s_' %
              (_DATA_MEMBERS[i].name, _DATA_MEMBERS[i].name),
              file=H)
    if (i != len(_DATA_MEMBERS) - 1):
        print('&&', file=H)
print(";\n  }", file=H)

#Print out data member declarations
print("\n private:", file=H)
for data_member in _DATA_MEMBERS:
    if data_member.default_initializer == '':
        print("  %s %s_;" % (
            data_member.type,
            data_member.name,
        ), file=H)
    else:
        print("  %s %s_ = %s;" % (data_member.type, data_member.name,
                                  data_member.default_initializer),
              file=H)

# Print out file ending
print("""};

#endif  // GRPC_CORE_LIB_SECURITY_CREDENTIALS_TLS_GRPC_TLS_CREDENTIALS_OPTIONS_H""",
      file=H)

H.close()
