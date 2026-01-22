## File Purposes

*   spiffe_cert.pem - the certificate that is placed in spiffe bundles (copied
    into the `x5c` field)
*   server1_spiffe.pem - another certificate placed in spiffe bundles
*   spiffe_multi_uri_san_cert.pem - another certificate placed in spiffe bundles
*   spiffe-openssl.cnf - the configuration file passed to the openssl CLI when
    creating these certificate files
*   spiffebundle.json - the valid spiffe bundle for happy path testing. The `example.com` bundle certificate corresponds to `spiffe_cert.pem` and the `test.example.com` bundle corresponds to `server1_spiffe.pem`.
*   spiffebundle2.json -  a valid spiffe bundle with multiple roots in a trust domain. The first root corresponds to `spiffe_cert.pem` and the second root corresponds to `server1_spiffe.pem`.
*   spiffebundle_corrupted_cert.json - manually modifies the `x5c` field and
    removes a character to create an invalid certificate
*   spiffebundle_empty_keys.json - the `keys` field is an empty array
*   spiffebundle_empty_string_keys.json - the `keys` field contains an entry
*   with an empty string key
*   spiffebundle_invalid_trustdomain - uses a `#` in the trust domain which is a
    disallowed character per the spec
*   spiffebundle_malformed.json - a fully wrong json
*   spiffebundle_match_client_spiffe.json - a valid spiffe bundle with a trust
    domain matching the SPIFFE ID in spiffe_cert.pem
*   spiffebundle_wrong_kid.json - has the `kid` field instead of the `kty` field
*   spiffebundle_wrong_kty.json - Uses `EC` instead of `RSA` in the `kty` field
*   spiffebundle_wrong_multi_certs.json - place 2 certificates in the `x5c`
    field
*   spiffebundle_wrong_root.json - The top level json string is `trustDomains`
    instead of `trust_domains`
*   spiffebundle_wrong_seq_type.json - the `spiffe_sequence` number must be an
    integer
*   spiffebundle_wrong_use.json - The `use` field must be `x509-svid` or
    `jwt-svid` (we are expecting and support `x509-svid` per the gRFC)

## Test File Creation:

The SPIFFE related extensions are listed in spiffe-openssl.cnf config. Both
client_spiffe.pem and server1_spiffe.pem are generated in the same way as the
client and server certificates described in the testdata/x509 with the same CAs.
Specifically they were made with the following commands:

```
$ openssl req -new -key client.key -out spiffe-cert.csr \
 -subj /C=US/ST=CA/L=SVL/O=gRPC/CN=testclient/ \
 -config spiffe-openssl.cnf -reqexts spiffe_client_e2e
$ openssl x509 -req -CA ca.pem -CAkey ca.key -CAcreateserial \
 -in spiffe-cert.csr -out client_spiffe.pem -extensions spiffe_client_e2e \
  -extfile spiffe-openssl.cnf -days 3650 -sha256
$ openssl req -new -key server1.key -out spiffe-cert.csr \
 -subj /C=US/ST=CA/L=SVL/O=gRPC/CN=*.test.google.com/ \
 -config spiffe-openssl.cnf -reqexts spiffe_server_e2e
$ openssl x509 -req -CA ca.pem -CAkey ca.key -CAcreateserial \
 -in spiffe-cert.csr -out server1_spiffe.pem -extensions spiffe_server_e2e \
  -extfile spiffe-openssl.cnf -days 3650 -sha256
```

Additionally, the SPIFFE trust bundle map files (spiffebundle*.json) are
manually created for end to end testing. The spiffebundle.json contains the
"example.com" trust domain (only this entry is used in e2e tests) matching URI
SAN of server1_spiffe.pem, and the CA certificate there is ca.pem. The
spiffebundle.json file contains "foo.bar.com" trust domain (only this entry is
used in e2e tests) matching URI SAN of client_spiffe.pem, and the CA certificate
there is also ca.pem.

If updating these files, the `x5c` field in the json is the raw PEM certificates
and can be copy pasted from the certificate file. `n` and `e` are values from
the public key. `e` should *probably* be `AQAB` as it is the exponent. `n` can
be fetched from the certificate by getting the RSA key from the cert and
extracting the value. This can be done in golang with the following codeblock:
``` func GetBase64ModulusFromPublicKey(key *rsa.PublicKey) string { return
base64.RawURLEncoding.EncodeToString(key.N.Bytes()) }

block, _ := pem.Decode(rawPemCert) cert, _ := x509.ParseCertificate(block.Bytes)
publicKey := cert.PublicKey.(*rsa.PublicKey)
fmt.Println(GetBase64ModulusFromPublicKey(publicKey)) ```

The rest of the files are manually modified as described above.
