Running Stress tests on Google Container Engine
=======================================

### **Glossary**:
* GCP: Google Cloud Platform
* GCE: Google Compute Engine
* GKE: Google Container Engine
* GCP console: https://console.cloud.google.com

### **Setup Instructions**
#### *On GCP:*
1. Login to GCP with your Google account (for example, your @gmail account) at https://cloud.google.com. If do not have a Google account, you will have to create an account first.
2. Enable billing on Google cloud platform. Instructions [here](https://cloud.google.com/container-engine/docs/before-you-begin)  (see the '*Enable billing*' section).
3. Create a Project from the [GCP console](https://console.cloud.google.com).i.e Click on the project dropdown box on the top right (to the right of the search box) and click '*Create a project*' option.
4. Enable the Container Engine API. Instructions [here](https://cloud.google.com/container-engine/docs/before-you-begin)  (See the '*Enable the Container Engine API*’ section). Alternatively, you can do the following:
    - Click on the '*Products & Services*' icon on the top left (i.e the icon with three small horizontal bars) and select '*API Manager*'
    - Select the '*Container Engine API*' under '*Google Cloud APIs*' on the main page. Note that you might have to click on '*More*' under '*Google Cloud APIs*' to see the '*Container Engine API*' link
    - Click on the '*Enable*' button. If the API is already enabled, the button's label would be '*Disable*' instead (do NOT click the button if its label is '*Disable*')
5. Create a Cluster from the GCP console.
    - Go to the Container Engine section from GCP console i.e: Click on the '*Products & Services*' icon on the top left (i.e the icon with three small horizontal bars) and click on '*Container Engine*'
    - Click '*Create Container Cluster*' and follow the instructions.
    - The instructions for 'Name/Zone/MachineType' etc are [here](https://cloud.google.com/container-engine/docs/clusters/operations) (**NOTE**: The page also has instructions to setting up default clusters and configuring `kubectl`. We will be doing that later)
    - For the cluster size, a smaller size of < 10 GCE instances is good enough for our use cases - assuming that we are planning to run a reasonably small number of stress client instances. For the machine type, something like '2 vCPUs 7.5 GB' (available in the drop down box) should be good enough.
    - **IMPORTANT**: Before hitting the '*Create*' button, click on '*More*' link just above the '*Create*' button and Select '*Enabled*' for BigQuery , '*Enabled*' for Cloud Platform and '*Read/Write*' for Cloud User Accounts.
    - Create the cluster by clicking '*Create*' button.

#### *On your machine* (or the machine from which stress tests on GKE are launched):
1. You need a working gRPC repository on your machine. If you do not have it, clone the grpc repository from github (https://github.com/grpc/grpc) and follow the instructions [here](https://github.com/grpc/grpc/blob/master/INSTALL.md)
2. Install Docker. Instructions [here](https://docs.docker.com/engine/installation/)
3. Install Google Cloud SDK. Instructions [here](https://cloud.google.com/sdk/). This installs the `gcloud` tool
4. Install `kubectl`, Kubernetes command line tool using `gcloud`. i.e
    - `$ gcloud components update kubectl`
    - NOTE: If you are running this from a GCE instance, the command may fail with the following error:
    ```
     You cannot perform this action because this Cloud SDK installation is 
     managed by an external package manager. If you would like to get the
     latest version, please see our main download page at:

     https://developers.google.com/cloud/sdk/

     ERROR: (gcloud.components.update) The component manager is disabled for this installation
    ```
    -- If so, you will have to manually install Cloud SDK by doing the following
    ```shell
      $ # The following installs latest Cloud SDK and updates the PATH
      $ # (Accept the default values when prompted)
      $ curl https://sdk.cloud.google.com | bash
      $ exec -l $SHELL
      $ # Set the defaults. Pick the default GCE credentials when prompted (The service account
      $ # name will have a name similar to: "xxx-compute@developer.gserviceaccount.com")
      $ gcloud init
    ``` 

5. Install Google python client apis:
    - `‘$ sudo pip install --upgrade google-api-python-client’`
    -  **Note**: Do `$ sudo apt-get install python-pip` (or `$ easy_install -U pip`) if you do not have pip
6. Install the `requests` Python package if you don’t have it already by doing `sudo pip install requests`. More details regarding `requests` package are [here](http://docs.python-requests.org/en/master/user/install/)
7. Set the `gcloud` defaults: See the instructions [here](https://cloud.google.com/container-engine/docs/before-you-begin) under "*Set gcloud defaults*" section)
    - Make sure you also fetch the cluster credentials for `kubectl` command to use. I.e `$ gcloud container clusters get-credentials CLUSTER_NAME`

### **Launching Stress tests**

The stress tests are launched by the following script (path is relative to GRPC root directory) :
`tools/run_tests/stress_test/run_stress_tests_on_gke.py`

You can find out more details by using the `--help` flag.
  - `<grpc_root_dir>$ tools/run_tests/stress_test/run_on_gke.py --help`

> **Example**
> ```bash
> $ # Change to the grpc root directory
> $ cd $GRPC_ROOT
> $ tools/run_tests/stress_test/run_on_gke.py --project_id=sree-gce --config_file=tools/run_tests/stress_test/configs/opt.json
> ```

> The above runs the stress test on GKE under the project `sree-gce` in the default cluster (that you set by `gcloud` command earlier). The test settings (like number of client instances, servers, the parmeters to pass, test cases etc) are all loaded from the config file `$GRPC_ROOT/tools/run_tests/stress_test/opt.json`
