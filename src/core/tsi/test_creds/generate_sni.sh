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

openssl genrsa -out sni1.key.rsa 2048
openssl pkcs8 -topk8 -in sni1.key.rsa -out sni1.key -nocrypt
openssl req -new -key sni1.key -out sni1.csr \
 -subj /C=US/ST=CA/L=SVL/O=gRPC/CN=testserver/ \
 -config sni_test_creds.cnf -reqexts sni1
openssl x509 -req -CA ca.pem -CAkey ca.key -CAcreateserial \
 -in sni1.csr -out sni1.pem -extensions sni1 \
  -extfile sni_test_creds.cnf -days 3650 -sha256

openssl genrsa -out sni2.key.rsa 2048
openssl pkcs8 -topk8 -in sni2.key.rsa -out sni2.key -nocrypt
openssl req -new -key sni2.key -out sni2.csr \
 -subj /C=US/ST=CA/L=SVL/O=gRPC/CN=testserver/ \
 -config sni_test_creds.cnf -reqexts sni2
openssl x509 -req -CA ca.pem -CAkey ca.key -CAcreateserial \
 -in sni2.csr -out sni2.pem -extensions sni2 \
  -extfile sni_test_creds.cnf -days 3650 -sha256

openssl genrsa -out sni3.key.rsa 2048
openssl pkcs8 -topk8 -in sni3.key.rsa -out sni3.key -nocrypt
openssl req -new -key sni3.key -out sni3.csr \
 -subj /C=US/ST=CA/L=SVL/O=gRPC/CN=testserver/ \
 -config sni_test_creds.cnf -reqexts sni3
openssl x509 -req -CA ca.pem -CAkey ca.key -CAcreateserial \
 -in sni3.csr -out sni3.pem -extensions sni3 \
  -extfile sni_test_creds.cnf -days 3650 -sha256

rm ./*.rsa
rm ./*.csr
rm ./*.srl
