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

# Generator script for src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h and test/core/security/grpc_tls_credentials_options_comparator_test.cc
# Should be executed from grpc's root directory.

from __future__ import print_function

import collections
from dataclasses import dataclass
import difflib
import filecmp
import os
import sys
import tempfile


@dataclass
class DataMember:
    name: str  # name of the data member without the trailing '_'
    type: str  # Type (eg. std::string, bool)
    test_name: str  # The name to use for the associated test
    test_value_1: str  # Test-specific value to use for comparison
    test_value_2: str  # Test-specific value (different from test_value_1)
    default_initializer: str = ''  # If non-empty, this will be used as the default initialization of this field
    getter_comment: str = ''  # Comment to add before the getter for this field
    special_getter_return_type: str = ''  # Override for the return type of getter (eg. const std::string&)
    override_getter: str = ''  # Override for the entire getter method. Relevant for certificate_verifier and certificate_provider
    setter_comment: str = ''  # Commend to add before the setter for this field
    setter_move_semantics: bool = False  # Should the setter use move-semantics
    special_comparator: str = ''  # If non-empty, this will be used in `operator==`


_DATA_MEMBERS = [
    DataMember(name='cert_request_type',
               type='grpc_ssl_client_certificate_request_type',
               default_initializer='GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE',
               test_name="DifferentCertRequestType",
               test_value_1="GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE",
               test_value_2="GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY"),
    DataMember(name='verify_server_cert',
               type='bool',
               default_initializer='true',
               test_name="DifferentVerifyServerCert",
               test_value_1="false",
               test_value_2="true"),
    DataMember(name='min_tls_version',
               type='grpc_tls_version',
               default_initializer='grpc_tls_version::TLS1_2',
               test_name="DifferentMinTlsVersion",
               test_value_1="grpc_tls_version::TLS1_2",
               test_value_2="grpc_tls_version::TLS1_3"),
    DataMember(name='max_tls_version',
               type='grpc_tls_version',
               default_initializer='grpc_tls_version::TLS1_3',
               test_name="DifferentMaxTlsVersion",
               test_value_1="grpc_tls_version::TLS1_2",
               test_value_2="grpc_tls_version::TLS1_3"),
    DataMember(
        name='certificate_verifier',
        type='grpc_core::RefCountedPtr<grpc_tls_certificate_verifier>',
        override_getter="""grpc_tls_certificate_verifier* certificate_verifier() {
    return certificate_verifier_.get();
  }""",
        setter_move_semantics=True,
        special_comparator=
        '(certificate_verifier_ == other.certificate_verifier_ || (certificate_verifier_ != nullptr && other.certificate_verifier_ != nullptr && certificate_verifier_->Compare(other.certificate_verifier_.get()) == 0))',
        test_name="DifferentCertificateVerifier",
        test_value_1="MakeRefCounted<HostNameCertificateVerifier>()",
        test_value_2="MakeRefCounted<XdsCertificateVerifier>(nullptr, \"\")"),
    DataMember(name='check_call_host',
               type='bool',
               default_initializer='true',
               test_name="DifferentCheckCallHost",
               test_value_1="false",
               test_value_2="true"),
    DataMember(
        name='certificate_provider',
        type='grpc_core::RefCountedPtr<grpc_tls_certificate_provider>',
        getter_comment=
        'Returns the distributor from certificate_provider_ if it is set, nullptr otherwise.',
        override_getter=
        """grpc_tls_certificate_distributor* certificate_distributor() {
    if (certificate_provider_ != nullptr) { return certificate_provider_->distributor().get(); }
    return nullptr;
  }""",
        setter_move_semantics=True,
        special_comparator=
        '(certificate_provider_ == other.certificate_provider_ || (certificate_provider_ != nullptr && other.certificate_provider_ != nullptr && certificate_provider_->Compare(other.certificate_provider_.get()) == 0))',
        test_name="DifferentCertificateProvider",
        test_value_1=
        "MakeRefCounted<StaticDataCertificateProvider>(\"root_cert_1\", PemKeyCertPairList())",
        test_value_2=
        "MakeRefCounted<StaticDataCertificateProvider>(\"root_cert_2\", PemKeyCertPairList())"
    ),
    DataMember(
        name='watch_root_cert',
        type='bool',
        default_initializer='false',
        setter_comment=
        'If need to watch the updates of root certificates with name |root_cert_name|. The default value is false. If used in tls_credentials, it should always be set to true unless the root certificates are not needed.',
        test_name="DifferentWatchRootCert",
        test_value_1="false",
        test_value_2="true"),
    DataMember(
        name='root_cert_name',
        type='std::string',
        special_getter_return_type='const std::string&',
        setter_comment=
        'Sets the name of root certificates being watched, if |set_watch_root_cert| is called. If not set, an empty string will be used as the name.',
        setter_move_semantics=True,
        test_name="DifferentRootCertName",
        test_value_1="\"root_cert_name_1\"",
        test_value_2="\"root_cert_name_2\""),
    DataMember(
        name='watch_identity_pair',
        type='bool',
        default_initializer='false',
        setter_comment=
        'If need to watch the updates of identity certificates with name |identity_cert_name|. The default value is false. If used in tls_credentials, it should always be set to true unless the identity key-cert pairs are not needed.',
        test_name="DifferentWatchIdentityPair",
        test_value_1="false",
        test_value_2="true"),
    DataMember(
        name='identity_cert_name',
        type='std::string',
        special_getter_return_type='const std::string&',
        setter_comment=
        'Sets the name of identity key-cert pairs being watched, if |set_watch_identity_pair| is called. If not set, an empty string will be used as the name.',
        setter_move_semantics=True,
        test_name="DifferentIdentityCertName",
        test_value_1="\"identity_cert_name_1\"",
        test_value_2="\"identity_cert_name_2\""),
    DataMember(name='tls_session_key_log_file_path',
               type='std::string',
               special_getter_return_type='const std::string&',
               setter_move_semantics=True,
               test_name="DifferentTlsSessionKeyLogFilePath",
               test_value_1="\"file_path_1\"",
               test_value_2="\"file_path_2\""),
    DataMember(
        name='crl_directory',
        type='std::string',
        special_getter_return_type='const std::string&',
        setter_comment=
        ' gRPC will enforce CRLs on all handshakes from all hashed CRL files inside of the crl_directory. If not set, an empty string will be used, which will not enable CRL checking. Only supported for OpenSSL version > 1.1.',
        setter_move_semantics=True,
        test_name="DifferentCrlDirectory",
        test_value_1="\"crl_directory_1\"",
        test_value_2="\"crl_directory_2\"")
    DataMember(
        name="send_client_ca_list",
        type="bool",
        default_initializer="false",
        test_name="DifferentSendClientCaListValues",
        test_value_1="false",
        test_value_2="true",
    ),
]


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


# Prints differences between two files
def get_file_differences(file1, file2):
    with open(file1) as f1:
        file1_text = f1.readlines()
    with open(file2) as f2:
        file2_text = f2.readlines()
    return difflib.unified_diff(file1_text,
                                file2_text,
                                fromfile=file1,
                                tofile=file2)


# Is this script executed in test mode?
test_mode = False
if len(sys.argv) > 1 and sys.argv[1] == "--test":
    test_mode = True

HEADER_FILE_NAME = 'src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h'
# Generate src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h
header_file_name = HEADER_FILE_NAME
if (test_mode):
    header_file_name = tempfile.NamedTemporaryFile(delete=False).name
H = open(header_file_name, 'w')

put_copyright(H, '2018')
print(
    '// Generated by tools/codegen/core/gen_grpc_tls_credentials_options.py\n',
    file=H)
print(
    """#ifndef GRPC_SRC_CORE_LIB_SECURITY_CREDENTIALS_TLS_GRPC_TLS_CREDENTIALS_OPTIONS_H
#define GRPC_SRC_CORE_LIB_SECURITY_CREDENTIALS_TLS_GRPC_TLS_CREDENTIALS_OPTIONS_H

#include <grpc/support/port_platform.h>

#include "absl/container/inlined_vector.h"

#include <grpc/grpc_security.h>

#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_distributor.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_provider.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_verifier.h"
#include "src/core/lib/security/security_connector/ssl_utils.h"

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
print("  // Getters for member fields.", file=H)
for data_member in _DATA_MEMBERS:
    if data_member.getter_comment != '':
        print("  // " + data_member.getter_comment, file=H)
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
print("  // Setters for member fields.", file=H)
for data_member in _DATA_MEMBERS:
    if data_member.setter_comment != '':
        print("  // " + data_member.setter_comment, file=H)
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
print("\n  bool operator==(const grpc_tls_credentials_options& other) const {",
      file=H)
operator_equal_content = "    return "
for i in range(len(_DATA_MEMBERS)):
    if (i != 0):
        operator_equal_content += "      "
    if (_DATA_MEMBERS[i].special_comparator != ''):
        operator_equal_content += _DATA_MEMBERS[i].special_comparator
    else:
        operator_equal_content += _DATA_MEMBERS[
            i].name + "_ == other." + _DATA_MEMBERS[i].name + "_"
    if (i != len(_DATA_MEMBERS) - 1):
        operator_equal_content += ' &&\n'
print(operator_equal_content + ";\n  }", file=H)

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

#endif  // GRPC_SRC_CORE_LIB_SECURITY_CREDENTIALS_TLS_GRPC_TLS_CREDENTIALS_OPTIONS_H""",
      file=H)

H.close()

# Generate test/core/security/grpc_tls_credentials_options_comparator_test.cc
TEST_FILE_NAME = 'test/core/security/grpc_tls_credentials_options_comparator_test.cc'
test_file_name = TEST_FILE_NAME
if (test_mode):
    test_file_name = tempfile.NamedTemporaryFile(delete=False).name
T = open(test_file_name, 'w')

put_copyright(T, '2022')
print('// Generated by tools/codegen/core/gen_grpc_tls_credentials_options.py',
      file=T)
print("""
#include <grpc/support/port_platform.h>

#include <string>

#include <gmock/gmock.h>

#include "src/core/lib/security/credentials/xds/xds_credentials.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace {
""",
      file=T)

# Generate negative test for each negative member
for data_member in _DATA_MEMBERS:
    print("""TEST(TlsCredentialsOptionsComparatorTest, %s) {
  auto* options_1 = grpc_tls_credentials_options_create();
  auto* options_2 = grpc_tls_credentials_options_create();
  options_1->set_%s(%s);
  options_2->set_%s(%s);
  EXPECT_FALSE(*options_1 == *options_2);
  EXPECT_FALSE(*options_2 == *options_1);
  delete options_1;
  delete options_2;
}""" % (data_member.test_name, data_member.name, data_member.test_value_1,
        data_member.name, data_member.test_value_2),
          file=T)

# Print out file ending
print("""
} // namespace
} // namespace grpc_core

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}""",
      file=T)
T.close()

if (test_mode):
    header_diff = get_file_differences(header_file_name, HEADER_FILE_NAME)
    test_diff = get_file_differences(test_file_name, TEST_FILE_NAME)
    os.unlink(header_file_name)
    os.unlink(test_file_name)
    header_error = False
    for line in header_diff:
        print(line)
        header_error = True
    if header_error:
        print(
            HEADER_FILE_NAME +
            ' should not be manually modified. Please make changes to tools/distrib/gen_grpc_tls_credentials_options.py instead.'
        )
    test_error = False
    for line in test_diff:
        print(line)
        test_error = True
    if test_error:
        print(
            TEST_FILE_NAME +
            ' should not be manually modified. Please make changes to tools/distrib/gen_grpc_tls_credentials_options.py instead.'
        )
    if (header_error or test_error):
        sys.exit(1)
