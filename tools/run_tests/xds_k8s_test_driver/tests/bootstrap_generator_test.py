# Copyright 2022 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
import datetime
import logging
from typing import List

from absl import flags
from absl.testing import absltest
from absl.testing import parameterized

from framework import bootstrap_generator_testcase
from framework import xds_k8s_testcase
from framework.helpers import retryers
from framework.test_app.runners.k8s import k8s_xds_client_runner
from framework.test_app.runners.k8s import k8s_xds_server_runner

logger = logging.getLogger(__name__)
flags.adopt_module_key_flags(xds_k8s_testcase)

# Type aliases
XdsTestServer = xds_k8s_testcase.XdsTestServer
XdsTestClient = xds_k8s_testcase.XdsTestClient
KubernetesServerRunner = k8s_xds_server_runner.KubernetesServerRunner
KubernetesClientRunner = k8s_xds_client_runner.KubernetesClientRunner
_timedelta = datetime.timedelta


# Returns a list of bootstrap generator versions to be tested along with their
# image names.
#
# Whenever we release a new version of the bootstrap generator, we need to add a
# corresponding entry here.
#
# TODO: Update bootstrap generator release instructions to add an entry here,
# after the release is published.
def bootstrap_version_testcases() -> List:
    return (
        dict(
            version="v0.14.0",
            image="gcr.io/grpc-testing/td-grpc-bootstrap:d6baaf7b0e0c63054ac4d9bedc09021ff261d599",
        ),
        dict(
            version="v0.13.0",
            image="gcr.io/grpc-testing/td-grpc-bootstrap:203db6ce70452996f4183c30dd4c5ecaada168b0",
        ),
        dict(
            version="v0.12.0",
            image="gcr.io/grpc-testing/td-grpc-bootstrap:8765051ef3b742bc5cd20f16de078ae7547f2ba2",
        ),
        dict(
            version="v0.11.0",
            image="gcr.io/grpc-testing/td-grpc-bootstrap:b96f7a73314668aee83cbf86ab1e40135a0542fc",
        ),
        # v0.10.0 uses v2 xDS transport protocol by default. TD only supports v3
        # and we can force the bootstrap generator to emit config with v3
        # support by setting the --include-v3-features-experimental flag to
        # true.
        #
        # TODO: Figure out how to pass flags to the bootstrap generator via the
        # client and server runners, and uncomment this version.
        # ('v0.10.0', 'gcr.io/grpc-testing/td-grpc-bootstrap:66de7ea0e170351c9fae17232b81adbfb3e80ec3'),
    )


# TODO: Reuse service account and namespaces for significant improvements in
# running time.
class BootstrapGeneratorClientTest(
    bootstrap_generator_testcase.BootstrapGeneratorBaseTest,
    parameterized.TestCase,
):
    client_runner: KubernetesClientRunner
    server_runner: KubernetesServerRunner
    test_server: XdsTestServer

    @classmethod
    def setUpClass(cls):
        """Hook method for setting up class fixture before running tests in
        the class.
        """
        super().setUpClass()

        # For client tests, we use a single server instance that can be shared
        # across all the parameterized clients. And this server runner will use
        # the version of the bootstrap generator as configured via the
        # --td_bootstrap_image flag.
        cls.server_runner = cls.initKubernetesServerRunner()
        cls.test_server = cls.startTestServer(
            server_runner=cls.server_runner,
            port=cls.server_port,
            maintenance_port=cls.server_maintenance_port,
            xds_host=cls.server_xds_host,
            xds_port=cls.server_xds_port,
        )

        # Load backends.
        neg_name, neg_zones = cls.server_runner.k8s_namespace.get_service_neg(
            cls.server_runner.service_name, cls.server_port
        )

        # Add backends to the Backend Service.
        cls.td.backend_service_add_neg_backends(neg_name, neg_zones)
        cls.td.wait_for_backends_healthy_status()

    @classmethod
    def tearDownClass(cls):
        # Remove backends from the Backend Service before closing the server
        # runner.
        neg_name, neg_zones = cls.server_runner.k8s_namespace.get_service_neg(
            cls.server_runner.service_name, cls.server_port
        )
        cls.td.backend_service_remove_neg_backends(neg_name, neg_zones)
        cls.server_runner.cleanup(force=cls.force_cleanup)
        super().tearDownClass()

    def tearDown(self):
        logger.info("----- TestMethod %s teardown -----", self.id())
        retryer = retryers.constant_retryer(
            wait_fixed=_timedelta(seconds=10),
            attempts=3,
            log_level=logging.INFO,
        )
        try:
            retryer(self._cleanup)
        except retryers.RetryError:
            logger.exception("Got error during teardown")
        super().tearDown()

    def _cleanup(self):
        self.client_runner.cleanup(force=self.force_cleanup)

    @parameterized.parameters(
        (t["version"], t["image"]) for t in bootstrap_version_testcases()
    )
    def test_baseline_in_client_with_bootstrap_version(self, version, image):
        """Runs the baseline test for multiple versions of the bootstrap
        generator on the client.
        """
        logger.info(
            "----- testing bootstrap generator version %s -----", version
        )
        self.client_runner = self.initKubernetesClientRunner(
            td_bootstrap_image=image
        )
        test_client: XdsTestClient = self.startTestClient(self.test_server)
        self.assertXdsConfigExists(test_client)
        self.assertSuccessfulRpcs(test_client)


# TODO: Use unique client and server deployment names while creating the
# corresponding runners, by suffixing the version of the bootstrap generator
# being tested. Then, run these in parallel.
class BootstrapGeneratorServerTest(
    bootstrap_generator_testcase.BootstrapGeneratorBaseTest,
    parameterized.TestCase,
):
    client_runner: KubernetesClientRunner
    server_runner: KubernetesServerRunner
    test_server: XdsTestServer

    def tearDown(self):
        logger.info("----- TestMethod %s teardown -----", self.id())
        retryer = retryers.constant_retryer(
            wait_fixed=_timedelta(seconds=10),
            attempts=3,
            log_level=logging.INFO,
        )
        try:
            retryer(self._cleanup)
        except retryers.RetryError:
            logger.exception("Got error during teardown")
        super().tearDown()

    def _cleanup(self):
        self.client_runner.cleanup(force=self.force_cleanup)
        self.removeServerBackends()
        self.server_runner.cleanup(force=self.force_cleanup)

    @parameterized.parameters(
        (t["version"], t["image"]) for t in bootstrap_version_testcases()
    )
    def test_baseline_in_server_with_bootstrap_version(self, version, image):
        """Runs the baseline test for multiple versions of the bootstrap
        generator on the server.
        """
        logger.info(
            "----- Testing bootstrap generator version %s -----", version
        )
        self.server_runner = self.initKubernetesServerRunner(
            td_bootstrap_image=image
        )
        self.test_server = self.startTestServer(
            server_runner=self.server_runner,
            port=self.server_port,
            maintenance_port=self.server_maintenance_port,
            xds_host=self.server_xds_host,
            xds_port=self.server_xds_port,
            bootstrap_version=version,
        )

        # Load backends.
        neg_name, neg_zones = self.server_runner.k8s_namespace.get_service_neg(
            self.server_runner.service_name, self.server_port
        )

        # Add backends to the Backend Service.
        self.td.backend_service_add_neg_backends(neg_name, neg_zones)
        self.td.wait_for_backends_healthy_status()

        self.client_runner = self.initKubernetesClientRunner()
        test_client: XdsTestClient = self.startTestClient(self.test_server)
        self.assertXdsConfigExists(test_client)
        self.assertSuccessfulRpcs(test_client)


if __name__ == "__main__":
    absltest.main()
