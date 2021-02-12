Protocol buffer definitions for xDS and top-level resource API messages.

Package group `//envoy/api/v2:friends` enumerates all consumers of the shared
API messages. That includes package envoy.api.v2 itself, which contains several
xDS definitions. Default visibility for all shared definitions should be set to
`//envoy/api/v2:friends`.

Additionally, packages envoy.api.v2.core and envoy.api.v2.auth are also
consumed throughout the subpackages of `//envoy/api/v2`.
