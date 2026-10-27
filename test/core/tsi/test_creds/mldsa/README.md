# ML-DSA Test Credentials

This directory contains ML-DSA-44, ML-DSA-65, and ML-DSA-87 post-quantum test certificates and keys used for TLS 1.3 handshake and RPC tests.

## Credentials

1. **`ca_mldsa44.key` / `ca_mldsa44.pem`**, **`ca_mldsa65.key` / `ca_mldsa65.pem`**, and **`ca_mldsa87.key` / `ca_mldsa87.pem`**:
   * Self-signed ML-DSA-44, ML-DSA-65, and ML-DSA-87 root CA private keys and certificates.
   * Subject: `C=AU, ST=Some-State, O=Internet Widgits Pty Ltd, CN=testca`.

2. **`server_mldsa44.key` / `server_mldsa44.pem`**, **`server_mldsa65.key` / `server_mldsa65.pem`**, and **`server_mldsa87.key` / `server_mldsa87.pem`**:
   * ML-DSA-44, ML-DSA-65, and ML-DSA-87 server private keys and certificates signed by the corresponding ML-DSA root CA (`ca_mldsa44.pem` / `ca_mldsa65.pem` / `ca_mldsa87.pem`).
   * Subject Common Name: `*.test.google.com`.
   * Subject Alternative Names: `*.test.google.fr`, `waterzooi.test.google.be`, `*.test.youtube.com`, `192.168.1.3` (matching `src/core/tsi/test_creds/server1.pem`).

3. **`client_mldsa44.key` / `client_mldsa44.pem`**, **`client_mldsa65.key` / `client_mldsa65.pem`**, and **`client_mldsa87.key` / `client_mldsa87.pem`**:
   * ML-DSA-44, ML-DSA-65, and ML-DSA-87 client private keys and certificates signed by the corresponding ML-DSA root CA (`ca_mldsa44.pem` / `ca_mldsa65.pem` / `ca_mldsa87.pem`).
   * Subject Common Name: `testclient`.

4. **`server_chain_mldsa87.key` / `server_chain_mldsa87.pem`** and **`client_chain_mldsa87.key` / `client_chain_mldsa87.pem`**:
   * 2-certificate ML-DSA-87 server and client certificate chains (`leaf + intermediate`), where the leaf certificates are signed by an ML-DSA-87 intermediate CA (`CN=testintermediateca`) that in turn chains up to `ca_mldsa87.pem`.

## RFC 9881 `seed` Private Key Representation

Under FIPS 204 (ML-DSA) and RFC 9881, an ML-DSA private key in PKCS#8 can be encoded as `seed` (32 bytes, tag `[0]`), `expandedKey` (tag `[1]`), or `both` (tag `[2]`). BoringSSL strictly requires the 32-byte `seed` representation and rejects `expandedKey` or `both` with `EVP_R_PRIVATE_KEY_WAS_NOT_SEED`.

All private keys in this directory use the 32-byte `seed-only` format (`-provparam ml-dsa.output_formats=seed-only` in OpenSSL 3.5+) for compatibility with both BoringSSL and OpenSSL 3.5+.

## Generation

Run `./generate_mldsa_creds.sh` (requires OpenSSL 3.5+) to regenerate these credentials.
