# Generates a CA with the same issuer name as the good CA in this directory
openssl req -x509 -new -newkey rsa:2048 -nodes -keyout evil_ca.key -out evil_ca.pem \
  -config ca-openssl.cnf -days 3650 -extensions v3_req
