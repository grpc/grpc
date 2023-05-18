# xDS Kubernetes Interop Tests

Proxyless Security Mesh Interop Tests executed on Kubernetes.

### Experimental
Work in progress. Internal APIs may and will change. Please refrain from making
changes to this codebase at the moment.

### Stabilization roadmap 
- [x] Replace retrying with tenacity
- [x] Generate namespace for each test to prevent resource name conflicts and
      allow running tests in parallel
- [x] Security: run server and client in separate namespaces
- [ ] Make framework.infrastructure.gcp resources [first-class
      citizen](https://en.wikipedia.org/wiki/First-class_citizen), support
      simpler CRUD
- [x] Security: manage `roles/iam.workloadIdentityUser` role grant lifecycle for
      dynamically-named namespaces 
- [x] Restructure `framework.test_app` and `framework.xds_k8s*` into a module
      containing xDS-interop-specific logic
- [ ] Address inline TODOs in code
- [x] Improve README.md documentation, explain helpers in bin/ folder

## Installation

#### Requirements
1. Python v3.9+
2. [Google Cloud SDK](https://cloud.google.com/sdk/docs/install)
3. `kubectl`

`kubectl` can be installed via `gcloud components install kubectl`, or system package manager: https://kubernetes.io/docs/tasks/tools/#kubectl

Python3 venv tool may need to be installed from APT on some Ubuntu systems:
```shell
sudo apt-get install python3-venv
```

##### Getting Started

1. If you haven't, [initialize](https://cloud.google.com/sdk/docs/install-sdk) gcloud SDK
2. Activate gcloud [configuration](https://cloud.google.com/sdk/docs/configurations) with your project 
3. Enable gcloud services:
   ```shell
   gcloud services enable \
     compute.googleapis.com \
     container.googleapis.com \
     logging.googleapis.com \
     monitoring.googleapis.com \
     networksecurity.googleapis.com \
     networkservices.googleapis.com \
     secretmanager.googleapis.com \
     trafficdirector.googleapis.com
   ```

#### Configure GKE cluster
This is an example outlining minimal requirements to run the [baseline tests](xds-baseline-tests).
 
Update gloud sdk:
```shell
gcloud -q components update
```

Pre-populate environment variables for convenience. To find project id, refer to
[Identifying projects](https://cloud.google.com/resource-manager/docs/creating-managing-projects#identifying_projects).
```shell
export PROJECT_ID="your-project-id"
export PROJECT_NUMBER=$(gcloud projects describe "${PROJECT_ID}" --format="value(projectNumber)")
# Compute Engine default service account
export GCE_SA="${PROJECT_NUMBER}-compute@developer.gserviceaccount.com"
# The prefix to name GCP resources used by the framework
export RESOURCE_PREFIX="xds-k8s-interop-tests"

# The zone name your cluster, f.e. xds-k8s-test-cluster
export CLUSTER_NAME="${RESOURCE_PREFIX}-cluster"
# The zone of your cluster, f.e. us-central1-a
export ZONE="us-central1-a" 
# Dedicated GCP Service Account to use with workload identity.
export WORKLOAD_SA_NAME="${RESOURCE_PREFIX}"
export WORKLOAD_SA_EMAIL="${WORKLOAD_SA_NAME}@${PROJECT_ID}.iam.gserviceaccount.com"
```

##### Create the cluster 
Minimal requirements: [VPC-native](https://cloud.google.com/traffic-director/docs/security-proxyless-setup)
cluster with [Workload Identity](https://cloud.google.com/kubernetes-engine/docs/how-to/workload-identity) enabled. 
```shell
gcloud container clusters create "${CLUSTER_NAME}" \
 --scopes=cloud-platform \
 --zone="${ZONE}" \
 --enable-ip-alias \
 --workload-pool="${PROJECT_ID}.svc.id.goog" \
 --workload-metadata=GKE_METADATA \
 --tags=allow-health-checks
```

For security tests you also need to create CAs and configure the cluster to use those CAs
as described
[here](https://cloud.google.com/traffic-director/docs/security-proxyless-setup#configure-cas).

##### Create the firewall rule
Allow [health checking mechanisms](https://cloud.google.com/traffic-director/docs/set-up-proxyless-gke#creating_the_health_check_firewall_rule_and_backend_service)
to query the workloads health.  
This step can be skipped, if the driver is executed with `--ensure_firewall`.
```shell
gcloud compute firewall-rules create "${RESOURCE_PREFIX}-allow-health-checks" \
  --network=default --action=allow --direction=INGRESS \
  --source-ranges="35.191.0.0/16,130.211.0.0/22" \
  --target-tags=allow-health-checks \
  --rules=tcp:8080-8100
```

##### Setup GCP Service Account

Create dedicated GCP Service Account to use
with [workload identity](https://cloud.google.com/kubernetes-engine/docs/how-to/workload-identity).

```shell
gcloud iam service-accounts create "${WORKLOAD_SA_NAME}" \
  --display-name="xDS K8S Interop Tests Workload Identity Service Account"
```

Enable the service account to [access the Traffic Director API](https://cloud.google.com/traffic-director/docs/prepare-for-envoy-setup#enable-service-account).
```shell
gcloud projects add-iam-policy-binding "${PROJECT_ID}" \
   --member="serviceAccount:${WORKLOAD_SA_EMAIL}" \
   --role="roles/trafficdirector.client"
```

##### Allow access to images
The test framework needs read access to the client and server images and the bootstrap
generator image. You may have these images in your project but if you want to use these
from the grpc-testing project you will have to grant the necessary access to these images
using https://cloud.google.com/container-registry/docs/access-control#grant or a
gsutil command. For example, to grant access to images stored in `grpc-testing` project GCR, run:

```sh
gsutil iam ch "serviceAccount:${GCE_SA}:objectViewer" gs://artifacts.grpc-testing.appspot.com/
```

##### Allow test driver to configure workload identity automatically
Test driver will automatically grant `roles/iam.workloadIdentityUser` to 
allow the Kubernetes service account to impersonate the dedicated GCP workload
service account (corresponds to the step 5
of [Authenticating to Google Cloud](https://cloud.google.com/kubernetes-engine/docs/how-to/workload-identity#authenticating_to)).
This action requires the test framework to have `iam.serviceAccounts.create`
permission on the project.

If you're running test framework locally, and you have `roles/owner` to your
project, **you can skip this step**.  
If you're configuring the test framework to run on a CI: use `roles/owner`
account once to allow test framework to grant `roles/iam.workloadIdentityUser`.

```shell
# Assuming CI is using Compute Engine default service account.
gcloud projects add-iam-policy-binding "${PROJECT_ID}" \
  --member="serviceAccount:${GCE_SA}" \
  --role="roles/iam.serviceAccountAdmin" \
  --condition-from-file=<(cat <<-END
---
title: allow_workload_identity_only
description: Restrict serviceAccountAdmin to granting role iam.workloadIdentityUser
expression: |-
  api.getAttribute('iam.googleapis.com/modifiedGrantsByRole', [])
        .hasOnly(['roles/iam.workloadIdentityUser'])
END
)
```

##### Configure GKE cluster access
```shell
# Unless you're using GCP VM with preconfigured Application Default Credentials, acquire them for your user
gcloud auth application-default login

# Install authentication plugin for kubectl.
# Details: https://cloud.google.com/blog/products/containers-kubernetes/kubectl-auth-changes-in-gke
gcloud components install gke-gcloud-auth-plugin

# Configuring GKE cluster access for kubectl
gcloud container clusters get-credentials "${CLUSTER_NAME}" --zone "${ZONE}"

# Save generated kube context name
export KUBE_CONTEXT="$(kubectl config current-context)"
``` 

#### Install python dependencies

```shell
# Create python virtual environment
python3 -m venv venv

# Activate virtual environment
. ./venv/bin/activate

# Install requirements
pip install -r requirements.lock

# Generate protos
python -m grpc_tools.protoc --proto_path=../../../ \
    --python_out=. --grpc_python_out=. \
    src/proto/grpc/testing/empty.proto \
    src/proto/grpc/testing/messages.proto \
    src/proto/grpc/testing/test.proto
```

# Basic usage

## Local development
This test driver allows running tests locally against remote GKE clusters, right
from your dev environment. You need:

1. Follow [installation](#installation) instructions
2. Authenticated `gcloud`
3. `kubectl` context (see [Configure GKE cluster access](#configure-gke-cluster-access))
4. Run tests with `--debug_use_port_forwarding` argument. The test driver 
   will automatically start and stop port forwarding using
   `kubectl` subprocesses. (experimental)

### Making changes to the driver
1. Install additional dev packages: `pip install -r requirements-dev.txt`
2. Use `./bin/yapf.sh` and `./bin/isort.sh` helpers to auto-format code.

### Updating Python Dependencies

We track our Python-level dependencies using three different files:

- `requirements.txt`
- `dev-requirements.txt`
- `requirements.lock`

`requirements.txt` lists modules without specific versions supplied, though
versions ranges may be specified. `requirements.lock` is generated from
`requirements.txt` and _does_ specify versions for every dependency in the
transitive dependency tree.

When updating `requirements.txt`, you must also update `requirements.lock`. To
do this, navigate to this directory and run `./bin/freeze.sh`.

### Setup test configuration

There are many arguments to be passed into the test run. You can save the
arguments to a config file ("flagfile") for your development environment.
Use [`config/local-dev.cfg.example`](https://github.com/grpc/grpc/blob/master/tools/run_tests/xds_k8s_test_driver/config/local-dev.cfg.example)
as a starting point:

```shell
cp config/local-dev.cfg.example config/local-dev.cfg
```

If you exported environment variables in the above sections, you can
template them into the local config (note this recreates the config):

```shell
envsubst < config/local-dev.cfg.example > config/local-dev.cfg
```

Learn more about flagfiles in [abseil documentation](https://abseil.io/docs/python/guides/flags#a-note-about---flagfile).

## Test suites

See the full list of available test suites in the [`tests/`](https://github.com/grpc/grpc/tree/master/tools/run_tests/xds_k8s_test_driver/tests) folder. 

### xDS Baseline Tests

Test suite meant to confirm that basic xDS features work as expected. Executing
it before other test suites will help to identify whether test failure related
to specific features under test, or caused by unrelated infrastructure
disturbances.

```shell
# Help
python -m tests.baseline_test --help
python -m tests.baseline_test --helpfull

# Run the baseline test with local-dev.cfg settings
python -m tests.baseline_test --flagfile="config/local-dev.cfg"
  
# Same as above, but using the helper script
./run.sh tests/baseline_test.py
```

### xDS Security Tests
Test suite meant to verify mTLS/TLS features. Note that this requires
additional environment configuration. For more details, and for the 
setup for the security tests, see
["Setting up Traffic Director service security with proxyless gRPC"](https://cloud.google.com/traffic-director/docs/security-proxyless-setup) user guide.

```shell
# Run the security test with local-dev.cfg settings
python -m tests.security_test --flagfile="config/local-dev.cfg"

# Same as above, but using the helper script
./run.sh tests/security_test.py
```

## Helper scripts
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
XDS_K8S_CONFIG=./path-to-flagfile.cfg ./run.sh bin/run_td_setup.py --resource_suffix=override-suffix
./run.sh tests/baseline_test.py
./run.sh tests/security_test.py --verbosity=1 --logger_levels=__main__:DEBUG,framework:DEBUG
./run.sh tests/security_test.py SecurityTest.test_mtls --nocheck_local_certs
```

## Partial setups
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

# Server regular mode: all grpc services on port 8080
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
