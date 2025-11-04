# Server Load Reporting Filter

This directory contains a server-side filter for reporting load data to a load balancer.

See also: [gRPC Filters overview](../GEMINI.md)

## Overarching Purpose

The server load reporting filter provides a mechanism for a gRPC server to report load data to a load balancer. This data can be used by the load balancer to make more intelligent load balancing decisions, such as routing requests to the least loaded backend.

## How it Works

1.  The filter is added to the server's channel stack.
2.  When a request is received, the filter extracts a load balancing token from the initial metadata of the request. This token is used to associate the load report with a specific backend.
3.  The filter records various metrics throughout the lifecycle of the call, such as the number of bytes sent and received, and the call latency.
4.  When the call is complete, the filter can extract custom cost metrics from the trailing metadata of the response.
5.  The filter then uses OpenCensus to record the metrics. The metrics are tagged with the load balancing token, which allows the load balancer to correlate the metrics with the backend that handled the request.

## Files

*   `server_load_reporting_filter.h`, `server_load_reporting_filter.cc`: These files define the `ServerLoadReportingFilter` class, a server-side filter that reports load data to a load balancer.
*   `registered_opencensus_objects.h`: This file defines the OpenCensus measures and tags that are used by the filter.

## Notes

*   This filter is an important component of gRPC's load balancing story, as it enables backends to provide real-time load information to load balancers.
*   The filter is designed to be used with the `grpclb` load balancing policy, but it can be used with any load balancing policy that supports load reporting.
*   The filter uses OpenCensus to record metrics. This means that the metrics can be exported to a variety of monitoring systems, such as Prometheus and Stackdriver.
