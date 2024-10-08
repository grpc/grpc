Support for resolving the scheme used by binder transport implementation.

The URI's authority is required to be empty.

The path is used as the identifiers of endpoint binder objects and the length
limit of the identifier is the same as unix socket length limit.

The length limit of the path should at least be 100 characters long. This is
guaranteed by `static_assert` in the implementation.
