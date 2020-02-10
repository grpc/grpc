# gRPC Security Audit

A third-party security audit of gRPC C++ stack was performed by [Cure53](https://cure53.de) in October 2019. The full report can be found [here](https://github.com/grpc/grpc/tree/master/doc/grpc_security_audit.pdf).

# Addressing grpc_security_audit

The following describes how gRPC team has or will address each of the security issues pointed out in the report.

## GRP-01-001 DoS through uninitialized pointer dereference

GRP-01-001 was fixed in version 1.24.0 and above with https://github.com/grpc/grpc/pull/20351. The fix was also patched in version 1.23.1.

## GRP-01-002 Refs to freed memory not automatically nulled
GRP-01-002 describes a programming pattern in gRPC Core where `gpr_free` is called and then the pointer is nulled afterwards. GRP-01-002 can be split into two concerns: 1) dangling pointer bugs and 2) the potential vulnerability of leveraging other bugs to access data through a freed pointer.

Regarding 1), gRPC uses a suite of sanitizer tests (asan, tsan, etc) to detect and fix any memory-related bugs. gRPC is also in the process of moving to c++ and the standard library, enabling the use of smart pointers in Core and thus making it harder to generate memory-related bugs. There are also plans to remove `gpr_free` in general.

Regarding 2), moving to smart pointers (in particular, unique_ptr) will help this issue as well. In addition, gRPC has continuous fuzzing tests to find and resolve security issues, and the pen test did not discover any concrete vulnerabilities in this area.

Below is a list of alternatives that gRPC team considered.


### Alternative #1: Rewrite gpr_free to take void\*\*
One solution is to change the API of `gpr_free` so that it automatically nulls the given pointer after freeing it.

```
gpr_free (void** ptr) {
  ...
  *ptr = nullptr;
}
```

This defensive programming pattern would help protect gRPC from the potential exploits and latent dangling pointer bugs mentioned in the security report.

However, performance would be a significant concern as we are now unconditionally adding a store to every gpr_free call, and there are potentially hundreds of these per RPC. At the RPC layer, this can add up to prohibitive costs.

Maintainability is also an issue since this approach impacts use of `*const`. Member pointers that are set in the initialization list of a constructor and not changed thereafter can be declared `*const`. This is a useful compile-time check if the member is taking ownership of something that was passed in by argument or allocated through a helper function called by the constructor initializer list. If this thing needs to be `gpr_free`'d using the proposed syntax, it can no longer be `*const` and we lose these checks (or we have to const_cast it which is also error-prone).

Another concern is readability - this `gpr_free` interface is less intuitive than the current one.

Yet another concern is that the use of non-smart pointers doesn’t imply ownership - it doesn’t protect against spare copies of the same pointers.

### Alternative #2: Add another gpr_free to the Core API
Adding an alternative `gpr_free` that nulls the given pointer is undesirable because we cannot enforce that we’re using this version of `gpr_free` everywhere we need to. It doesn’t solve the original problem because it doesn’t reduce the chance of programmer error.

Like alternative #1, this solution doesn’t protect against spare copies of the same pointers and is subject to the same maintainability concerns.

### Alternative #3: Rewrite gpr_free to take void\*&
```
gpr_free (void*& ptr) {
  ...
  ptr = nullptr;
}
```
This falls into the same pitfalls as solution #1 and furthermore is C89 non-compliant, which is a current requirement for `gpr_free`. Moreover, Google’s style guide discourages non-const reference parameters, so this is even less desirable than solution #1.


### Conclusion
Because of performance and maintainability concerns, GRP-01-002 will be addressed through the ongoing work to move gRPC Core to C++ and smart pointers and the future work of removing `gpr_free` in general. We will continue to leverage our sanitizer and fuzzing tests to help expose vulnerabilities.

## GRP-01-003 Calls to malloc suffer from potential integer overflows
The vulnerability, as defined by the report, is that calls to `gpr_malloc` in the C-core codebase may suffer from potential integer overflow in cases where we multiply the array element size by the size of the array. The penetration testers did not identify a concrete place where this occurred, but rather emphasized that the coding pattern itself had potential to lead to vulnerabilities. The report’s suggested solution for GRP-01-003 was to create a `calloc(size_t nmemb, size_t size)` wrapper that contains integer overflow checks.

However, gRPC team firmly believes that gRPC Core should only use integer overflow checks in the places where they’re needed; for example, any place where remote input influences the input to `gpr_malloc` in an unverified way. This is because bounds-checking is very expensive at the RPC layer.

Determining exactly where bounds-checking is needed requires an audit of tracing each `gpr_malloc` (or `gpr_realloc` or `gpr_zalloc`) call up the stack to determine if the sufficient bounds-checking was performed. This kind of audit, done manually, is fairly expensive engineer-wise.

### Conclusion
GRP-01-003 will be addressed through leveraging gRPC Core fuzzer tests to actively identify and resolve any integer overflow issues. If any issues are identified, we may create a `gpr_safe_malloc(size_t nmemb, size_t size)` wrapper to consolidate bounds-checking in one place. This function will *not* zero out memory because of performance concerns, and so will not be a calloc-style wrapper.

