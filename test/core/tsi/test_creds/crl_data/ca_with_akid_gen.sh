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

rm -rf ca_with_akid/
mkdir ca_with_akid/
cp ca-with-akid.cnf ca_with_akid/
pushd ca_with_akid/ || exit
touch index.txt
echo 1 > ./serial
echo 1000 > ./crlnumber

openssl req -x509 -new -newkey rsa:2048 -nodes -keyout ca_with_akid.key -out ca_with_akid.pem \
  -config ca-with-akid.cnf -days 3650 -extensions v3_req
  
openssl ca -gencrl -out crl_with_akid.crl -keyfile ca_with_akid.key -cert ca_with_akid.pem -crldays 3650  -config ca-with-akid.cnf

popd || exit

cp "./ca_with_akid/ca_with_akid.key" ./
cp "./ca_with_akid/ca_with_akid.pem" ./
cp "./ca_with_akid/crl_with_akid.crl" ./

rm -rf ca_with_akid