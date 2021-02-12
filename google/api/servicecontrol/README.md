Google Service Control provides control plane functionality to managed services,
such as logging, monitoring, and status checks. This page provides an overview
of what it does and how it works.

## Why use Service Control?

When you develop a cloud service, you typically start with the business
requirements and the architecture design, then proceed with API definition
and implementation. Before you put your service into production, you
need to deal with many control plane issues:

* How to control access to your service.
* How to send logging and monitoring data to both consumers and producers.
* How to create and manage dashboards to visualize this data.
* How to automatically scale the control plane components with your service.

Service Control is a mature and feature-rich control plane provider
that addresses these needs with high efficiency, high scalability,
and high availability. It provides a simple public API that can be accessed
from anywhere using JSON REST and gRPC clients, so when you move your service
from on-premise to a cloud provider, or from one cloud provider to another,
you don't need to change the control plane provider.

Services built using Google Cloud Endpoints already take advantage of
Service Control. Cloud Endpoints sends logging and monitoring data
through Google Service Control for every request arriving at its
proxy. If you need to report any additional logging and monitoring data for
your Cloud Endpoints service, you can call the Service Control API directly
from your service.

The Service Control API definition is open sourced and available on
[GitHub](https://github.com/googleapis/googleapis/tree/master/google/api/servicecontrol).
By changing the DNS name, you can easily use alternative implementations of
the Service Control API.

## Architecture

Google Service Control works with a set of *managed services* and their
*operations* (activities), *checks* whether an operation is allowed to proceed,
and *reports* completed operations. Behind the scenes, it leverages other
Google Cloud services, such as
[Google Service Management](/service-management),
[Stackdriver Logging](/logging), and [Stackdriver Monitoring](/monitoring),
while hiding their complexity from service producers. It enables service
producers to send telemetry data to their consumers. It uses caching,
batching, aggregation, and retries to deliver higher performance and
availability than the individual backend systems it encapsulates.

<figure id="fig-arch" class="center">
<div style="width: 70%;margin: auto">
  <img src="/service-control/images/arch.svg"
    alt="The overall architecture of a service that uses Google Service Control.">
</div>
<figcaption><b>Figure 1</b>: Using Google Service Control.</figcaption>
</figure>

The Service Control API provides two methods:

* [`services.check`](/service-control/reference/rest/v1/services/check), used for:
    * Ensuring valid consumer status
    * Validating API keys
* [`services.report`](/service-control/reference/rest/v1/services/report), used for:
    * Sending logs to Stackdriver Logging
    * Sending metrics to Stackdriver Monitoring

We’ll look at these in more detail in the rest of this overview.

## Managed services

A [managed service](/service-management/reference/rest/v1/services) is
a network service managed by
[Google Service Management](/service-management). Each managed service has a
unique name, such as `example.googleapis.com`, which must be a valid
fully-qualified DNS name, as per RFC 1035.

For example:

* Google Cloud Pub/Sub (`pubsub.googleapis.com`)
* Google Cloud Vision (`vision.googleapis.com`)
* Google Cloud Bigtable (`bigtable.googleapis.com`)
* Google Cloud Datastore (`datastore.googleapis.com`)

Google Service Management manages the lifecycle of each service’s
configuration, which is used to customize Google Service Control's behavior.
Service configurations are also used by Google Cloud Console
for displaying APIs and their settings, enabling/disabling APIs, and more.

## Operations

Google Service Control uses the generic concept of an *operation*
to represent the
activities of a managed service, such as API calls and resource usage. Each
operation is associated with a managed service and a specific service
consumer, and has a set of properties that describe the operation, such as
the API method name and resource usage amount. For more information, see the
[Operation definition](/service-control/rest/v1/Operation).

## Check

The [`services.check`](/service-control/reference/rest/v1/services/check)
method determines whether an operation should be allowed to proceed
for a managed service.

For example:

* Check if the consumer is still active.
* Check if the consumer has enabled the service.
* Check if the API key is still valid.

By performing multiple checks within a single method call, it provides
better performance, higher reliability, and reduced development cost to
service producers compared to checking with multiple backend systems.

## Report

The [`services.report`](/service-control/reference/rest/v1/services/report)
method reports completed operations for
a managed service to backend systems, such as logging and monitoring. The
reported data can be seen in Google API Console and Google Cloud Console,
and retrieved with appropriate APIs, such as the Stackdriver Logging and
Stackdriver Monitoring APIs.

## Next steps

* Read our [Getting Started guide](/service-control/getting-started) to find out
  how to set up and use the Google Service Control API.
