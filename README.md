# xDS API Working Group (xDS-WG)

# Goal

The objective of the xDS API Working Group (xDS-WG) is to bring together parties
across the industry interested in a common control and configuration API for
data plane proxies and load balancers, based on the xDS APIs.

# Vision

The xDS vision is one of a universal data plane API, articulated at
[https://blog.envoyproxy.io/the-universal-data-plane-api-d15cec7a](https://blog.envoyproxy.io/the-universal-data-plane-api-d15cec7a).
xDS aims to provide a set of APIs that provide the de facto standard for L4/L7
data plane configuration, similar to the role played by OpenFlow at L2/L3/L4 in
SDN.

The [existing Envoy xDS
APIs](https://github.com/envoyproxy/envoy/tree/master/api) constitute the basis
for this vision and will incrementally evolve towards supporting a goal of
client neutrality. We will evolve the xDS APIs to support additional clients,
for example data plane proxies beyond Envoy, proxyless service mesh libraries,
hardware load balancers, mobile clients and beyond. We will strive to be vendor
and implementation agnostic to the degree possible while not regressing on
support for data plane components that have committed to xDS in production
(Envoy & gRPC to date).

The xDS APIs have two delineated aspects, a transport protocol and data model,
The xDS transport protocol provides a low latency versioned streaming gRPC
delivery of xDS resources. The data model covers common data plane concerns such
as service discovery, load balancing assignments, routing discovery, listener
configuration, secret discovery, load reporting, health check delegation, etc.

# Repository structure

The xDS APIs are split between this repository and
https://github.com/envoyproxy/envoy/tree/master/api. Our long-term goal is to
move the entire API to this repository, this will be done opportunistically over
time as we generalize parts of the API to be less client-specific.

# Mailing list and meetings

We have an open mailing list [xds-wg@lists.cncf.io](https://lists.cncf.io/g/xds-wg/) for communication and announcements. We also meet
on an ad hoc basis via Zoom.

To monitor activity, you can either subscribe to a GitHub watch on this repository or join the [@cncf/xds-wg](https://github.com/orgs/cncf/teams/xds-wg) team for
tagging on key PRs and RFCs.
