# Call Credentials

This directory defines the `grpc_call_credentials` interface, which is used to add per-call security information to a gRPC call.

## Overarching Purpose

Call credentials are used to attach security information to individual gRPC calls. This can be used for things like OAuth 2.0 access tokens, which need to be refreshed periodically.

## Files

- **`call_creds_util.h` / `call_creds_util.cc`**: Utilities for working with call credentials.
- **`composite_call_credentials.h` / `composite_call_credentials.cc`**: A call credential that is composed of multiple other call credentials.
- **`context_authenticator.h`**: A `grpc_auth_context` that is populated from call credentials.
- **`credentials.h`**: The base `grpc_credentials` class.
- **`fake_credentials.h`**: A fake implementation of call credentials for testing.
- **`jwt_credentials.h` / `jwt_credentials.cc`**: An implementation of call credentials that uses JWTs.
- **`oauth2_credentials.h` / `oauth2_credentials.cc`**: An implementation of call credentials that uses OAuth 2.0.
- **`plugin_credentials.h` / `plugin_credentials.cc`**: An implementation of call credentials that uses a plugin.
- **`sts_credentials.h` / `sts_credentials.cc`**: An implementation of call credentials that uses STS.

## Notes

- Call credentials are a powerful tool for adding per-call security to gRPC.
- They are often used in conjunction with channel credentials to provide a complete security solution.
- The `grpc_call_credentials` interface is designed to be extensible, allowing new call credential types to be added easily.
