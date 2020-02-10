# gRPC Versioning Guide

## Versioning Overview

All gRPC implementations use a three-part version number (`vX.Y.Z`) and follow [semantic versioning](https://semver.org/), which defines the semantics of major, minor and patch components of the version number. In addition to that, gRPC versions evolve according to these rules:
- **Major version bumps** only happen on rare occasions. In order to qualify for a major version bump, certain criteria described later in this document need to be met. Most importantly, a major version increase must not break wire compatibility with other gRPC implementations so that existing gRPC libraries remain fully interoperable.
- **Minor version bumps** happen approx. every 6 weeks as part of the normal release cycle as defined by the gRPC release process. A new release branch named vMAJOR.MINOR.PATCH) is cut every 6 weeks based on the [release schedule](https://github.com/grpc/grpc/blob/master/doc/grpc_release_schedule.md).
- **Patch version bump** corresponds to bugfixes done on release branch.

There are a few situations where we don't adhere to the Semantic Versioning 2.0.0 strictly:
- A **minor** version will not necessarily add new functionality. This follows from the fact that we cut minor releases on a regular schedule, so we can't guarantee there will always be new features in each of the supported languages.
- Backward compatibility can be broken by a **minor** release if the API affected by the change was marked as EXPERIMENTAL upon its introduction.

There are also a few extra rules regarding adding new gRPC implementations (e.g. adding support for a new language)
- New implementations start at v0.x.y version and until they reach 1.0, they are considered not ready for production workloads. Breaking API changes are allowed in the 0.x releases as the library is not considered stable yet.
- The "1.0" release has semantics of GA (generally available) and being production ready. Requirements to reach this milestone are at least these
  - basic RPC features are feature complete and tested
  - implementation is tested for interoperability with other languages
  - Public API is declared stable
- Once a gRPC library reaches 1.0 (or higher version), the normal rules for versioning apply.

## Policy for updating the major version number

To avoid user confusion and simplify reasoning, the gRPC releases in different languages try to stay synchronized in terms of major and minor version (all languages follow the same release schedule). Nevertheless, because we also strictly follow semantic versioning, there are circumstances in which a gRPC implementation needs to break the version synchronicity and do a major version bump independently of other languages.

### Situations when it's ok to do a major version bump
- **change forced by the language ecosystem:** when the language itself or its standard libraries that we depend on make a breaking change (something which is out of our control), reacting with updating gRPC APIs may be the only adequate response.
- **voluntary change:** Even in non-forced situations, there might be circumstances in which a breaking API change makes sense and represents a net win, but as a rule of thumb breaking changes are very disruptive for users, cause user fragmentation and incur high maintenance costs. Therefore, breaking API changes should be very rare events that need to be considered with extreme care and the bar for accepting such changes is intentionally set very high.
  Example scenarios where a breaking API change might be adequate:
  - fixing a security problem which requires changes to API (need to consider the non-breaking alternatives first)
  - the change leads to very significant gains to security, usability or development velocity. These gains need to be clearly documented and claims need to be supported by evidence (ideally by numbers). Costs to the ecosystem (impact on users, dev team etc.) need to be taken into account and the change still needs to be a net positive after subtracting the costs.

  All proposals to make a breaking change need to be documented as a gRFC document (in the grpc/proposal repository) that covers at least these areas:
  - Description of the proposal including an explanation why the proposed change is one of the very rare events where a breaking change is introduced.
  - Migration costs (= what does it mean for the users to migrate to the new API, what are the costs and risks associated with it)
  - Pros of the change (what is gained and how)
  - Cons of the change (e.g. user confusion, lost users and user trust, work needed, added maintenance costs)
  - Plan for supporting users still using the old major version (in case migration to the new major version is not trivial or not everyone can migrate easily)

Note that while major version bump allows changing APIs used by the users, it must not impact the interoperability of the implementation with other gRPC implementations and the previous major version released. That means that **no backward incompatible protocol changes are allowed**: old clients must continue interoperating correctly with new servers and new servers with old clients.

### Situations that DON'T warrant a major version bump
- Because other languages do so. This is not a good enough reason because
doing a major version bump has high potential for disturbing and confusing the users of that language and fragmenting the user base and that is a bigger threat than having language implementations at different major version (provided the state is well documented). Having some languages at different major version seems to be unavoidable anyway (due to forced version bumps), unless we bump some languages artificially.
- "I don't like this API": In retrospect, some API decisions made in the past necessarily turn out more lucky than others, but without strong reasons that would be in favor of changing the API and without enough supporting evidence (see previous section), other strategy than making a breaking API change needs to be used. Possible options: Expand the API to make it useful again; mark API as deprecated while keeping its functionality and providing a new better API.
