[TOC]

# Introduction

The RuntimeConfig service provides Google Cloud Platform users the ability to
dynamically configure your service.

The RuntimConfig service creates and manages RuntimeConfig resources
within a Google Cloud Project and various variables within said resource.

## Details

Each cloud project can create multiple **Config** objects. A **Config** object
by itself does not contain any configuration information, but rather is a
logical grouping of variables. Variable names are hierarchical and follow file
system style, where only leaf nodes can contain values.

For example, you can have a configuration called *Flags*. Within that
configuration object, you can create the following variables.

* `/ports/service_port`
* `/ports/monitoring_port`
* `/ports/admin_port`

This creates three variables: `/ports/serve_port`, `/ports/monitoring_port`,
`/ports/admin_port`. Note that `/ports` cannot have a value but it can be
listed.

### Setup

In order to make requests to RuntimeConfig service, you need to enable the API
for your project.

To achieve that, go to the
[Google Cloud Console](https://console.cloud.google.com/apis/api/runtimeconfig.googleapis.com/overview)
and enable *Google Cloud RuntimeConfig API* for your project.

The documentation for this service is located
[here](https://cloud.google.com/deployment-manager/runtime-configurator/reference/rest/).
