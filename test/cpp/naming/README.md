# Resolver Tests

This directory has tests and infrastructure for unit tests and GCE
integration tests of gRPC resolver functionality.

There are two different tests here:

## Resolver unit tests (resolver "component" tests)

These tests run per-change, along with the rest of the grpc unit tests.
They query a local testing DNS server.

## GCE integration tests (only for manual runs)

One can also run the "resolver component tests" against GCE DNS. This
is done by uploading the resolver component test DNS records to a GCE
DNS private zone under the grpc-testing project, running the test on
a GCE instance, and not specifying an authority in uris.

One can run these tests manually by running the following script on a GCE
VM belonging to the grpc-testing project (or an equivalent environment that
has all of the DNS records available):

```
python test/cpp/naming/experimental_manual_run_resolver_component_tests.py
```

Note that when running this script, some tests may fail only because
the DNS records haven't been uploaded to GCE DNS. One can check if this
is the case by querying the record manually with `dig`.

## Making changes to test records

After making a change to `resolver_test_record_groups.yaml`:

1. Increment the "version number" in the `resolver_tests_common_zone_name`
   DNS zone (this is a yaml field at the top
   of `resolver_test_record_groups.yaml`).

2. Regenerate projects.

3. From the repo root, run:

```
$ test/cpp/naming/create_private_dns_zone.sh
$ test/cpp/naming/private_dns_zone_init.sh
```

Note that these commands must be ran in environment that
has access to the grpc-testing GCE project.
