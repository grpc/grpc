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

# Generate client/server self signed CAs and certs.
openssl req -x509 -newkey rsa:4096 -keyout ca.key -out ca.pem -days 365 -nodes -subj "/C=US/ST=VA/O=Internet Widgits Pty Ltd/CN=foo.bar.hoo.ca.com"

# The SPIFFE related extensions are listed in spiffe-openssl.cnf config. Both
# client_spiffe.pem and server_spiffe.pem are generated in the same way with
# original client.pem and server.pem but with using that config. Here are the
# exact commands (we pass "-subj" as argument in this case):
openssl genrsa -out client.key.rsa 2048
openssl pkcs8 -topk8 -in client.key.rsa -out client.key -nocrypt
openssl req -new -key client.key -out spiffe-cert.csr \
 -subj /C=US/ST=CA/L=SVL/O=gRPC/CN=testclient/ \
 -config spiffe-openssl.cnf -reqexts spiffe_client_e2e
openssl x509 -req -CA ca.pem -CAkey ca.key -CAcreateserial \
 -in spiffe-cert.csr -out client_spiffe.pem -extensions spiffe_client_e2e \
  -extfile spiffe-openssl.cnf -days 3650 -sha256

openssl genrsa -out server.key.rsa 2048
openssl pkcs8 -topk8 -in server.key.rsa -out server.key -nocrypt
openssl req -new -key server.key -out spiffe-cert.csr \
 -subj "/C=US/ST=CA/L=SVL/O=gRPC/CN=*.test.google.com/" \
 -config spiffe-openssl.cnf -reqexts spiffe_server_e2e
openssl x509 -req -CA ca.pem -CAkey ca.key -CAcreateserial \
 -in spiffe-cert.csr -out server_spiffe.pem -extensions spiffe_server_e2e \
  -extfile spiffe-openssl.cnf -days 3650 -sha256

openssl genrsa -out multi_san.key.rsa 2048
openssl pkcs8 -topk8 -in multi_san.key.rsa -out multi_san.key -nocrypt
openssl req -new -key multi_san.key -out spiffe-cert.csr \
 -subj /C=US/ST=CA/L=SVL/O=gRPC/CN=testclient/ \
 -config spiffe-openssl.cnf -reqexts spiffe_server_e2e
openssl x509 -req -CA ca.pem -CAkey ca.key -CAcreateserial \
 -in spiffe-cert.csr -out multi_san_spiffe.pem -extensions spiffe_client_multi \
  -extfile spiffe-openssl.cnf -days 3650 -sha256

openssl genrsa -out invalid_utf8_san.key.rsa 2048
openssl pkcs8 -topk8 -in invalid_utf8_san.key.rsa -out invalid_utf8_san.key -nocrypt
openssl req -new -key invalid_utf8_san.key -out spiffe-cert.csr \
 -subj /C=US/ST=CA/L=SVL/O=gRPC/CN=testclient/ \
 -config spiffe-openssl.cnf -reqexts spiffe_server_e2e
openssl x509 -req -CA ca.pem -CAkey ca.key -CAcreateserial \
 -in spiffe-cert.csr -out invalid_utf8_san_spiffe.pem -extensions spiffe_client_non_utf8 \
  -extfile spiffe-openssl.cnf -days 3650 -sha256

rm ./*.rsa
rm ./*.csr
rm ./*.srl
