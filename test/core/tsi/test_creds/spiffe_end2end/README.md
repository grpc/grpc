All of the following files in this directory except `server_spiffebundle.json`
and `client_spiffebundle.json` are generated with the `generate.sh` and
`generate_intermediate.sh` script in this directory.

These comprise a root trust certificate authority (CA) that signs two
certificates - `client_spiffe.pem` and `server_spiffe.pem`. These are valid
SPIFFE certificates (via the configuration in `spiffe-openssl.cnf`), and the
`*_spiffebundle.json` files are SPIFFE Bundle Maps for the client and server
respectively.

The SPIFFE trust bundle map files (`*_spiffebundle.json`) are manually created
for end to end testing. The `server_spiffebundle.json` contains the
`foo.bar.com` trust domain (only this entry is used in e2e tests) matching URI
SAN of `client_spiffe.pem`, and the CA certificate is `ca.pem`. The client
`spiffebundle.json` file contains `example.com` trust domain matching the URI
SAN of `server_spiffe.pem`, and the CA certificate there is also `ca.pem`.

`leaf_and_intermediate_chain.pem` is a certificate chain whose leaf is a valid
SPIFFE cert that is signed by an intermediate CA (`intermediate_ca.pem`). The
intermediate CA is signed by the root CA (`ca.pem`). Thus, this setup yields a
valid chain to the root of trust `ca.pem`.

If updating these files, the `x5c` field in the json is the raw PEM CA
certificate and can be copy pasted from the certificate file `ca.pem`. `n` and
`e` are values from the public key attached to this certificate. `e` should
*probably* be `AQAB` as it is the exponent. `n` can be fetched from the
certificate by getting the RSA key from the cert and extracting the value. This
can be done in golang with the following codeblock:

```
func(GetBase64ModulusFromPublicKey(key *rsa.PublicKey) string {
    return base64.RawURLEncoding.EncodeToString(key.N.Bytes())
}

block, _ := pem.Decode(rawPemCert) cert, _ := x509.ParseCertificate(block.Bytes)
publicKey := cert.PublicKey.(*rsa.PublicKey)
fmt.Println(GetBase64ModulusFromPublicKey(publicKey))
```
