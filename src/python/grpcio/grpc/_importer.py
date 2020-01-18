# TODO: Flowerbox, etc.

import sys
import six

def _uninstalled_protos(*args, **kwargs):
    raise NotImplementedError(
        "Install the protobuf package to use the protos function.")


def _uninstalled_services(*args, **kwargs):
    raise NotImplementedError(
        "Install the protobuf package to use the services function.")


def _uninstalled_protos_and_services(*args, **kwargs):
    raise NotImplementedError(
        "Install the protobuf package to use the protos_and_services function."
    )


def _interpreter_version_protos(*args, **kwargs):
    raise NotImplementedError(
        "The protos function is only on available on Python 3.X interpreters.")


def _interpreter_version_services(*args, **kwargs):
    raise NotImplementedError(
        "The services function is only on available on Python 3.X interpreters."
    )


def _interpreter_version_protos_and_services(*args, **kwargs):
    raise NotImplementedError(
        "The protos_and_services function is only on available on Python 3.X interpreters."
    )


# TODO: Remove the now-unnecessary implemenentation from grpc_tools.

if sys.version_info[0] < 3:
    protos = _interpreter_version_protos
    services = _interpreter_version_services
    protos_and_services = _interpreter_version_protos_and_services
else:
    try:
        from google import protobuf
    except (ModuleNotFoundError, ImportError) as e:
        # NOTE: It's possible that we're encountering a transitive ImportError, so
        # we check for that and re-raise if so.
        if "google" not in e.args[0]:
            raise e
        protos = _uninstalled_protos
        services = _uninstalled_services
        protos_and_services = _uninstalled_protos_and_services
    else:
        from google.protobuf import protos

        import contextlib
        import importlib
        import importlib.machinery
        import os

        from grpc import _service_reflection

        _PROTO_MODULE_SUFFIX = "_pb2_grpc"

        def _module_name_to_proto_file(module_name):
            components = module_name.split(".")
            proto_name = components[-1][:-1 * len(_PROTO_MODULE_SUFFIX)]
            return os.path.sep.join(components[:-1] + [proto_name + ".proto"])

        def _proto_file_to_module_name(proto_file):
            components = proto_file.split(os.path.sep)
            proto_base_name = os.path.splitext(components[-1])[0]
            return ".".join(components[:-1] + [proto_base_name + _PROTO_MODULE_SUFFIX])

        @contextlib.contextmanager
        def _augmented_syspath(new_paths):
            original_sys_path = sys.path
            if new_paths is not None:
                sys.path = sys.path + new_paths
            try:
                yield
            finally:
                sys.path = original_sys_path

        class ProtoLoader(importlib.abc.Loader):

            def __init__(self, module_name, protobuf_path):
                self._module_name = module_name
                self._protobuf_path = protobuf_path

            def create_module(self, spec):
                return None

            def _generated_file_to_module_name(self, filepath):
                components = filepath.split(os.path.sep)
                return ".".join(
                    components[:-1] + [os.path.splitext(components[-1])[0]])

            def exec_module(self, module):
                """Instantiate a module identical to the generated version.
                """
                # NOTE(rbellevi): include_paths are propagated via sys.path.
                proto_module = protos(self._protobuf_path)
                file_descriptor = getattr(proto_module, _service_reflection.DESCRIPTOR_KEY)
                for service_name, service_descriptor in six.iteritems(file_descriptor.services_by_name):
                    setattr(module,
                            _service_reflection.get_stub_class_name(service_descriptor),
                            _service_reflection.get_stub_type(service_descriptor))
                    setattr(module,
                            _service_reflection.get_servicer_class_name(service_descriptor),
                            _service_reflection.get_servicer_type(service_descriptor))
                    setattr(module,
                            _service_reflection.get_servicer_addition_function_name(service_descriptor),
                            _service_reflection.get_servicer_addition_function(service_descriptor))


        class ProtoFinder(importlib.abc.MetaPathFinder):

            def find_spec(self, fullname, path, target=None):
                filepath = _module_name_to_proto_file(fullname)
                for search_path in sys.path:
                    try:
                        prospective_path = os.path.join(search_path, filepath)
                        os.stat(prospective_path)
                    except (FileNotFoundError, NotADirectoryError):
                        continue
                    else:
                        return importlib.machinery.ModuleSpec(
                            fullname,
                            ProtoLoader(fullname, filepath))


        def services(protobuf_path, *, include_paths=None):
            with _augmented_syspath(include_paths):
                module_name = _proto_file_to_module_name(protobuf_path)
                module = importlib.import_module(module_name)
                return module


        def protos_and_services(protobuf_path, *, include_paths=None):
            protos = protobuf.protos(protobuf_path, include_paths=include_paths)
            services_ = services(protobuf_path, include_paths=include_paths)
            return protos, services_


        sys.meta_path.extend([ProtoFinder()])
