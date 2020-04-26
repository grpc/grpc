import logging
import unittest


class ImportTest(unittest.TestCase):
    def test_import(self):
        from namespaced.upper.example.namespaced_example_pb2 import NamespacedExample
        namespaced_example = NamespacedExample()
        namespaced_example.value = "hello"
        # Dummy assert, important part is namespaced example was imported.
        self.assertEqual(namespaced_example.value, "hello")

    def test_grpc(self):
        from namespaced.upper.example.namespaced_example_pb2_grpc import NamespacedServiceStub
        # No error from import
        self.assertEqual(1, 1)


if __name__ == '__main__':
    logging.basicConfig()
    unittest.main()
