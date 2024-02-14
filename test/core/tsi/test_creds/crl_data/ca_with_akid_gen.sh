rm -rf ca_with_akid/
mkdir ca_with_akid/
cp ca-with-akid.cnf ca_with_akid/
pushd ca_with_akid/
touch index.txt
echo 1 > ./serial
echo 1000 > ./crlnumber

openssl req -x509 -new -newkey rsa:2048 -nodes -keyout ca_with_akid.key -out ca_with_akid.pem \
  -config ca-with-akid.cnf -days 3650 -extensions v3_req
  
openssl ca -gencrl -out crl_with_akid.crl -keyfile ca_with_akid.key -cert ca_with_akid.pem -crldays 3650  -config ca-with-akid.cnf

popd

cp "./ca_with_akid/ca_with_akid.key" ./
cp "./ca_with_akid/ca_with_akid.pem" ./
cp "./ca_with_akid/crl_with_akid.crl" ./

rm -rf ca_with_akid