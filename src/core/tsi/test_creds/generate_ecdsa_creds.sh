#!/bin/bash
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

# This file generates the following in this test_creds directory:
# badclient_ecdsa.key
# badclient_ecdsa.pem
# client_ecdsa.pem
# client_ecdsa.key
set -e

cd "$(dirname "$0")"

# Generate bad (self-signed) ECDSA client key and cert
openssl ecparam -name prime256v1 -genkey -noout -out badclient_ecdsa.key
openssl req -new -x509 -key badclient_ecdsa.key -out badclient_ecdsa.pem \
  -subj "/CN=badclient_ecdsa" -days 3650 -sha256

# Generate valid ECDSA client key and cert signed by test CA (ca.pem / ca.key)
openssl ecparam -name prime256v1 -genkey -noout -out client_ecdsa.key
openssl req -new -key client_ecdsa.key -out client_ecdsa.csr \
  -subj "/CN=client_ecdsa"
openssl x509 -req -in client_ecdsa.csr -CA ca.pem -CAkey ca.key -CAcreateserial \
  -out client_ecdsa.pem -days 3650 -sha256

rm -f ./*.csr ./*.srl
