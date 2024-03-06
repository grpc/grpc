#!/bin/bash
# Copyright 2024 gRPC authors.
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

# Generates a CA with the same issuer name as the good CA in this directory
openssl req -x509 -new -newkey rsa:2048 -nodes -keyout evil_ca.key -out evil_ca.pem \
  -config ca-openssl.cnf -days 3650 -extensions v3_req

# Generate the CRL file:
# ----------------------------------------------------------------------------
rm -rf evil_ca
mkdir evil_ca
cp ca-openssl.cnf evil_ca/
cp evil_ca.key evil_ca/
cp evil_ca.pem evil_ca/
pushd evil_ca 
touch index.txt
echo 1 > ./serial
echo 1000 > ./crlnumber

openssl ca -gencrl -out evil.crl -keyfile evil_ca.key -cert evil_ca.pem -crldays 3650
popd
cp "./evil_ca/evil.crl" ./crls/
rm -rf evil_ca

# openssl genrsa -out evil_cert.key.rsa 2048
# openssl pkcs8 -topk8 -in evil_cert.key.rsa -out evil_cert.key -nocrypt
# openssl req -new -key evil_cert.key -out evil_cert.csr
# openssl x509 -req -CA evil_ca.pem -CAkey evil_ca.key -CAcreateserial -in evil_cert.csr \
#   -out evil_cert.pem  -days 3650

# rm evil_ca.srl
# rm evil_cert.csr
# rm evil_cert.key.rsa