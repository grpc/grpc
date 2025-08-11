# GCP Authentication Filter

This directory contains a client-side filter for adding GCP authentication to gRPC calls.

## Overarching Purpose

The GCP authentication filter provides a mechanism for authenticating to Google Cloud services using service account credentials. It is designed to be used in conjunction with xDS for service discovery and configuration.

## Files

*   `gcp_authentication_filter.h`, `gcp_authentication_filter.cc`: These files define the `GcpAuthenticationFilter` class, a client-side filter that adds GCP authentication to outgoing requests.
*   `gcp_authentication_service_config_parser.h`, `gcp_authentication_service_config_parser.cc`: These files define the `GcpAuthenticationServiceConfigParser` class, which is responsible for parsing the GCP authentication configuration from the service config.

## Major Classes

*   `grpc_core::GcpAuthenticationFilter`: The channel filter implementation.
*   `grpc_core::GcpAuthenticationFilter::CallCredentialsCache`: A cache for storing `grpc_call_credentials` objects.
*   `grpc_core::GcpAuthenticationServiceConfigParser`: The service config parser for the GCP authentication configuration.

## Notes

*   This filter is tightly coupled with xDS. It relies on the `XdsConfig` to determine the audience for GCP authentication based on the cluster that the call is being sent to.
*   The filter uses a cache to store `grpc_call_credentials` objects, which helps to improve performance by avoiding the need to create new credentials for each call.
*   This filter is an essential component for any gRPC application that needs to securely communicate with Google Cloud services.
