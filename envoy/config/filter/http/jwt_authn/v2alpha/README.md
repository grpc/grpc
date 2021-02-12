# JWT Authentication HTTP filter config

## Overview

1. The proto file in this folder defines an HTTP filter config for "jwt_authn" filter.

2. This filter will verify the JWT in the HTTP request as:
    - The signature should be valid
    - JWT should not be expired
    - Issuer and audiences are valid and specified in the filter config.

3. [JWK](https://tools.ietf.org/html/rfc7517#appendix-A) is needed to verify JWT signature. It can be fetched from a remote server or read from a local file. If the JWKS is fetched remotely, it will be cached by the filter.

3. If a JWT is valid, the user is authenticated and the request will be forwarded to the backend server. If a JWT is not valid, the request will be rejected with an error message.

## The locations to extract JWT

JWT will be extracted from the HTTP headers or query parameters. The default location is the HTTP header:
```
Authorization: Bearer <token>
```
The next default location is in the query parameter as:
```
?access_token=<TOKEN>
```

If a custom location is desired, `from_headers` or `from_params` can be used to specify custom locations to extract JWT.

## HTTP header to pass successfully verified JWT

If a JWT is valid, its payload will be passed to the backend in a new HTTP header specified in `forward_payload_header` field. Its value is base64url-encoded JWT payload in JSON.


## Further header options

In addition to the `name` field, which specifies the HTTP header name,
the `from_headers` section can specify an optional `value_prefix` value, as in:

```yaml
    from_headers:
      - name: bespoke
        value_prefix: jwt_value
```

The above will cause the jwt_authn filter to look for the JWT in the `bespoke` header, following the tag `jwt_value`.

Any non-JWT characters (i.e., anything _other than_ alphanumerics, `_`, `-`, and `.`) will be skipped,
and all following, contiguous, JWT-legal chars will be taken as the JWT.

This means all of the following will return a JWT of `eyJFbnZveSI6ICJyb2NrcyJ9.e30.c2lnbmVk`:

```text
bespoke: jwt_value=eyJFbnZveSI6ICJyb2NrcyJ9.e30.c2lnbmVk

bespoke: {"jwt_value": "eyJFbnZveSI6ICJyb2NrcyJ9.e30.c2lnbmVk"}

bespoke: beta:true,jwt_value:"eyJFbnZveSI6ICJyb2NrcyJ9.e30.c2lnbmVk",trace=1234
```

The header `name` may be `Authorization`.

The `value_prefix` must match exactly, i.e., case-sensitively.
If the `value_prefix` is not found, the header is skipped: not considered as a source for a JWT token.

If there are no JWT-legal characters after the `value_prefix`, the entire string after it
is taken to be the JWT token. This is unlikely to succeed; the error will reported by the JWT parser.