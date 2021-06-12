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
3. Configured GKE cluster

#### Configure GKE cluster
This is an example outlining minimal requirements to run `tests.baseline_test`.  
For more details, and for the setup for security tests, see
["Setting up Traffic Director service security with proxyless gRPC"](https://cloud.google.com/traffic-director/docs/security-proxyless-setup)
 user guide.
 
Update gloud sdk:
```shell
gcloud -q components update
```

Pre-populate environment variables for convenience. To find project id, refer to
[Identifying projects](https://cloud.google.com/resource-manager/docs/creating-managing-projects#identifying_projects).
```shell
export PROJECT_ID="your-project-id"
export PROJECT_NUMBER=$(gcloud projects describe "${PROJECT_ID}" --format="value(projectNumber)")

# The zone name your cluster, f.e. xds-k8s-test-cluster
export CLUSTER_NAME="xds-k8s-test-cluster"
# The zone of your cluster, f.e. us-central1-a
export ZONE="us-central1-a"
# K8S namespace you'll use to run the cluster, f.e.
export K8S_NAMESPACE="interop-psm-security" 
```

##### Create the cluster 
Minimal requirements: [VPC-native](https://cloud.google.com/traffic-director/docs/security-proxyless-setup)
cluster with [Workload Identity](https://cloud.google.com/kubernetes-engine/docs/how-to/workload-identity) enabled. 
```shell
gcloud beta container clusters create "${CLUSTER_NAME}" \
 --zone="${ZONE}" \
 --enable-ip-alias \
 --workload-pool="${PROJECT_ID}.svc.id.goog" \
 --workload-metadata=GKE_METADATA \
 --tags=allow-health-checks
```

##### Create the firewall rule
Allow [health checking mechanisms](https://cloud.google.com/traffic-director/docs/set-up-proxyless-gke#creating_the_health_check_firewall_rule_and_backend_service)
to query the workloads health.  
This step can be skipped, if the driver is executed with `--ensure_firewall`.
```shell
gcloud compute firewall-rules create "${K8S_NAMESPACE}-allow-health-checks" \
  --network=default --action=allow --direction=INGRESS \
  --source-ranges="35.191.0.0/16,130.211.0.0/22" \
  --target-tags=allow-health-checks \
  --rules=tcp:8080-8100
```

##### Allow workload identities to talk to Traffic Director APIs
```shell
gcloud iam service-accounts add-iam-policy-binding "${PROJECT_NUMBER}-compute@developer.gserviceaccount.com" \
  --role roles/iam.workloadIdentityUser \
  --member "serviceAccount:${PROJECT_ID}.svc.id.goog[${K8S_NAMESPACE}/psm-grpc-client]"

gcloud iam service-accounts add-iam-policy-binding "${PROJECT_NUMBER}-compute@developer.gserviceaccount.com" \
  --role roles/iam.workloadIdentityUser \
  --member "serviceAccount:${PROJECT_ID}.svc.id.goog[${K8S_NAMESPACE}/psm-grpc-server]"
``` 

##### Configure GKE cluster access
```shell
# Configuring GKE cluster access for kubectl
gcloud container clusters get-credentials "your_gke_cluster_name" --zone "your_gke_cluster_zone"

# Save generated kube context name
export KUBE_CONTEXT="$(kubectl config current-context)"
``` 

#### Install python dependencies

```shell
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

```shell
# Help
python -m tests.baseline_test --help
python -m tests.baseline_test --helpfull

# Run on grpc-testing cluster
python -m tests.baseline_test \
  --flagfile="config/grpc-testing.cfg" \
  --kube_context="${KUBE_CONTEXT}" \
  --server_image="gcr.io/grpc-testing/xds-interop/java-server:d22f93e1ade22a1e026b57210f6fc21f7a3ca0cf" \
  --client_image="gcr.io/grpc-testing/xds-interop/java-client:d22f93e1ade22a1e026b57210f6fc21f7a3ca0cf"
```

### xDS Security Tests
```shell
# Help
python -m tests.security_test --help
python -m tests.security_test --helpfull

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

## Local development
This test driver allows running tests locally against remote GKE clusters, right
from your dev environment. You need:

1. Follow [installation](#installation) instructions
2. Authenticated `gcloud`
3. `kubectl` context (see [Configure GKE cluster access](#configure-gke-cluster-access))
4. Run tests with `--debug_use_port_forwarding` argument. The test driver 
   will automatically start and stop port forwarding using
   `kubectl` subprocesses. (experimental)

### Setup test configuration

There are many arguments to be passed into the test run. You can save the
arguments to a config file ("flagfile") for your development environment.
Use [`config/local-dev.cfg.example`](https://github.com/grpc/grpc/blob/master/tools/run_tests/xds_k8s_test_driver/config/local-dev.cfg.example)
as a starting point:

```shell
cp config/local-dev.cfg.example config/local-dev.cfg
```

Learn more about flagfiles in [abseil documentation](https://abseil.io/docs/python/guides/flags#a-note-about---flagfile).

### Helper scripts
You can use interop xds-k8s [`bin/`](https://github.com/grpc/grpc/tree/master/tools/run_tests/xds_k8s_test_driver/bin)
scripts to configure TD, start k8s instances step-by-step, and keep them alive
for as long as you need. 

* To run helper scripts using local config:
  * `python -m bin.script_name --flagfile=config/local-dev.cfg`
  * `./run.sh bin/script_name.py` automatically appends the flagfile
* Use `--help` to see script-specific argument
* Use `--helpfull` to see all available argument

#### Overview
```shell
# Helper tool to configure Traffic Director with different security options
python -m bin.run_td_setup --help

# Helper tools to run the test server, client (with or without security)
python -m bin.run_test_server --help
python -m bin.run_test_client --help

# Helper tool to verify different security configurations via channelz
python -m bin.run_channelz --help
```

#### `./run.sh` helper
Use `./run.sh` to execute helper scripts and tests with `config/local-dev.cfg`.

```sh
USAGE: ./run.sh script_path [arguments]
   script_path: path to python script to execute, relative to driver root folder
   arguments ...: arguments passed to program in sys.argv

ENVIRONMENT:
   XDS_K8S_CONFIG: file path to the config flagfile, relative to
                   driver root folder. Default: config/local-dev.cfg
                   Will be appended as --flagfile="config_absolute_path" argument
   XDS_K8S_DRIVER_VENV_DIR: the path to python virtual environment directory
                            Default: $XDS_K8S_DRIVER_DIR/venv
DESCRIPTION:
This tool performs the following:
1) Ensures python virtual env installed and activated
2) Exports test driver root in PYTHONPATH
3) Automatically appends --flagfile="\$XDS_K8S_CONFIG" argument

EXAMPLES:
./run.sh bin/run_td_setup.py --help
./run.sh bin/run_td_setup.py --helpfull
XDS_K8S_CONFIG=./path-to-flagfile.cfg ./run.sh bin/run_td_setup.py --namespace=override-namespace
./run.sh tests/baseline_test.py
./run.sh tests/security_test.py --verbosity=1 --logger_levels=__main__:DEBUG,framework:DEBUG
./run.sh tests/security_test.py SecurityTest.test_mtls --nocheck_local_certs
```

### Regular workflow
```shell
# Setup Traffic Director
./run.sh bin/run_td_setup.py

# Start test server
./run.sh bin/run_test_server.py

# Add test server to the backend service
./run.sh bin/run_td_setup.py --cmd=backends-add

# Start test client
./run.sh bin/run_test_client.py
```

### Secure workflow
```shell
# Setup Traffic Director in mtls. See --help for all options
./run.sh bin/run_td_setup.py --security=mtls

# Start test server in a secure mode
./run.sh bin/run_test_server.py --secure

# Add test server to the backend service
./run.sh bin/run_td_setup.py --cmd=backends-add

# Start test client in a secure more --secure
./run.sh bin/run_test_client.py --secure
```

### Sending RPCs
#### Start port forwarding
```shell
# Client: all services always on port 8079
kubectl port-forward deployment.apps/psm-grpc-client 8079

# Server regular mode: all grpc services on port 8079
kubectl port-forward deployment.apps/psm-grpc-server 8080
# OR
# Server secure mode: TestServiceImpl is on 8080, 
kubectl port-forward deployment.apps/psm-grpc-server 8080
# everything else (channelz, healthcheck, CSDS) on 8081
kubectl port-forward deployment.apps/psm-grpc-server 8081
```

#### Send RPCs with grpccurl
```shell
# 8081 if security enabled
export SERVER_ADMIN_PORT=8080

# List server services using reflection
grpcurl --plaintext 127.0.0.1:$SERVER_ADMIN_PORT list
# List client services using reflection
grpcurl --plaintext 127.0.0.1:8079 list

# List channels via channelz
grpcurl --plaintext 127.0.0.1:$SERVER_ADMIN_PORT grpc.channelz.v1.Channelz.GetTopChannels
grpcurl --plaintext 127.0.0.1:8079 grpc.channelz.v1.Channelz.GetTopChannels

# Send GetClientStats to the client
grpcurl --plaintext -d '{"num_rpcs": 10, "timeout_sec": 30}' 127.0.0.1:8079 \
  grpc.testing.LoadBalancerStatsService.GetClientStats
```

### Cleanup
* First, make sure to stop port forwarding, if any
* Run `./bin/cleanup.sh`

##### Partial cleanup
You can run commands below to stop/start, create/delete resources however you want.  
Generally, it's better to remove resources in the opposite order of their creation.

Cleanup regular resources:
```shell
# Cleanup TD resources
./run.sh bin/run_td_setup.py --cmd=cleanup
# Stop test client
./run.sh bin/run_test_client.py --cmd=cleanup
# Stop test server, and remove the namespace
./run.sh bin/run_test_server.py --cmd=cleanup --cleanup_namespace
```

Cleanup regular and security-specific resources:
```shell
# Cleanup TD resources, with security
./run.sh bin/run_td_setup.py --cmd=cleanup --security=mtls
# Stop test client (secure)
./run.sh bin/run_test_client.py --cmd=cleanup --secure
# Stop test server (secure), and remove the namespace
./run.sh bin/run_test_server.py --cmd=cleanup --cleanup_namespace --secure
```

In addition, here's some other helpful partial cleanup commands:
```shell
# Remove all backends from the backend services
./run.sh bin/run_td_setup.py --cmd=backends-cleanup

# Stop the server, but keep the namespace
./run.sh bin/run_test_server.py --cmd=cleanup --nocleanup_namespace
```

### Known errors
#### Error forwarding port
If you stopped a test with `ctrl+c`, while using `--debug_use_port_forwarding`,
you might see an error like this:

> `framework.infrastructure.k8s.PortForwardingError: Error forwarding port, unexpected output Unable to listen on port 8081: Listeners failed to create with the following errors: [unable to create listener: Error listen tcp4 127.0.0.1:8081: bind: address already in use]`

Unless you're running `kubectl port-forward` manually, it's likely that `ctrl+c`
interrupted python before it could clean up subprocesses.

You can do `ps aux | grep port-forward` and then kill the processes by id,
or with `killall kubectl`
