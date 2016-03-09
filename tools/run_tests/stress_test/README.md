Running Stress tests on Google Container Engine
=======================================

### **Glossary**:
* GCP: Google Cloud Platform
* GCE: Google Compute Engine
* GKE: Google Container Engine
* GCP console: https://console.cloud.google.com

### **Setup Instructions**
#### *On GCP:*
1. Create a GCP account (if you haven’t already) at https://cloud.google.com 
2. Enable billing on Google cloud platform. Instructions [here](https://cloud.google.com/container-engine/docs/before-you-begin)  (see the '*Enable billing*' section).
3. Create a Project from the [GCP console](https://console.cloud.google.com) 
4. Enable the Container Engine API. Instructions [here](https://cloud.google.com/container-engine/docs/before-you-begin)  (See the '*Enable the Container Engine API*’ section). 
5. Create a Cluster from the GCP console. i.e Go to the Container Engine section from GCP console and click ‘Create Container Cluster’ and follow the instructions.
    5.1. The instructions for Name/Zone/MachineType etc are [here](https://cloud.google.com/container-engine/docs/clusters/operations) (**NOTE**: The page also has instructions to setting up default clusters and configuring `kubectl`. We will be doing that later)
    5.2. For the cluster size, a smaller size of < 10 GCE instances is good enough for our use cases - assuming that we are planning to run a reasonably small number of stress client instances.
    5.3. **IMPORTANT**: Before hitting the "Create" button, click on “More” link just above the "Create" button and Select "Enabled" for BigQuery , "Enabled" for Cloud Platform and "Read/Write" for Cloud User Accounts.
    5.4. Create the cluster by clicking "Create" button.

#### *On your machine* (or the machine from which stress tests on GKE are launched): 
1. You need a working gRPC repository on your machine. If you do not have it, clone the grpc repository from github (https://github.com/grpc/grpc) and follow the instructions at https://github.com/grpc/grpc/blob/master/INSTALL.md 
2. Install Docker (https://docs.docker.com/engine/installation/)
3. Install Google Cloud SDK. Instructions [here](https://cloud.google.com/sdk/). This installs the `gcloud` tool
4. Install `kubectl`, Kubernetes command line tool using `gcloud` 
        `$ gcloud components update kubectl`
5. Install Google python client apis: 
        `‘$ sudo pip install --upgrade google-api-python-client’`
        **Note**: Do  `$ easy_install -U pip` if you do not have pip
6. Install the `requests` Python package if you don’t have it already. Instructions are [here](http://docs.python-requests.org/en/master/user/install/)
7. Set the `gcloud` defaults: See the instructions at https://cloud.google.com/container-engine/docs/before-you-begin under "*Set gcloud defaults*" section)
7.1. Make sure you also fetch the cluster credentials for `kubectl` command to use. I.e `$ gcloud container clusters get-credentials CLUSTER_NAME`

### **Launching Stress tests**

The stress tests are launched by the following script (path is relative to GRPC root directory) :
`tools/run_tests/stress_test/run_stress_tests_on_gke.py`

The script has several parameters and you can find out more details by using the `--help` flag. 
`<grpc_root_dir>$ tools/run_tests/stress_test/run_stress_tests_on_gke.py --help`

> **Example**
> `$ tools/run_tests/Stress_test/run_stress_tests_on_gke.py --project_id=sree-gce --test_duration_secs=180 --num_clients=5`

>Launches the 5 instances of stress test clients, 1 instance of stress test server and runs the test for 180 seconds. The test would be run on the default container cluster (that you have set in `gcloud`) in the project `sree-gce`.  

> Note: we currently do not have the ability to launch multiple instances of the server. This can be added very easily in future 

all `kubectl`, Kubernetes command line tool using `gcloud` 
        `$ gcloud components update kubectl`
5. Install Google python client apis: 
        `‘$ sudo pip install --upgrade google-api-python-client’`
        **Note**: Do  `$ easy_install -U pip` if you do not have pip
6. Install the `requests` Python package if you don’t have it already. Instructions are [here](http://docs.python-requests.org/en/master/user/install/)
7. Set the `gcloud` defaults: See the instructions at https://cloud.google.com/container-engine/docs/before-you-begin under "*Set gcloud defaults*" section)
7.1. Make sure you also fetch the cluster credentials for `kubectl` command to use. I.e `$ gcloud container clusters get-credentials CLUSTER_NAME`

### **Launching Stress tests**

The stress tests are launched by the following script (path is relative to GRPC root directory) :
`tools/run_tests/stress_test/run_stress_tests_on_gke.py`

The script has several parameters and you can find out more details by using the `--help` flag. 
`<grpc_root_dir>$ tools/run_tests/stress_test/run_stress_tests_on_gke.py --help`

> **Example**
> `$ tools/run_tests/Stress_test/run_stress_tests_on_gke.py --project_id=sree-gce --test_duration_secs=180 --num_clients=5`

>Launches the 5 instances of stress test clients, 1 instance of stress test server and runs the test for 180 seconds. The test would be run on the default container cluster (that you have set in `gcloud`) in the project `sree-gce`.  

> Note: we currently do not have the ability to launch multiple instances of the server. This can be added very easily in future
