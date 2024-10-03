# Contributing to gRPC Python
We're thrilled you're interested in contributing to gRPC Python!  Our vibrant community is the heart of this project. Your expertise and ideas are invaluable, so join us in shaping the future of gRPC Python.

## Legal Requirements
**Sign the CLA:** Before your PR can be reviewed, you'll need to sign the [CNCF Contributor License Agreement (CLA)](https://identity.linuxfoundation.org/projects/cncf).

## Community Code of Conduct
gRPC Python follows the [CNCF Code of Conduct](https://github.com/cncf/foundation/blob/master/code-of-conduct.md).


## Guidelines
gRPC Python Follows [gRPC Guidelines for Pull Requests](https://github.com/grpc/grpc/blob/master/CONTRIBUTING.md#guidelines-for-pull-requests)

## There are many ways to contribute!

* **Code:** Fix bugs, add new features, or improve existing code.
* **Documentation:** Improve tutorials, guides, or API reference documentation.
* **Community:** Answer questions on forums, help triage issues, or write blog posts.
* **Testing:** Help ensure the quality of gRPC Python by writing tests or reporting bugs.


## Writing Your First Patch for gRPC Python

Ready to dive in?  We'll walk you through the entire process of making your first contribution, from identifying an issue to submitting your changes for review. Don't worry if you're new to open source – our documentation and helpful community will ensure a smooth experience.

### Prerequisites

* **Git:** You should have Git installed on your system. If not, download and install it from the [official Git website](https://git-scm.com/).
* **GitHub Account:** You'll need a GitHub account to fork the repository and submit pull requests. 
* **Python:** You should have a good understanding of Python programming. If you're new to Python, there are many resources available online to get you started. You can find the official documentation on the [Python website](https://www.python.org/doc/).
* **gRPC Concepts:** Familiarize yourself with the basics of gRPC, including concepts like protocol buffers, services, clients, and servers. Refer to the [gRPC documentation](https://grpc.io/docs/) for an overview.
* **Bazel:** Bazel is one of the build systems used for gRPC. To install it, follow the instructions for your operating system on the [Bazel website](https://bazel.build/install).

### Steps to Contributing to gRPC Python

1. **Find an Issue:**
   * **Browse Open Issues:** Look for issues labeled "[help wanted](https://github.com/grpc/grpc/issues?q=is%3Aopen+label%3A%22disposition%2Fhelp+wanted%22)" on the [gRPC Python issue tracker](https://github.com/grpc/grpc/issues?q=is%3Aissue+is%3Aopen+label%3Alang%2Fpython+sort%3Aupdated-desc).
   * **Ask for Help:** If you're unsure where to start or need clarification on an issue, feel free to ask questions on the [gRPC forum](https://groups.google.com/g/grpc-io).
2. **Get a Copy of the gRPC Python Development Version:**
   * **Fork:**  Click the "Fork" button on the top right of the [gRPC repository page](https://github.com/grpc/grpc) to create a copy of the repository under your GitHub account.
   * **Clone your fork:**
   ```git clone https://github.com/<your-username>/grpc.git```
   * **Initialize and Update Submodules:**
   ```bash
   cd grpc
   git submodule update --init --recursive 
   ```
   * **Create a Branch:** Make your changes on a new branch: 
   ```git checkout -b my-feature-branch```
3. **Setting up Your Local System for Development and Testing:**
   * Create a new virtual environment by running:```python -m venv ~/.virtualenvs/grpc-python```
   * Activate the environment: ```source ~/.virtualenvs/grpc-python/bin/activate```
   * Install Dependencies: ```pip install -r requirements.txt```
4. **Run Tests:**
   * Before making any changes, run the existing test suite to ensure your environment is set up correctly:
   * **Using Bazel (Recommended):**
     * To run a single unit test:
       ```bash
       bazel test --cache_test_results=no "//src/python/grpcio_tests/tests/unit:_abort_test" 
       ```
     * To execute all unit tests for Python:
       ```bash
       bazel test --cache_test_results=no "//src/python/..." 
       ```
   * **Using Provided Scripts (Alternative):**
     * Install Python Modules:
       ```bash
       ./tools/distrib/install_all_python_modules.sh
       ```
     * Run Tooling Tests:
       ```bash
       ./tools/distrib/run_python_tooling_tests.sh
       ```
   * **Verify No Failures:** Make sure all tests pass before submitting your patch.
5. **Commit & Push Changes:**
    * **Commit:** `git commit -m "Add new feature: brief description"`  (Make sure your commit message is clear and concise.)
    * **Push:** `git push origin my-feature-branch`
6. **Open a Pull Request (PR):**
    * **Go to GitHub:** Visit the original gRPC Python repository on GitHub.
    * **Click "New Pull Request":** Compare your branch with the main branch and submit your pull request.
    * **Provide a Description:**  Write a clear explanation of your changes, referencing the relevant issue(s).
7. **Code Review:**
    * **Wait for Feedback:** Maintainers will review your PR and provide feedback.
    * **Respond to Comments:**  Address any questions or concerns raised during the review.
    * **Make Revisions:** Update your code as needed based on the feedback.

## Code Style

* **Pythonic Code:** Follow the [PEP 8 style guide](https://www.python.org/dev/peps/pep-0008/) for Python code.
* **Type Hints:** Use type hints to improve code readability and maintainability.
* **Formatting:** Use [Black](https://black.readthedocs.io/en/stable/) for automatic code formatting.

