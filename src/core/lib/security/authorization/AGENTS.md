# Authorization

This directory contains the implementation of the gRPC authorization framework.

## Overarching Purpose

The authorization framework provides a mechanism for authorizing incoming requests based on a set of rules defined in an authorization policy. This can be used to control access to gRPC services and methods based on the authenticated user's identity and other request metadata.

## Core Concepts

The authorization framework is built around a few key abstractions:

*   **`AuthorizationEngine`**: The `AuthorizationEngine` is the main interface for making authorization decisions. It takes a set of evaluation arguments and returns an authorization decision.
*   **`AuthorizationPolicyProvider`**: The `AuthorizationPolicyProvider` is responsible for providing the authorization policy to the `AuthorizationEngine`. This allows the authorization policy to be loaded from a variety of sources, such as a local file or a remote server.
*   **`EvaluateArgs`**: The `EvaluateArgs` struct contains the information that is needed to make an authorization decision. This includes the request headers, the peer identity, and other information about the request.

## Implementations

The authorization framework includes two main implementations of the `AuthorizationEngine`:

*   **`GrpcAuthorizationEngine`**: This implementation is based on the [Envoy RBAC filter](https://www.envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/rbac_filter). It uses a set of RBAC policies to make authorization decisions.
*   **`CelAuthorizationEngine`**: This implementation is based on the [Common Expression Language (CEL)](https://cel-spec.dev/). It uses a set of CEL expressions to make authorization decisions. This implementation is still experimental.

## Files

*   **`authorization_engine.h`**: Defines the `AuthorizationEngine` interface.
*   **`grpc_authorization_engine.h`, `grpc_authorization_engine.cc`**: These files define the `GrpcAuthorizationEngine` class.
*   **`cel_authorization_engine.h`, `cel_authorization_engine.cc`**: These files define the `CelAuthorizationEngine` class.
*   **`grpc_server_authz_filter.h`, `grpc_server_authz_filter.cc`**: These files define the `GrpcServerAuthzFilter` class, which is a server-side filter that uses an `AuthorizationEngine` to authorize incoming requests.
*   **`rbac_policy.h`, `rbac_policy.cc`**: These files define the `Rbac` struct, which represents an RBAC policy.
*   **`authorization_policy_provider.h`**: Defines the `AuthorizationPolicyProvider` interface.
*   **`grpc_authorization_policy_provider.h`, `grpc_authorization_policy_provider.cc`**: These files define a concrete implementation of the `AuthorizationPolicyProvider` interface that loads the authorization policy from a static string.
*   **`audit_logging.h`, `audit_logging.cc`**: These files define the interfaces for audit logging. Audit logging can be used to record information about authorization decisions.

## Notes

*   The authorization framework is a powerful tool for securing gRPC services. It can be used to implement a wide range of authorization policies, from simple access control lists to complex, attribute-based policies.
*   The framework is designed to be used in conjunction with xDS, which can be used to dynamically update the authorization policy at runtime.
*   The CEL-based authorization engine is still experimental and is not yet recommended for production use.
*   Audit logging can be used to record information about authorization decisions. This can be useful for security auditing and for debugging authorization policies.
