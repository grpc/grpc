# EKU Test Credentials

These credentials are used to test TLS verification key purpose bypass.

All certificates are signed by the CA in `src/core/tsi/test_creds/ca.pem`.

## Files

### `client_only_eku.pem` / `client_only_eku.key`
*   **EKU**: `clientAuth` (lacks `serverAuth`).
*   **Purpose**: Used to verify client-side EKU validation. A client connection to a server presenting this certificate should fail by default (as it lacks `serverAuth`), but succeed when the client sets `verification_key_purpose` to `ALLOW_ANY`.

### `server_only_eku.pem` / `server_only_eku.key`
*   **EKU**: `serverAuth` (lacks `clientAuth`).
*   **Purpose**: Used to verify server-side EKU validation in mTLS. When a server requiring client authentication receives this certificate from a client, the handshake should fail by default (as it lacks `clientAuth`), but succeed when the server sets `verification_key_purpose` to `ALLOW_ANY`.
