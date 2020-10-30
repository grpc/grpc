# xDS Kubernetes Interop Tests

Proxyless Security Mesh Interop Tests executed on Kubernetes. Work in progress.

## Installation

#### Requrements
1. Python v3.6+
2. [Google Cloud SDK](https://cloud.google.com/sdk/docs/install)

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
```sh
# Help
python -m tests.baseline_test --help
python -m tests.baseline_test --helpfull

# Run on grpc-testing cluster
python -m tests.baseline_test \
  --flagfile="config/grpc-testing.cfg" \
  --kube_context="${KUBE_CONTEXT}" \
  --namespace=interop-psm-security \
  --server_image="gcr.io/grpc-testing/xds-k8s-test-server-java:latest" \
  --client_image="gcr.io/grpc-testing/xds-k8s-test-client-java:latest" \
```

### xDS Security Tests
```sh
# Help
python -m tests.security_test --help
python -m tests.security_test --helpfull

# Run on grpc-testing cluster
python -m tests.security_test \
  --flagfile="config/grpc-testing.cfg" \
  --kube_context="${KUBE_CONTEXT}" \
  --namespace=interop-psm-security \
  --server_image="gcr.io/grpc-testing/xds-k8s-test-server-java:latest" \
  --client_image="gcr.io/grpc-testing/xds-k8s-test-client-java:latest" \
```
