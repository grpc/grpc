#!/bin/bash
cd "$(dirname "$0")"
# Copyright 2026 gRPC authors.
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

# Generate client_only_eku cert signed by src/core/tsi/test_creds/ca
openssl genrsa -out client_only_eku.key.rsa 2048
openssl pkcs8 -topk8 -in client_only_eku.key.rsa -out client_only_eku.key -nocrypt
openssl req -new -key client_only_eku.key -out client_only_eku.csr -config client_only_eku.cnf
openssl x509 -req -CA ../../../../../src/core/tsi/test_creds/ca.pem -CAkey ../../../../../src/core/tsi/test_creds/ca.key -CAcreateserial -in client_only_eku.csr -out client_only_eku.pem -extensions v3_req -extfile client_only_eku.cnf -days 3650 -sha256

# Generate server_only_eku cert signed by src/core/tsi/test_creds/ca
openssl genrsa -out server_only_eku.key.rsa 2048
openssl pkcs8 -topk8 -in server_only_eku.key.rsa -out server_only_eku.key -nocrypt
openssl req -new -key server_only_eku.key -out server_only_eku.csr -config server_only_eku.cnf
openssl x509 -req -CA ../../../../../src/core/tsi/test_creds/ca.pem -CAkey ../../../../../src/core/tsi/test_creds/ca.key -CAcreateserial -in server_only_eku.csr -out server_only_eku.pem -extensions v3_req -extfile server_only_eku.cnf -days 3650 -sha256

rm ./*.rsa
rm ./*.csr
rm -f ../../../../../src/core/tsi/test_creds/ca.srl
rm -f ./ca.srl
