# RBAC Filter

This directory contains a server-side filter for enforcing Role-Based Access Control (RBAC) policies.

## Overarching Purpose

The RBAC filter provides a mechanism for authorizing incoming requests based on a set of rules defined in an RBAC policy. This can be used to control access to gRPC services and methods based on the authenticated user's identity and other request metadata. The filter is designed to be used with xDS, which can be used to dynamically update the RBAC policy at runtime.

## Files

*   `rbac_filter.h`, `rbac_filter.cc`: These files define the `RbacFilter` class, a server-side filter that enforces RBAC policies on incoming requests. The filter fetches the RBAC policy from the filter config, and then uses the `GrpcAuthorizationEngine` to evaluate the policy against the request.

## Major Classes

*   `grpc_core::RbacFilter`: The channel filter implementation. This filter is responsible for enforcing the RBAC policy.

## Notes

*   The RBAC filter is a powerful tool for securing gRPC services. It can be used to implement a wide range of authorization policies, from simple access control lists to complex, attribute-based policies.
*   This filter is an important component of gRPC's security story, as it provides a flexible and extensible way to control access to gRPC services.
