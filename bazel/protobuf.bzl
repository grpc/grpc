"""Utility functions for generating protobuf code."""

_PROTO_EXTENSION = ".proto"

def get_proto_root(workspace_root):
    """Gets the root protobuf directory.

    Args:
      workspace_root: context.label.workspace_root

    Returns:
      The directory relative to which generated include paths should be.
    """
    if workspace_root:
        return "/{}".format(workspace_root)
    else:
        return ""

def _strip_proto_extension(proto_filename):
    if not proto_filename.endswith(_PROTO_EXTENSION):
        fail('"{}" does not end with "{}"'.format(
            proto_filename,
            _PROTO_EXTENSION,
        ))
    return proto_filename[:-len(_PROTO_EXTENSION)]

def proto_path_to_generated_filename(proto_path, fmt_str):
    """Calculates the name of a generated file for a protobuf path.

    For example, "examples/protos/helloworld.proto" might map to
      "helloworld.pb.h".

    Args:
      proto_path: The path to the .proto file.
      fmt_str: A format string used to calculate the generated filename. For
        example, "{}.pb.h" might be used to calculate a C++ header filename.

    Returns:
      The generated filename.
    """
    return fmt_str.format(_strip_proto_extension(proto_path))

def _get_include_directory(include):
    directory = include.path
    if directory.startswith("external"):
        external_separator = directory.find("/")
        repository_separator = directory.find("/", external_separator + 1)
        return directory[:repository_separator]
    else:
        return "."

def get_include_protoc_args(includes):
    """Returns protoc args that imports protos relative to their import root.

    Args:
      includes: A list of included proto files.

    Returns:
      A list of arguments to be passed to protoc. For example, ["--proto_path=."].
    """
    return [
        "--proto_path={}".format(_get_include_directory(include))
        for include in includes
    ]

def get_plugin_args(plugin, flags, dir_out, generate_mocks):
    """Returns arguments configuring protoc to use a plugin for a language.

    Args:
      plugin: An executable file to run as the protoc plugin.
      flags: The plugin flags to be passed to protoc.
      dir_out: The output directory for the plugin.
      generate_mocks: A bool indicating whether to generate mocks.

    Returns:
      A list of protoc arguments configuring the plugin.
    """
    augmented_flags = list(flags)
    if generate_mocks:
        augmented_flags.append("generate_mock_code=true")
    return [
        "--plugin=protoc-gen-PLUGIN=" + plugin.path,
        "--PLUGIN_out=" + ",".join(augmented_flags) + ":" + dir_out,
    ]
