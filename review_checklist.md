# API Review Checklist

This checklist is intended to be used when reviewing xDS API changes.
Users who wish to contribute API changes should read this and proactively
consider the answers to these questions before sending a PR.

## Feature Enablement
- Are the default values going to cause behavior changes for existing users
  who do not know about the change and have not updated the resources being
  served by their control plane?
  - If yes, do we have some estimate of how many users will be affected?
  - Why is it justified to change the default behavior, rather than making
    this feature opt-in?
    - Some possible justifications include security concerns with existing
      behavior, or a desire to eliminate legacy behavior.
  - What is the plan to make this change in a safe way?  For example, is the
    transition going to be staged over the course of several minor xDS versions?
  - How will we warn users about this change?
    - Possible ways to do this include release notes, announcements, warnings
      from the code, etc.
- Will users have a way to disable this change if it causes problems?
  - If not, why do we think that's okay?  (It might be the case that we think
    it will not actually affect anyone, or no one will care.)
  - If so, is the mechanism to disable it part of the xDS API, or is it
    acceptable to have a separate knob for this in the client?  (See also
    "Genericness" below -- if this is not part of the API, will every xDS
    client need to add a different knob?  Is consistency across clients
    important for this?)
- If the feature is modeled as a proto3 scalar, is it plausible that its
  default value may change in the future? If so, it should be wrapped with
  a Well-Known Type (WKT), e.g. `bool` becomes `google.protobuf.BoolValue`.

## Style
- Is the PR aligned with the [API style guidelines](STYLE.md)?

## Validation Rules
- Does the field have protocgen-validate rules to enforce constraints on
  its value?
- Is the field mandatory? Does it have the required rule?
- Is the field numeric? Is it bounded correctly?
- Is the field repeated? Does it have the correct min/max items numbers? Are
  the values unique?
- If a field may eventually be repeated but initially there is a desire to
  cap it to a single value, consider using repeated with a max length of 1,
  which is easier to relax in the future.

## Deprecations
- When a field or enum value is deprecated, according to the minor/patch
  versioning policy this implies a minor version for support removal. Is the
  work necessary to add support for the newer replacement field acceptable to
  known xDS clients in this time frame?
- No deprecations are allowed when an alternative is "not ready yet" in any
  major known xDS client (Envoy or gRPC at this point), unless the
  maintainers of that xDS client have signed off on the change. If you are not
  sure about the current state of a feature in the major known xDS clients,
  ask one of the API shepherds.
- Is this deprecated field documented in a way that points to the preferred
  newer method of configuration?

## Extensibility
- Is this feature being directly added into the API itself, or is it being
  introduced via an extension point (i.e., using `TypedExtensionConfig`)?
- If not via an extension point, why not?
- If no appropriate extension point currently exists, is this a good
  opportunity to add one?  Can we move some existing "core" functionality
  into a set of standardized plugins for an extension point?
- Do we have good documentation showing what to plug into the extension point?
  (At minimum, it should have a comment pointing to the common protos to
  be plugged into the extension point.)
- If an enum is being introduced, should this be a oneof with empty messages
  for future API growth?
- When a new field is introduced for a distinct purpose, should this be a
  message to allow for future API growth?

## Consistency
- Can the proposed API reuse part or all of other APIs?
  - Can some other API be refactored to be part of it, or vice versa?
  - Example: Can it use common types such as matcher or number?
- Are there similar structures that already exist?
- Is the naming convention consistent with other APIs?
- If there are new proto messages being added, are they in the right
  place in the tree? Consider not just the current users of this proto
  but also other possible uses in the future. Would it make more sense
  to make the proto a generic type and put it in a common location, so
  that it can be used in other places in the future?

## Interactions With Other Features
- Will this feature interact in strange ways with other features, either
  existing or planned?
  - For example, if you are defining a new cluster type, how will the
    new type implement all of the features currently configured via CDS?
  - If this is a change in the upstream side of the API, will it work properly
    with LRS load reporting?
- Will there be combinations of features that won't work properly?  If so,
  please document each combination that won't work and justify why this is
  okay. Is there some other way to structure this feature that would not
  cause conflicts with other features?
- If this change involves extension configuration, how will it interact
  with ECDS?

## Genericness
- Is this an Envoy-specific or proxy-specific feature? How will it apply to
  xDS clients other than Envoy (e.g., gRPC)?

## Dependencies
- Does this feature pull in any new dependencies for clients?
- Are these dependencies optional or required?
- Will the dependencies cause problems for any known xDS clients (e.g.,
  [Envoy's dependency policy](https://github.com/envoyproxy/envoy/blob/master/DEPENDENCY_POLICY.md))?

## Failure Modes
- What is the failure mode if this feature is configured but is not working
  for some reason?
- Is the failure mode what users will expect/want?
- Is this failure mode specified in the API, or is each client expected to
  handle it on its own?  Consistency across clients is preferred; if there's
  a reason this isn't feasible, please explain.

## Scalability
- Does this feature add new per-request functionality?  How much overhead does
  it add on the per-request path?
- Are there ways that the API could be structured differently to make it
  possible to implement the feature in more efficient ways?  (Even if this
  efficiency is not needed now, it may be something we will need in the future,
  and we will save pain in the long run if we structure the API in a way that
  gives us the flexibility to change the implementation later.)
- How does this feature affect config size? For example, instead of
  adding a huge mandatory proto to every single route entry, consider
  ways of setting it at the virtual host level and then overriding only
  the parts that change on a per-route basis.
- Will the change require multiple round trips via the REST API?

## Monitoring
- Is there any behavior associated with this feature that will require
  monitoring?
- How will the data be exposed to monitoring?
- Is the monitoring configuration part of the xDS API, or is it client-specific?

## Documentation
- Can a user look at the docs and understand it without a bunch of extra
  context?
- Pay special attention to documentation around extensions and `typed_config`.
  Users generally find this extremely confusing. There should be examples
  showing how to configure extension points and optimally all known public
  extensions (there is tooling work in progress to automate this).
- Larger features should contain architecture overview documentation with
  relevant cross-linking.
- Relevant differences between clients need to be documented (in the future
  we will build tooling to allow for common documentation as well as per-client
  documentation).

## Where to Put New Protos
- The xDS API is currently partly in the Envoy repo and partly in the
  cncf/xds repo. We will move pieces of the API to the latter repo
  slowly over time, as they become less Envoy-specific, until eventually
  the whole API has been moved there. If your change involves adding
  new protos, they should generally go in the new repo.
