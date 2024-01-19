#!/bin/bash
# Copyright 2023 gRPC authors.
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

# Meant to be run from test/core/tsi/test_creds/crl_data
# Sets up an intermediate ca, generates certificates and crl files, then copies then up and deletes unnecessary files

rm -rf intermediate_ca
mkdir intermediate_ca
cp intermediate.cnf intermediate_ca/
cp leaf_signed_by_intermediate.cnf intermediate_ca/
pushd intermediate_ca
touch index.txt
echo 1 > ./serial
echo 1000 > ./crlnumber

# Generating the intermediate CA
openssl genrsa -out temp.rsa 2048
openssl pkcs8 -topk8 -in temp.rsa -out intermediate_ca.key -nocrypt
rm temp.rsa
openssl req -key intermediate_ca.key -new -out temp.csr -config intermediate.cnf
openssl x509 -req -days 3650 -in temp.csr -CA "../ca.pem" -CAkey "../ca.key" -CAcreateserial -out intermediate_ca.pem -extfile intermediate.cnf -extensions 'v3_req'

# Generating the leaf and chain
openssl genrsa -out temp.rsa 2048
openssl pkcs8 -topk8 -in temp.rsa -out leaf_signed_by_intermediate.key -nocrypt
openssl req -key leaf_signed_by_intermediate.key -new -out temp.csr -config leaf_signed_by_intermediate.cnf
openssl x509 -req -days 3650 -in temp.csr -CA intermediate_ca.pem -CAkey intermediate_ca.key -CAcreateserial -out leaf_signed_by_intermediate.pem -extfile leaf_signed_by_intermediate.cnf -extensions 'v3_req'
cat leaf_signed_by_intermediate.pem intermediate_ca.pem > leaf_and_intermediate_chain.pem

# Generate empty CRL for the intermediate
openssl ca -config=intermediate.cnf -gencrl -out intermediate.crl -keyfile intermediate_ca.key -cert intermediate_ca.pem -crldays 3650
popd

# Copy files up to the higher directory
cp "./intermediate_ca/leaf_signed_by_intermediate.key" ./
cp "./intermediate_ca/leaf_signed_by_intermediate.pem" ./
cp "./intermediate_ca/leaf_and_intermediate_chain.pem" ./
cp "./intermediate_ca/intermediate_ca.key" ./
cp "./intermediate_ca/intermediate_ca.pem" ./

# Revoke the intermediate
openssl ca -revoke intermediate_ca.pem -keyfile ca.key -cert ca.pem -days 3650
openssl ca -gencrl -out current.crl -keyfile ca.key -cert ca.pem -crldays 3650


# Copy CRLs into their own directory and run rehash
cp "./intermediate_ca/intermediate.crl" ./crls
cp current.crl ./crls/
openssl rehash ./crls/

mkdir crls_missing_intermediate
cp current.crl ./crls_missing_intermediate/
openssl rehash ./crls_missing_intermediate/

mkdir crls_missing_root
cp intermediate.crl ./crls_missing_root/
openssl rehash ./crls_missing_root/

rm intermediate_ca
