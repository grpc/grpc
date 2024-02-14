openssl req -x509 -new -newkey rsa:2048 -nodes -keyout ca_with_akid.key -out ca_with_akid.pem \
  -config ca-with-akid.cnf -days 3650 -extensions v3_req
  
openssl ca -gencrl -out crl_with_akid.crl -keyfile ca_with_akid.key -cert ca_with_akid.pem -crldays 3650