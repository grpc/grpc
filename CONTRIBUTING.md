# Contributing guide

## API changes

All API changes should follow the [style guide](STYLE.md).

API changes are regular PRs in https://github.com/envoyproxy/envoy for the API/configuration
changes. They may be as part of a larger implementation PR. Please follow the standard Bazel and CI
process for validating build/test sanity of `api/` before submitting a PR.

*Note: New .proto files should be added to
[BUILD](https://github.com/envoyproxy/envoy/blob/main/api/versioning/BUILD) in order to get the RSTs generated.*

## Documentation changes

The Envoy project takes documentation seriously. We view it as one of the reasons the project has
seen rapid adoption. As such, it is required that all features have complete documentation. This is
generally going to be a combination of API documentation as well as architecture/overview
documentation.

### Building documentation locally

The documentation can be built locally in the root of https://github.com/envoyproxy/envoy via:

```
docs/build.sh
```

To skip configuration examples validation:

```
SPHINX_SKIP_CONFIG_VALIDATION=true docs/build.sh
```

Or to use a hermetic Docker container:

```
./ci/run_envoy_docker.sh './ci/do_ci.sh docs'
```

This process builds RST documentation directly from the proto files, merges it with the static RST
files, and then runs [Sphinx](https://www.sphinx-doc.org/en/stable/rest.html) over the entire tree to
produce the final documentation. The generated RST files are not committed as they are regenerated
every time the documentation is built.

### Viewing documentation

Once the documentation is built, it is available rooted at `generated/docs/index.html`. The
generated RST files are also viewable in `generated/rst`.

Note also that the generated documentation can be viewed in CI:

1. Open docs job in Azure Pipelines.
2. Navigate to "Upload Docs to GCS" log.
3. Click on the link there.

If you do not see "Upload Docs to GCS" or it is failing, that means the docs are not built correctly.

### Documentation guidelines

The following are some general guidelines around documentation.

* Cross link as much as possible. Sphinx is fantastic at this. Use it! See ample examples with the
  existing documentation as a guide.
* Please use a **single space** after a period in documentation so that all generated text is
  consistent.
* Comments can be left inside comments if needed (that's pretty deep, right?) via the `[#comment:]`
  special tag. E.g.,

  ```
  // This is a really cool field!
  // [#comment:TODO(mattklein123): Do something cooler]
  string foo_field = 3;
  ```

* Prefer *italics* for emphasis as `backtick` emphasis is somewhat jarring in our Sphinx theme.
* All documentation is expected to use proper English grammar with proper punctuation. If you are
  not a fluent English speaker please let us know and we will help out.
