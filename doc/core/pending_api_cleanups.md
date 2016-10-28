There are times when we make changes that include a temporary shim for
backward-compatibility (e.g., a macro or some other function to preserve
the original API) to avoid having to bump the major version number in
the next release.  However, when we do eventually want to release a
feature that does change the API in a non-backward-compatible way, we
will wind up bumping the major version number anyway, at which point we
can take the opportunity to clean up any pending backward-compatibility
shims.

This file lists all pending backward-compatibility changes that should
be cleaned up the next time we are going to bump the major version
number:

- remove `GRPC_ARG_MAX_MESSAGE_LENGTH` channel arg from
  `include/grpc/impl/codegen/grpc_types.h` (commit `af00d8b`)
- remove `ServerBuilder::SetMaxMessageSize()` method from
  `include/grpc++/server_builder.h` (commit `6980362`)
- remove `GRPC_INITIAL_METADATA_IGNORE_CONNECTIVITY` macro from
  `include/grpc/impl/codegen/grpc_types.h` (commit `59c9f90`)
