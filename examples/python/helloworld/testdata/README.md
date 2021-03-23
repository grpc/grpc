## Key Generation
### CA Key/Cert Generation
```
openssl genrsa -out ca.key 4096
openssl req -new -x509 -key ca.key -sha256 -subj "/C=US/ST=NJ/O=CA, Inc." -days 365 -out ca.cert
```
### Client Key/Cert Generation
```
openssl genrsa -out client.key 4096
openssl req -new -key client.key -sha256 -subj "/C=US/ST=NJ/O=CA/CN=localhost" -out client.csr
openssl x509 -req -in client.csr -CA ca.cert -CAkey ca.key -CAcreateserial -out client.pem -days 365 -sha256 -extfile certificate.conf -extensions req_ext
```

### Server Key/Cert Generation
```
openssl genrsa -out service.key 4096
openssl req -new -key service.key -sha256 -subj "/C=US/ST=NJ/O=CA/CN=localhost" -out service.csr
openssl x509 -req -in service.csr -CA ca.cert -CAkey ca.key -CAcreateserial -out service.pem -days 365 -sha256 -extfile certificate.conf -extensions req_ext
```