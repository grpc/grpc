# xDS Kubernetes Interop Tests

Proxyless Security Mesh Interop Tests executed on Kubernetes.

### Experimental
Work in progress. Internal APIs may and will change. Please refrain from making
changes to this codebase at the moment.

### Stabilization roadmap 
- [ ] Replace retrying with tenacity
- [ ] Generate namespace for each test to prevent resource name conflicts and
      allow running tests in parallel
- [ ] Security: run server and client in separate namespaces
- [ ] Make framework.infrastructure.gcp resources [first-class
      citizen](https://en.wikipedia.org/wiki/First-class_citizen), support
      simpler CRUD
- [ ] Security: manage `roles/iam.workloadIdentityUser` role grant lifecycle for
      dynamically-named namespaces 
- [ ] Restructure `framework.test_app` and `framework.xds_k8s*` into a module
      containing xDS-interop-specific logic
- [ ] Address inline TODOs in code
- [ ] Improve README.md documentation, explain helpers in bin/ folder

## Installation

#### Requirements
1. Python v3.6+
2. [Google Cloud SDK](https://cloud.google.com/sdk/docs/install)
3. A GKE cluster (must enable "Enable VPC-native traffic routing" to use it with
   the Traffic Director)
    * Otherwise, you will see error logs when you inspect Kubernetes virtual
      service
    * (In `grpc-testing`, you will need a metadata tag
      `--tags=allow-health-checks` to allow UHC to reach your resources.)

#### Configure GKE cluster access

```sh
# Update gloud sdk
gcloud -q components update

# Configuring GKE cluster access for kubectl
gcloud container clusters get-credentials "your_gke_cluster_name" --zone "your_gke_cluster_zone"

# Save generated kube context name
KUBE_CONTEXT="$(kubectl config current-context)"
``` 

#### Install python dependencies

```sh
# Create python virtual environment
python3.6 -m venv venv

# Activate virtual environment
. ./venv/bin/activate

# Install requirements
pip install -r requirements.txt

# Generate protos
python -m grpc_tools.protoc --proto_path=../../../ \
    --python_out=. --grpc_python_out=. \
    src/proto/grpc/testing/empty.proto \
    src/proto/grpc/testing/messages.proto \
    src/proto/grpc/testing/test.proto
```

# Basic usage

### xDS Baseline Tests

Test suite meant to confirm that basic xDS features work as expected. Executing
it before other test suites will help to identify whether test failure related
to specific features under test, or caused by unrelated infrastructure
disturbances.

The client and server images are created based on Git commit hashes, but not
every single one of them. It is triggered nightly and per-release. For example,
the commit we are using below (`d22f93e1ade22a1e026b57210f6fc21f7a3ca0cf`) comes
from branch `v1.37.x` in `grpc-java` repo.

```sh
# Help
python -m tests.baseline_test --help
python -m tests.baseline_test --helpful

# Run on grpc-testing cluster
python -m tests.baseline_test \
  --flagfile="config/grpc-testing.cfg" \
  --kube_context="${KUBE_CONTEXT}" \
  --server_image="gcr.io/grpc-testing/xds-interop/java-server:d22f93e1ade22a1e026b57210f6fc21f7a3ca0cf" \
  --client_image="gcr.io/grpc-testing/xds-interop/java-client:d22f93e1ade22a1e026b57210f6fc21f7a3ca0cf"
```

### xDS Security Tests
```sh
# Help
python -m tests.security_test --help
python -m tests.security_test --helpful

# Run on grpc-testing cluster
python -m tests.security_test \
  --flagfile="config/grpc-testing.cfg" \
  --kube_context="${KUBE_CONTEXT}" \
  --server_image="gcr.io/grpc-testing/xds-interop/java-server:d22f93e1ade22a1e026b57210f6fc21f7a3ca0cf" \
  --client_image="gcr.io/grpc-testing/xds-interop/java-client:d22f93e1ade22a1e026b57210f6fc21f7a3ca0cf"
```

### Test namespace

It's possible to run multiple xDS interop test workloads in the same project.
But we need to ensure the name of the global resources won't conflict. This can
be solved by supplying `--namespace` and `--server_xds_port`. The xDS port needs
to be unique across the entire project (default port range is [8080, 8280],
avoid if possible). Here is an example:

```shell
python3 -m tests.baseline_test \
  --flagfile="config/grpc-testing.cfg" \
  --kube_context="${KUBE_CONTEXT}" \
  --server_image="gcr.io/grpc-testing/xds-interop/java-server:d22f93e1ade22a1e026b57210f6fc21f7a3ca0cf" \
  --client_image="gcr.io/grpc-testing/xds-interop/java-client:d22f93e1ade22a1e026b57210f6fc21f7a3ca0cf" \
  --namespace="box-$(date +"%F-%R")" \
  --server_xds_port="$(($RANDOM%1000 + 34567))"
```

### Setup test configuration

There are many arguments to be passed into the test run. You can save the
arguments to a config file for your development environment. Please take a look
at
https://github.com/grpc/grpc/blob/master/tools/run_tests/xds_k8s_test_driver/config/local-dev.cfg.example.
You can create your own config by:

```shell
cp config/local-dev.cfg.example config/local-dev.cfg
```

### Clean-up resources

```shell
python -m bin.run_td_setup --cmd=cleanup --flagfile=config/local-dev.cfg && \
python -m bin.run_test_client --cmd=cleanup --flagfile=config/local-dev.cfg && \
python -m bin.run_test_server --cmd=cleanup --cleanup_namespace --flagfile=config/local-dev.cfg
```
