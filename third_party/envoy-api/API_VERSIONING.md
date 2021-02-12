# API versioning guidelines

The Envoy project (and in the future [UDPA](https://github.com/cncf/udpa)) takes API stability and
versioning seriously. Providing stable APIs is a necessary step in ensuring API adoption and success
of the ecosystem. Below we articulate the API versioning guidelines that aim to deliver this
stability.

# API semantic versioning

The Envoy APIs consist of a family of packages, e.g. `envoy.admin.v2alpha`,
`envoy.service.trace.v2`. Each package is independently versioned with a protobuf semantic
versioning scheme based on https://cloud.google.com/apis/design/versioning.

The major version for a package is captured in its name (and directory structure). E.g. version 2
of the tracing API package is named `envoy.service.trace.v2` and its constituent protos are located
in `api/envoy/service/trace/v2`. Every protobuf must live directly in a versioned package namespace,
we do not allow subpackages such as `envoy.service.trace.v2.somethingelse`.

Minor and patch versions will be implemented in the future, this effort is tracked in
https://github.com/envoyproxy/envoy/issues/8416.

In everyday discussion and GitHub labels, we refer to the `v2`, `v3`, `vN`, `...` APIs. This has a
specific technical meaning. Any given message in the Envoy API, e.g. the `Bootstrap` at
`envoy.config.bootstrap.v3.Boostrap`, will transitively reference a number of packages in the Envoy
API. These may be at `vN`, `v(N-1)`, etc. The Envoy API is technically a DAG of versioned package
namespaces. When we talk about the `vN xDS API`, we really refer to the `N` of the root
configuration resources (e.g. bootstrap, xDS resources such as `Cluster`). The
v3 API bootstrap configuration is `envoy.config.bootstrap.v3.Boostrap`, even
though it might might transitively reference `envoy.service.trace.v2`.

# Backwards compatibility

In general, within a package's major API version, we do not allow any breaking changes. The guiding
principle is that neither the wire format nor protobuf compiler generated language bindings should
experience a backward compatible break on a change. Specifically:

* Fields should not be renumbered or have their types changed. This is standard proto development
  procedure.

* Renaming of fields or package namespaces for a proto must not occur. This is inherently dangerous,
  since:
  * Field renames break wire compatibility. This is stricter than standard proto development
    procedure in the sense that it does not break binary wire format. However, it **does** break
    loading of YAML/JSON into protos as well as text protos. Since we consider YAML/JSON to be first
    class inputs, we must not change field names.

  * For service definitions, the gRPC endpoint URL is inferred from package namespace, so this will
    break client/server communication.

  * For a message embedded in an `Any` object, the type URL, which the package namespace is a part
    of, may be used by Envoy or other API consuming code. Currently, this applies to the top-level
    resources embedded in `DiscoveryResponse` objects, e.g. `Cluster`, `Listener`, etc.

  * Consuming code will break and require source code changes to match the API changes.

* Some other changes are considered breaking for Envoy APIs that are usually considered safe in
  terms of protobuf wire compatibility:
  * Upgrading a singleton field to a repeated, e.g. `uint32 foo = 1;` to `repeated uint32 foo = 1`.
    This changes the JSON wire representation and hence is considered a breaking change.

  * Wrapping an existing field with `oneof`. This has no protobuf or JSON/YAML wire implications,
    but is disruptive to various consuming stubs in languages such as Go, creating unnecessary
    churn.

  * Increasing the strictness of
    [protoc-gen-validate](https://github.com/envoyproxy/protoc-gen-validate) annotations. Exceptions
    may be granted for scenarios in which these stricter conditions model behavior already implied
    structurally or by documentation.

An exception to the above policy exists for:
* Changes made within 14 days of the introduction of a new API field or message.
* API versions tagged `vNalpha`. Within an alpha major version, arbitrary breaking changes are allowed.
* Any field, message or enum with a `[#not-implemented-hide:..` comment.
* Any proto with a `(udpa.annotations.file_status).work_in_progress` option annotation.

Note that changes to default values for wrapped types, e.g. `google.protobuf.UInt32Value` are not
governed by the above policy. Any management server requiring stability across Envoy API or
implementations within a major version should set explicit values for these fields.

# API lifecycle

A new major version is a significant event in the xDS API ecosystem, inevitably requiring support
from clients (Envoy, gRPC) and a large number of control planes, ranging from simple in-house custom
management servers to xDS-as-a-service offerings run by vendors. The [xDS API
shepherds](https://github.com/orgs/envoyproxy/teams/api-shepherds) will make the decision to add a
new major version subject to the following constraints:
* There exists sufficient technical debt in the xDS APIs in the existing supported major version
  to justify the cost burden for xDS client/server implementations.
* At least one year has elapsed since the last major version was cut.
* Consultation with the Envoy community (via Envoy community call, `#xds` channel on Slack), as
  well as gRPC OSS community (via reaching out to language maintainers) is made. This is not a veto
  process; the API shepherds retain the right to move forward with a new major API version after
  weighing this input with the first two considerations above.

Following the release of a new major version, the API lifecycle follows a deprecation clock.
Envoy will support at most three major versions of any API package at all times:
* The current stable major version, e.g. v3.
* The previous stable major version, e.g. v2. This is needed to ensure that we provide at least 1
  year for a supported major version to sunset. By supporting two stable major versions
  simultaneously, this makes it easier to coordinate control plane and Envoy
  rollouts as well. This previous stable major version will be supported for exactly 1
  year after the introduction of the new current stable major version, after which it will be
  removed from the Envoy implementation.
* Optionally, the next experimental alpha major version, e.g. v4alpha. This is a release candidate
  for the next stable major version. This is only generated when the current stable major version
  requires a breaking change at the next cycle, e.g. a deprecation or field rename. This release
  candidate is mechanically generated via the
  [protoxform](https://github.com/envoyproxy/envoy/tree/main/tools/protoxform) tool from the
  current stable major version, making use of annotations such as `deprecated = true`. This is not a
  human editable artifact.

An example of how this might play out is that at the end of December in 2020, if a v4 major version
is justified, we might freeze
`envoy.config.bootstrap.v4alpha` and this package would then become the current stable major version
`envoy.config.bootstrap.v4`. The `envoy.config.bootstrap.v3` package will become the previous stable
major version and support for `envoy.config.bootstrap.v2` will be dropped from the Envoy
implementation. Note that some transitively referenced package, e.g.
`envoy.config.filter.network.foo.v2` may remain at version 2 during this release, if no changes were
made to the referenced package. If no major version is justified at this point, the decision to cut
v4 might occur at some point in 2021 or beyond, however v2 support will still be removed at the end
of 2020.

The implication of this API lifecycle and clock is that any deprecated feature in the Envoy API will
retain implementation support for at least 1-2 years.

We are currently working on a strategy to introduce minor versions
(https://github.com/envoyproxy/envoy/issues/8416). This will bump the xDS API minor version on every
deprecation and field introduction/modification. This will provide an opportunity for the control
plane to condition on client and major/minor API version support. Currently under discussion, but
not finalized will be the sunsetting of Envoy client support for deprecated features after a year
of support within a major version. Please post to https://github.com/envoyproxy/envoy/issues/8416
any thoughts around this.

# New API features

The Envoy APIs can be [safely extended](https://cloud.google.com/apis/design/compatibility) with new
packages, messages, enums, fields and enum values, while maintaining [backwards
compatibility](#backwards-compatibility). Additions to the API for a given package should normally
only be made to the *current stable major version*. The rationale for this policy is that:
* The feature is immediately available to Envoy users who consume the current stable major version.
  This would not be the case if the feature was placed in `vNalpha`.
* `vNalpha` can be mechanically generated from `vN` without requiring developers to maintain the new
  feature in both locations.
* We encourage Envoy users to move to the current stable major version from the previous one to
  consume new functionality.

# When can an API change be made to a package's previous stable major version?

As a pragmatic concession, we allow API feature additions to the previous stable major version for a
single quarter following a major API version increment. Any changes to the previous stable major
version must be manually reflected in a consistent manner in the current stable major version as
well.

# How to make a breaking change across major versions

We maintain [backwards compatibility](#backwards-compatibility) within a major version but allow
breaking changes across major versions. This enables API deprecations, cleanups, refactoring and
reorganization. The Envoy APIs have a stylized workflow for achieving this. There are two prescribed
methods, depending on whether the change is mechanical or manual.

## Mechanical breaking changes

Field deprecations, renames, etc. are mechanical changes that are supported by the
[protoxform](https://github.com/envoyproxy/envoy/tree/main/tools/protoxform) tool. These are
guided by [annotations](STYLE.md#api-annotations).

## Manual breaking changes

A manual breaking change is distinct from the mechanical changes such as field deprecation, since in
general it requires new code and tests to be implemented in Envoy by hand. For example, if a developer
wants to unify `HeaderMatcher` with `StringMatcher` in the route configuration, this is a likely
candidate for this class of change. The following steps are required:
1. The new version of the feature, e.g. the `NewHeaderMatcher` message should be added, together
   with referencing fields, in the current stable major version for the route configuration proto.
2. The Envoy implementation should be changed to consume configuration from the fields added in (1).
   Translation code (and tests) should be written to map from the existing field and messages to
   (1).
3. The old message/enum/field/enum value should be annotated as deprecated.
4. At the next major version, `protoxform` will remove the deprecated version automatically.

This make-before-break approach ensures that API major version releases are predictable and
mechanical, and has the bulk of the Envoy code and test changes owned by feature developers, rather
than the API owners. There will be no major `vN` initiative to address technical debt beyond that
enabled by the above process.

# Client features

Not all clients will support all fields and features in a given major API version. In general, it is
preferable to use Protobuf semantics to support this, for example:
* Ignoring a field's contents is sufficient to indicate that the support is missing in a client.
* Setting both deprecated and the new method for expressing a field if support for a range of
  clients is desired (where this does not involve huge overhead or gymnastics).

This approach does not always work, for example:
* A route matcher conjunct condition should not be ignored just because the client is missing the
  ability to implement the match; this might result in route policy bypass.
* A client may expect the server to provide a response in a certain format or encoding, for example
  a JSON encoded `Struct`-in-`Any` representation of opaque extension configuration.

For this purpose, we have [client
features](https://www.envoyproxy.io/docs/envoy/latest/api/client_features).

# One Definition Rule (ODR)

To avoid maintaining more than two stable major versions of a package, and to cope with diamond
dependency, we add a restriction on how packages may be referenced transitively; a package may have
at most one version of another package in its transitive dependency set. This implies that some
packages will have a major version bump during a release cycle simply to allow them to catch up to
the current stable version of their dependencies.

Some of this complexity and churn can be avoided by having strict rules on how packages may
reference each other. Package organization and `BUILD` visibility constraints should be used
restrictions to maintain a shallow depth in the dependency tree for any given package.

# Minimizing the impact of churn

In addition to stability, the API versioning policy has an explicit goal of minimizing the developer
overhead for the Envoy community, other clients of the APIs (e.g. gRPC), management server vendors
and the wider API tooling ecosystem. A certain amount of API churn between major versions is
desirable to reduce technical debt and to support API evolution, but too much creates costs and
barriers to upgrade.

We consider deprecations to be *mandatory changes*. Any deprecation will be removed at the next
stable API version.

Other mechanical breaking changes are considered *discretionary*. These include changes such as
field renames and are largely reflected in protobuf comments. The `protoxform` tool may decide to
minimize API churn by deferring application of discretionary changes until a major version cycle
where the respective message is undergoing a mandatory change.

The Envoy API structure helps with minimizing churn between versions. Developers should architect
and split packages such that high churn protos, e.g. HTTP connection manager, are isolated in
packages and have a shallow reference hierarchy.
