REPOSITORY_LOCATIONS = dict(
    bazel_gazelle = dict(
        sha256 = "be9296bfd64882e3c08e3283c58fcb461fa6dd3c171764fcc4cf322f60615a9b",
        urls = ["https://github.com/bazelbuild/bazel-gazelle/releases/download/0.18.1/bazel-gazelle-0.18.1.tar.gz"],
    ),
    bazel_skylib = dict(
        sha256 = "2ef429f5d7ce7111263289644d233707dba35e39696377ebab8b0bc701f7818e",
        urls = ["https://github.com/bazelbuild/bazel-skylib/releases/download/0.8.0/bazel-skylib.0.8.0.tar.gz"],
    ),
    com_envoyproxy_protoc_gen_validate = dict(
        sha256 = "ddefe3dcbb25d68a2e5dfea67d19c060959c2aecc782802bd4c1a5811d44dd45",
        strip_prefix = "protoc-gen-validate-2feaabb13a5d697b80fcb938c0ce37b24c9381ee",  # Jul 26, 2018
        urls = ["https://github.com/envoyproxy/protoc-gen-validate/archive/2feaabb13a5d697b80fcb938c0ce37b24c9381ee.tar.gz"],
    ),
    com_github_grpc_grpc = dict(
        sha256 = "bcb01ac7029a7fb5219ad2cbbc4f0a2df3ef32db42e236ce7814597f4b04b541",
        strip_prefix = "grpc-79a8b5289e3122d2cea2da3be7151d37313d6f46",
        # Commit from 2019-05-30
        urls = ["https://github.com/grpc/grpc/archive/79a8b5289e3122d2cea2da3be7151d37313d6f46.tar.gz"],
    ),
    com_google_googleapis = dict(
        # TODO(dio): Consider writing a Skylark macro for importing Google API proto.
        sha256 = "c1969e5b72eab6d9b6cfcff748e45ba57294aeea1d96fd04cd081995de0605c2",
        strip_prefix = "googleapis-be480e391cc88a75cf2a81960ef79c80d5012068",
        urls = ["https://github.com/googleapis/googleapis/archive/be480e391cc88a75cf2a81960ef79c80d5012068.tar.gz"],
    ),
    com_google_protobuf = dict(
        sha256 = "b7220b41481011305bf9100847cf294393973e869973a9661046601959b2960b",
        strip_prefix = "protobuf-3.8.0",
        urls = ["https://github.com/protocolbuffers/protobuf/releases/download/v3.8.0/protobuf-all-3.8.0.tar.gz"],
    ),
    io_bazel_rules_go = dict(
        sha256 = "96b1f81de5acc7658e1f5a86d7dc9e1b89bc935d83799b711363a748652c471a",
        urls = ["https://github.com/bazelbuild/rules_go/releases/download/0.19.2/rules_go-0.19.2.tar.gz"],
    ),
    rules_foreign_cc = dict(
        sha256 = "c957e6663094a1478c43330c1bbfa71afeaf1ab86b7565233783301240c7a0ab",
        strip_prefix = "rules_foreign_cc-a209b642c7687a8894c19b3dd40e43e6d3f38e83",
        # 2019-07-17
        urls = ["https://github.com/bazelbuild/rules_foreign_cc/archive/a209b642c7687a8894c19b3dd40e43e6d3f38e83.tar.gz"],
    ),
    rules_proto = dict(
        sha256 = "73ebe9d15ba42401c785f9d0aeebccd73bd80bf6b8ac78f74996d31f2c0ad7a6",
        strip_prefix = "rules_proto-2c0468366367d7ed97a1f702f9cd7155ab3f73c5",
        # 2019-11-19
        urls = ["https://github.com/bazelbuild/rules_proto/archive/2c0468366367d7ed97a1f702f9cd7155ab3f73c5.tar.gz"],
    ),
    net_zlib = dict(
        sha256 = "629380c90a77b964d896ed37163f5c3a34f6e6d897311f1df2a7016355c45eff",
        strip_prefix = "zlib-1.2.11",
        urls = ["https://github.com/madler/zlib/archive/v1.2.11.tar.gz"],
    ),
    six_archive = dict(
        sha256 = "105f8d68616f8248e24bf0e9372ef04d3cc10104f1980f54d57b2ce73a5ad56a",
        urls = ["https://files.pythonhosted.org/packages/b3/b2/238e2590826bfdd113244a40d9d3eb26918bd798fc187e2360a8367068db/six-1.10.0.tar.gz"],
    ),
)
