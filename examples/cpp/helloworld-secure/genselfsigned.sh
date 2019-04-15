openssl genrsa -out selfsigned2.key 
openssl req -new -x509 -key selfsigned2.key -out selfsigned2.cert -days 3650 -subj "/C=US/ST=Ohio/L=Columbus/O=Acme Company/OU=Acme/CN=localhost"

