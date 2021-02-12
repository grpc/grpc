Google Service Management manages a set of *services*. Service
Management allows *service producers* to
publish their services on Google Cloud Platform so that they can be discovered
and used by *service consumers*. It also handles the tasks of tracking
service lifecycle and programming various backend systems -- such as
[Stackdriver Logging](https://cloud.google.com/stackdriver),
[Stackdriver Monitoring](https://cloud.google.com/stackdriver) -- to support
the managed services.

If you are a service producer, you can use the Google Service Management API
and [Google Cloud SDK (gcloud)](/sdk) to publish and manage your services.
Each managed service has a service configuration which declares various aspects
of the service such as its API surface, along with parameters to configure the
supporting backend
systems, such as logging and monitoring. If you build your service using
[Google Cloud Endpoints](https://cloud.google.com/endpoints/), the service
configuration will be handled automatically.

If you are a service consumer and want to use a managed service, you can use the
Google Service Management API or [Google Cloud Console](https://console.cloud.google.com)
to activate the
service for your [Google developer project](https://developers.google.com/console/help/new/),
then start using its APIs and functions.

## Managed services

REST URL: `https://servicemanagement.googleapis.com/v1/services/{service-name}` <br />
REST schema is defined [here](/service-management/reference/rest/v1/services).

A managed service refers to a network service managed by
Service Management. Each managed service has a unique name, such as
`example.googleapis.com`, which must be a valid fully-qualified DNS name, as per
RFC 1035.

A managed service typically provides some REST APIs and/or other
functions to their service consumers, such as mobile apps or cloud services.

Service producers can use methods, such as
[services.create](/service-management/reference/rest/v1/services/create),
[services.delete](/service-management/reference/rest/v1/services/delete),
[services.undelete](/service-management/reference/rest/v1/services/undelete),
to manipulate their managed services.

## Service producers

A service producer is the Google developer project responsible for publishing
and maintaining a managed service. Each managed service is owned by exactly one
service producer.

## Service consumers

A service consumer is a Google developer project that has enabled and can
invoke APIs on a managed service. A managed service can have many service
consumers.

## Service configuration

REST URL: `https://servicemanagement.googleapis.com/v1/services/{service-name}/configs/{config_id}` <br />
REST schema is defined [here](/service-management/reference/rest/v1/services.configs).

Each managed service is described by a service configuration which covers a wide
range of features, including its name, title, RPC API definitions,
REST API definitions, documentation, authentication, and more.

To change the configuration of a managed service, the service producer needs to
publish an updated service configuration to Service Management.
Service Management keeps a history of published
service configurations, making it possible to easily retrace how a service's
configuration evolved over time. Service configurations can be published using
the
[services.configs.create](/service-management/reference/rest/v1/services.configs/create)
or [services.configs.submit](/service-management/reference/rest/v1/services.configs/submit)
methods.

Alternatively, `services.configs.submit` allows publishing an
[OpenAPI](https://github.com/OAI/OpenAPI-Specification) specification, formerly
known as the Swagger Specification, which is automatically converted to a
corresponding service configuration.

## Service rollout

REST URL: `https://servicemanagement.googleapis.com/v1/services/{service-name}/rollouts/{rollout-id}` <br />
REST schema is defined [here](/service-management/reference/rest/v1/services.rollouts).

A `Rollout` defines how Google Service Management should deploy service
configurations to backend systems and how the configurations take effect at
runtime. It lets service producers specify multiple service configuration
versions to be deployed together, and a strategy that indicates how they
should be used.

Updating a managed service's configuration can be dangerous, as a configuration
error can lead to a service outage. To mitigate risks, Service Management
supports gradual rollout of service configuration changes. This feature gives
service producers time to identity potential issues and rollback service
configuration changes in case of errors, thus minimizing the customer
impact of bad configurations. For example, you could specify that 5% of traffic
uses configuration 1, while the remaining 95% uses configuration 2.

Service Management keeps a history of rollouts so that service
producers can undo to previous configuration versions. You can rollback a configuration
by initiating a new `Rollout` that clones a previously submitted
rollout record.