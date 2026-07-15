# Transport-Specific Credentials

This directory contains credentials implementations that are tied to a specific transport security type.

## Overarching Purpose

These credentials are used to configure the security of the underlying transport. For example, the `TlsCredentials` are used to configure TLS for the Chttp2 transport.

## Files

- **`alts_credentials.h` / `alts_credentials.cc`**: An implementation of credentials that uses ALTS.
- **`google_compute_engine_credentials.h` / `google_compute_engine_credentials.cc`**: An implementation of credentials that uses GCE credentials.
- **`google_iam_credentials.h` / `google_iam_credentials.cc`**: An implementation of credentials that uses Google IAM credentials.
- **`insecure_credentials.h` / `insecure_credentials.cc`**: An implementation of credentials that does not use any security.
- **`local_credentials.h` / `local_credentials.cc`**: An implementation of credentials that uses local credentials (e.g., UDS).
- **`tls_credentials.h` / `tls_credentials.cc`**: An implementation of credentials that uses TLS.
- **`xds_credentials.h` / `xds_credentials.cc`**: An implementation of credentials that uses XDS.

## Notes

- These credentials are used to configure the security of the underlying transport.
- They are typically used in conjunction with channel credentials to provide a complete security solution.
- The credentials in this directory are specific to certain transport security types, and will not work with other transport security types.
