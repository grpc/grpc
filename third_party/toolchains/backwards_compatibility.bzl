# TODO: Flowerbox, etc.

load("@bazel_toolchains//rules/exec_properties:exec_properties.bzl", "create_exec_properties_dict", "create_rbe_exec_properties_dict","merge_dicts")

# TODO: Dedupe.
def bc_exec_properties(exec_properties):
    if True:
        d = dict(labels = exec_properties[1], **exec_properties[0])
        print(d)
        return create_rbe_exec_properties_dict(
            **d
        )
    else:
        labels_dict = {"label:" + key : exec_properties[1][key] for key in exec_properties[1]}
        return merge_dicts(
            create_exec_properties_dict(),
            labels_dict,
        )

def bc_platform(**kwargs):
    # TODO: Docstring.
    exec_properties = kwargs.pop("exec_properties", None)
    if len(exec_properties) != 2:
        # TODO: Expand on this.
        fail("exec_properties must be a list of size 2.")

    #if versions.get() > XXX:
    if True:
        d = dict(labels = exec_properties[1], **exec_properties[0])
        print(d)
        output_exec_properties = create_rbe_exec_properties_dict(
            **d
        )
    else:
        labels_dict = {"label:" + key : exec_properties[1][key] for key in exec_properties[1]}
        output_exec_properties = merge_dicts(
            create_exec_properties_dict(),
            labels_dict,
        )

    native.platform(
        exec_properties = output_exec_properties,
        **kwargs,
    )
