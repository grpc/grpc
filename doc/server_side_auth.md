Server-side API for Authenticating Clients
==========================================

NOTE: This document describes how server-side authentication works in C-core based gRPC implementations only. In gRPC Java and Go, server side authentication is handled differently.

## AuthContext

To perform server-side authentication, gRPC exposes the *authentication context* for each call. The context exposes important authentication-related information about the RPC such as the type of security/authentication type being used and the peer identity.

The authentication context is structured as a multi-map of key-value pairs - the *auth properties*. In addition to that, for authenticated RPCs, the set of properties corresponding to a selected key will represent the verified identity of the caller - the *peer identity*.

The contents of the *auth properties* are populated by an *auth interceptor*. The interceptor also chooses which property key will act as the peer identity (e.g. for client certificate authentication this property will be `"x509_common_name"` or `"x509_subject_alternative_name"`).

WARNING: AuthContext is the only reliable source of truth when it comes to authenticating RPCs. Using any other call/context properties for authentication purposes is wrong and inherently unsafe.

#### Example AuthContext contents

For secure channel using mutual TLS authentication with both client and server certificates (test certificates from this repository are used).

Populated auth properties:
```
"transport_security_type": "ssl"  # connection is secured using TLS/SSL
"x509_common_name": "*.test.google.com"  # from client's certificate
"x509_pem_cert": "-----BEGIN CERTIFICATE-----\n..."  # client's PEM encoded certificate
"x509_subject_alternative_name": "*.test.google.fr"
"x509_subject_alternative_name": "waterzooi.test.google.be"
"x509_subject_alternative_name": "*.test.youtube.com"
"x509_subject_alternative_name": "192.168.1.3"
```

The peer identity is set of all properties named `"x509_subject_alternative_name"`:
```
peer_identity_property_name = "x509_subject_alternative_name"
```

## AuthProperty

Auth properties are elements of the AuthContext. They have a name (a key of type string) and a value which can be a string or binary data.

## Auth Interceptors

Auth interceptors are gRPC components that populate contents of the auth context based on gRPC's internal state and/or call metadata.
gRPC comes with some basic "interceptors" already built-in.

WARNING: While there is a public API that allows anyone to write their own custom interceptor, please think twice before using it.
There are legitimate uses for custom interceptors but you should keep in mind that as auth interceptors essentially decide which RPCs are authenticated and which are not, their code is very sensitive from the security perspective and getting things wrong might have serious consequences. If unsure, we strongly recommend to rely on official & proven interceptors that come with gRPC.

#### Available auth interceptors
- TLS/SSL certificate authentication (built into gRPC's security layer, automatically used whenever you use a secure connection)
- (coming soon) JWT auth token authentication
- more will be added over time

## Status (by language)
C-core exposes low level API to access auth context contents and to implement an auth interceptor.
In C++, the auth interceptor API is exposed as `AuthMetadataProcessor`.

A high level API to access AuthContext contents is available in these languages:
- C++
- C# (implementation in-progress)
- other languages coming soon
