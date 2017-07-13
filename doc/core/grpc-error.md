# gRPC Error

## Background

`grpc_error` is the c-core's opaque representation of an error. It holds a
collection of integers, strings, timestamps, and child errors that related to
the final error.

always present are:

*   GRPC_ERROR_STR_FILE and GRPC_ERROR_INT_FILE_LINE - the source location where
    the error was generated
*   GRPC_ERROR_STR_DESCRIPTION - a human readable description of the error
*   GRPC_ERROR_TIME_CREATED - a timestamp indicating when the error happened

An error can also have children; these are other errors that are believed to
have contributed to this one. By accumulating children, we can begin to root
cause high level failures from low level failures, without having to derive
execution paths from log lines.

grpc_errors are refcounted objects, which means they need strict ownership
semantics. An extra ref on an error can cause a memory leak, and a missing ref
can cause a crash.

This document serves as a detailed overview of grpc_error's ownership rules. It
should help people use the errors, as well as help people debug refcount related
errors.

## Clarification of Ownership

If a particular function is said to "own" an error, that means it has the
responsibility of calling unref on the error. A function may have access to an
error without ownership of it.

This means the function may use the error, but must not call unref on it, since
that will be done elsewhere in the code. A function that does not own an error
may explicitly take ownership of it by manually calling GRPC_ERROR_REF.

## Ownership Rules

There are three rules of error ownership, which we will go over in detail.

*   If `grpc_error` is returned by a function, the caller owns a ref to that
    instance.
*   If a `grpc_error` is passed to a `grpc_closure` callback function, then that
    function does not own a ref to the error.
*   if a `grpc_error` is passed to *any other function*, then that function
    takes ownership of the error.

### Rule 1

> If `grpc_error` is returned by a function, the caller owns a ref to that
> instance.*

For example, in the following code block, error1 and error2 are owned by the
current function.

```C
grpc_error* error1 = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Some error occured");
grpc_error* error2 = some_operation_that_might_fail(...);
```

The current function would have to explicitly call GRPC_ERROR_UNREF on the
errors, or pass them along to a function that would take over the ownership.

### Rule 2

> If a `grpc_error` is passed to a `grpc_closure` callback function, then that
> function does not own a ref to the error.

A `grpc_closure` callback function is any function that has the signature:

```C
void (*cb)(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error);
```

This means that the error ownership is NOT transferred when a functions calls:

```C
c->cb(exec_ctx, c->cb_arg, err);
```

The caller is still responsible for unref-ing the error.

However, the above line is currently being phased out! It is safer to invoke
callbacks with `GRPC_CLOSURE_RUN` and `GRPC_CLOSURE_SCHED`. These functions are
not callbacks, so they will take ownership of the error passed to them.

```C
grpc_error* error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Some error occured");
GRPC_CLOSURE_RUN(exec_ctx, cb, error);
// current function no longer has ownership of the error
```

If you schedule or run a closure, but still need ownership of the error, then
you must explicitly take a reference.

```C
grpc_error* error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Some error occured");
GRPC_CLOSURE_RUN(exec_ctx, cb, GRPC_ERROR_REF(error));
// do some other things with the error
GRPC_ERROR_UNREF(error);
```

Rule 2 is more important to keep in mind when **implementing** `grpc_closure`
callback functions. You must keep in mind that you do not own the error, and
must not unref it. More importantly, you cannot pass it to any function that
would take ownership of the error, without explicitly taking ownership yourself.
For example:

```C
void on_some_action(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {
  // this would cause a crash, because some_function will unref the error,
  // and the caller of this callback will also unref it.
  some_function(error);

  // this callback function must take ownership, so it can give that
  // ownership to the function it is calling.
  some_function(GRPC_ERROR_REF(error));
}
```

### Rule 3

> if a `grpc_error` is passed to *any other function*, then that function takes
> ownership of the error.

Take the following example:

```C
grpc_error* error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Some error occured");
// do some things
some_function(error);
// can't use error anymore! might be gone.
```

When some_function is called, it takes over the ownership of the error, and it
will eventually unref it. So the caller can no longer safely use the error.

If the caller needed to keep using the error (or passing it to other functions),
if would have to take on a reference to it. This is a common pattern seen.

```C
void func() {
  grpc_error* error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Some error");
  some_function(GRPC_ERROR_REF(error));
  // do things
  some_other_function(GRPC_ERROR_REF(error));
  // do more things
  some_last_function(error);
}
```

The last call takes ownership and will eventually give the error its final
unref.

When **implementing** a function that takes an error (and is not a
`grpc_closure` callback function), you must ensure the error is unref-ed either
by doing it explicitly with GRPC_ERROR_UNREF, or by passing the error to a
function that takes over the ownership.
