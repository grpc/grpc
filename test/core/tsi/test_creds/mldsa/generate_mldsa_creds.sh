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

# This script generates the following ML-DSA test credentials (using the
# RFC 9881 32-byte seed-only private key representation required by BoringSSL):
# ca_mldsa65.key
# ca_mldsa65.pem
# client_mldsa65.key
# client_mldsa65.pem
# server_mldsa65.key
# server_mldsa65.pem
# ca_mldsa87.key
# ca_mldsa87.pem
# client_mldsa87.key
# client_mldsa87.pem
# server_mldsa87.key
# server_mldsa87.pem

set -e

cd "$(dirname "$0")"

CA_DIR="../../../../../src/core/tsi/test_creds"

generate_mldsa_set() {
  local alg="$1"
  local suffix="$2"

  # 1. Generate self-signed Root CA key and cert (seed-only format)
  openssl genpkey -algorithm "${alg}" \
    -provparam ml-dsa.output_formats=seed-only \
    -out "ca_${suffix}.key"
  openssl req -x509 -new -key "ca_${suffix}.key" \
    -out "ca_${suffix}.pem" -days 3650 \
    -config "${CA_DIR}/ca-openssl.cnf" -extensions v3_req \
    -subj "/C=AU/ST=Some-State/O=Internet Widgits Pty Ltd/CN=testca"

  # 2. Generate client key and cert signed by the ML-DSA Root CA
  openssl genpkey -algorithm "${alg}" \
    -provparam ml-dsa.output_formats=seed-only \
    -out "client_${suffix}.key"
  openssl req -new -key "client_${suffix}.key" \
    -out "client_${suffix}.csr" \
    -subj "/C=AU/ST=Some-State/O=Internet Widgits Pty Ltd/CN=testclient"
  openssl x509 -req -in "client_${suffix}.csr" \
    -CA "ca_${suffix}.pem" -CAkey "ca_${suffix}.key" -CAcreateserial \
    -out "client_${suffix}.pem" -days 3650

  # 3. Generate server key and cert signed by the ML-DSA Root CA
  openssl genpkey -algorithm "${alg}" \
    -provparam ml-dsa.output_formats=seed-only \
    -out "server_${suffix}.key"
  openssl req -new -key "server_${suffix}.key" \
    -out "server_${suffix}.csr" \
    -config "${CA_DIR}/server1-openssl.cnf" \
    -subj "/C=US/ST=Illinois/L=Chicago/O=Example, Co./CN=*.test.google.com"
  openssl x509 -req -in "server_${suffix}.csr" \
    -CA "ca_${suffix}.pem" -CAkey "ca_${suffix}.key" -CAcreateserial \
    -extfile "${CA_DIR}/server1-openssl.cnf" -extensions v3_req \
    -out "server_${suffix}.pem" -days 3650
}

generate_mldsa_set "ML-DSA-65" "mldsa65"
generate_mldsa_set "ML-DSA-87" "mldsa87"

chmod 644 ./*.key ./*.pem
rm -f ./*.csr ./*.srl
