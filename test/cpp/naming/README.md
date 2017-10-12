# Resolver Tests

This directory has tests and infrastructure for unit tests and GCE
integration tests of gRPC resolver functionality.

There are two different tests here:

## Resolver unit tests (resolver "component" tests)

These tests run per-change, along with the rest of the grpc unit tests.
They query a local testing DNS server.

## GCE integration tests

These tests use the same test binary and the same test records
as the unit tests, but they run against GCE DNS (this is done by
running the test on a GCE instance and not specifying an authority
in uris). These tests run in a background job, which needs to be
actively monitored.

## Making changes to test records

After making a change to `resolver_test_record_groups.yaml`:

1. Increment the "version number" in the `resolver_tests_common_zone_name`
   DNS zone (this is a yaml field at the top
   of `resolver_test_record_groups.yaml`).

2. Regenerate projects.

3. From the repo root, run:

```
$ test/cpp/naming/create_dns_private_zone.sh
$ test/cpp/naming/private_dns_zone_init.sh
```

Note that these commands must be ran in environment that
has access to the grpc-testing GCE project.

If everything runs smoothly, then once the change is merged,
the GCE DNS integration testing job will transition to the
new records and continue passing.

### Cleanup

It might be useful to clean up old GCE DNS zones and records after creating a
new private zone. An old GCE DNS zone can cleaned up and removed from the
grpc-testing cloud project with the following commands:

```
$ touch empty-file
$ gcloud dns record-sets import -z $ZONE_ID_OF_ZONE_TO_DELETE --delete-all-existing empty-file
$ gcloud dns managed-zones delete $ZONE_ID_OF_ZONE_TO_DELETE
```
