package(default_visibility = ["//visibility:public"])

licenses(["notice"])


sh_binary(
	name = "update_includes",
	srcs = ["update_includes.sh"]
)

genrule(
	name = "node_generator_cc",
	srcs = ["@grpc_grpc_node//:node_generator_cc"],
	cmd = "$(location :update_includes) $< $@",
	tools = [":update_includes"],
	outs = ["src/compiler/node_generator.cc"]
)

genrule(
	name = "node_plugin_cc",
	srcs = ["@grpc_grpc_node//:node_plugin_cc"],
	cmd = "$(location :update_includes) $< $@",
	tools = [":update_includes"],
	outs = ["src/compiler/node_plugin.cc"]
)


genrule(
	name = "node_generator_h",
	srcs = ["@grpc_grpc_node//:node_generator_h"],
	cmd = "$(location :update_includes) $< $@",
	tools = [":update_includes"],
	outs = ["node_generator.h"]
)

genrule(
	name = "node_generator_helpers_h",
	srcs = ["@grpc_grpc_node//:node_generator_helpers_h"],
	cmd = "$(location :update_includes) $< $@",
	tools = [":update_includes"],
	outs = ["node_generator_helpers.h"]
)
