# ECDSA Test Credentials

This directory contains ECDSA test certificates and keys used for multi-certificate algorithm negotiation tests and ECDSA client/server tests.

## Credentials

1. **`server_ecdsa.key` / `server_ecdsa.pem`**:
   * ECDSA server private key (`prime256v1`) and certificate signed by the test root CA (`src/core/tsi/test_creds/ca.pem`).
   * Subject Common Name: `*.test.google.fr`.
   * Subject Alternative Names: `*.test.google.fr`, `waterzooi.test.google.be`, `*.test.youtube.com`, `192.168.1.3`.
   * Shares the exact same SANs as `src/core/tsi/test_creds/server1.pem` (RSA) to test grouping of RSA and ECDSA certificates on the same `SSL_CTX`.

2. **`client_ecdsa.key` / `client_ecdsa.pem`**:
   * ECDSA client private key (`prime256v1`) and certificate signed by the test root CA (`src/core/tsi/test_creds/ca.pem`).
   * Subject Common Name: `client_ecdsa`.

3. **`badclient_ecdsa.key` / `badclient_ecdsa.pem`**:
   * Self-signed ECDSA client certificate (`prime256v1`) not trusted by the test root CA.

## Generation

Run `./generate_ecdsa_creds.sh` to regenerate these credentials using OpenSSL.
