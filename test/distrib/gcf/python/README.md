# Python Google Cloud Functions Distribtest

This distribtest acts as a smoke test for usage of the `grpcio` Python wheel in
the GCF environment. This test is dependent on two long-lived GCP resources:

- `gcf-distribtest-topic` Pub Sub Topic with default configuration.
- `grpc-gcf-distribtest` GCS Bucket. This bucket has 1 day TTL on all artifacts.


All Functions _should_ be deleted by the test under normal circumstances. In
case anything goes wrong with this process, a `cleanup.sh` script is provided to
delete any dangling test functions.
