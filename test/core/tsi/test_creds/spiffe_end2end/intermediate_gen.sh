#!/bin/bash
# Copyright 2025 gRPC authors.
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

set -e

# Meant to be run from testdata/spiffe_end2end/
# Sets up an intermediate ca, generates certificates, then copies then up and deletes unnecessary files

rm -rf intermediate_ca
mkdir intermediate_ca
cp intermediate.cnf intermediate_ca/
cp spiffe-openssl.cnf intermediate_ca/
pushd intermediate_ca

# Generating the intermediate CA
openssl genrsa -out temp.rsa 2048
openssl pkcs8 -topk8 -in temp.rsa -out intermediate_ca.key -nocrypt
rm temp.rsa
openssl req -key intermediate_ca.key -new -out temp.csr -config intermediate.cnf
openssl x509 -req -days 3650 -in temp.csr -CA "../ca.pem" -CAkey "../ca.key" -CAcreateserial -out intermediate_ca.pem -extfile intermediate.cnf  -extensions 'v3_req'

# Generating the leaf and chain
openssl genrsa -out temp.rsa 2048
openssl pkcs8 -topk8 -in temp.rsa -out leaf_signed_by_intermediate.key -nocrypt
openssl req -new -key leaf_signed_by_intermediate.key -out spiffe-cert.csr \
 -subj /C=US/ST=CA/L=SVL/O=gRPC/CN=testserver/ \
 -config spiffe-openssl.cnf -reqexts spiffe_server_e2e
openssl x509 -req -CA intermediate_ca.pem -CAkey intermediate_ca.key -CAcreateserial \
 -in spiffe-cert.csr -out leaf_signed_by_intermediate.pem -extensions spiffe_server_e2e \
  -extfile spiffe-openssl.cnf -days 3650 -sha256
cat leaf_signed_by_intermediate.pem intermediate_ca.pem > leaf_and_intermediate_chain.pem

popd

# Copy files up to the higher directory
cp "./intermediate_ca/leaf_signed_by_intermediate.key" ./
cp "./intermediate_ca/leaf_signed_by_intermediate.pem" ./
cp "./intermediate_ca/leaf_and_intermediate_chain.pem" ./
cp "./intermediate_ca/intermediate_ca.key" ./
cp "./intermediate_ca/intermediate_ca.pem" ./

rm ca.srl
rm -rf intermediate_ca
