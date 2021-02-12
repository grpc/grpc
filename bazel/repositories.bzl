load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:jvm.bzl", "jvm_maven_import_external")

MAVEN_SERVER_URLS = ["https://repo.maven.apache.org/maven2"]

def pgv_dependencies():
    if not native.existing_rule("io_bazel_rules_go"):
        http_archive(
            name = "io_bazel_rules_go",
            sha256 = "6f111c57fd50baf5b8ee9d63024874dd2a014b069426156c55adbf6d3d22cb7b",
            urls = [
                "https://mirror.bazel.build/github.com/bazelbuild/rules_go/releases/download/v0.25.0/rules_go-v0.25.0.tar.gz",
                "https://github.com/bazelbuild/rules_go/releases/download/v0.25.0/rules_go-v0.25.0.tar.gz",
            ],
        )

    if not native.existing_rule("bazel_gazelle"):
        http_archive(
            name = "bazel_gazelle",
            sha256 = "b85f48fa105c4403326e9525ad2b2cc437babaa6e15a3fc0b1dbab0ab064bc7c",
            urls = [
                "https://mirror.bazel.build/github.com/bazelbuild/bazel-gazelle/releases/download/v0.22.2/bazel-gazelle-v0.22.2.tar.gz",
                "https://github.com/bazelbuild/bazel-gazelle/releases/download/v0.22.2/bazel-gazelle-v0.22.2.tar.gz",
            ],
        )

    if not native.existing_rule("com_google_protobuf"):
        http_archive(
            name = "com_google_protobuf",
            url = "https://github.com/protocolbuffers/protobuf/archive/v3.11.4.tar.gz",
            sha256 = "a79d19dcdf9139fa4b81206e318e33d245c4c9da1ffed21c87288ed4380426f9",
            strip_prefix = "protobuf-3.11.4",
        )

    # TODO(akonradi): This shouldn't be necesary since the same http_archive block is imported by
    # protobuf_deps from @com_google_protobuf. Investigate why.
    if not native.existing_rule("zlib"):
        http_archive(
            name = "zlib",
            build_file = "@com_google_protobuf//:third_party/zlib.BUILD",
            sha256 = "c3e5e9fdd5004dcb542feda5ee4f0ff0744628baf8ed2dd5d66f8ca1197cb1a1",
            strip_prefix = "zlib-1.2.11",
            urls = ["https://zlib.net/zlib-1.2.11.tar.gz"],
        )

    if not native.existing_rule("bazel_skylib"):
        http_archive(
            name = "bazel_skylib",
            sha256 = "e5d90f0ec952883d56747b7604e2a15ee36e288bb556c3d0ed33e818a4d971f2",
            strip_prefix = "bazel-skylib-1.0.2",
            urls = ["https://github.com/bazelbuild/bazel-skylib/archive/1.0.2.tar.gz"],
        )

    if not native.existing_rule("six"):
        http_archive(
            name = "six",
            build_file = "@com_google_protobuf//:third_party/six.BUILD",
            sha256 = "d16a0141ec1a18405cd4ce8b4613101da75da0e9a7aec5bdd4fa804d0e0eba73",
            urls = ["https://pypi.python.org/packages/source/s/six/six-1.12.0.tar.gz"],
        )

    if not native.existing_rule("com_google_re2j"):
        jvm_maven_import_external(
            name = "com_google_re2j",
            artifact = "com.google.re2j:re2j:1.2",
            artifact_sha256 = "e9dc705fd4c570344b54a7146b2e3a819cdc271a29793f4acc1a93b56a388e59",
            server_urls = MAVEN_SERVER_URLS,
        )

    if not native.existing_rule("com_googlesource_code_re2"):
        http_archive(
            name = "com_googlesource_code_re2",
            sha256 = "2e9489a31ae007c81e90e8ec8a15d62d58a9c18d4fd1603f6441ef248556b41f",
            strip_prefix = "re2-2020-07-06",
            urls = ["https://github.com/google/re2/archive/2020-07-06.tar.gz"],
        )

    if not native.existing_rule("com_google_guava"):
        jvm_maven_import_external(
            name = "com_google_guava",
            artifact = "com.google.guava:guava:27.0-jre",
            artifact_sha256 = "63b09db6861011e7fb2481be7790c7fd4b03f0bb884b3de2ecba8823ad19bf3f",
            server_urls = MAVEN_SERVER_URLS,
        )

    if not native.existing_rule("guava"):
        native.bind(
            name = "guava",
            actual = "@com_google_guava//jar",
        )

    if not native.existing_rule("com_google_gson"):
        jvm_maven_import_external(
            name = "com_google_gson",
            artifact = "com.google.code.gson:gson:2.8.5",
            artifact_sha256 = "233a0149fc365c9f6edbd683cfe266b19bdc773be98eabdaf6b3c924b48e7d81",
            server_urls = MAVEN_SERVER_URLS,
        )

    if not native.existing_rule("gson"):
        native.bind(
            name = "gson",
            actual = "@com_google_gson//jar",
        )

    if not native.existing_rule("error_prone_annotations_maven"):
        jvm_maven_import_external(
            name = "error_prone_annotations_maven",
            artifact = "com.google.errorprone:error_prone_annotations:2.3.2",
            artifact_sha256 = "357cd6cfb067c969226c442451502aee13800a24e950fdfde77bcdb4565a668d",
            server_urls = MAVEN_SERVER_URLS,
        )

    if not native.existing_rule("error_prone_annotations"):
        native.bind(
            name = "error_prone_annotations",
            actual = "@error_prone_annotations_maven//jar",
        )

    if not native.existing_rule("org_apache_commons_validator"):
        jvm_maven_import_external(
            name = "org_apache_commons_validator",
            artifact = "commons-validator:commons-validator:1.6",
            artifact_sha256 = "bd62795d7068a69cbea333f6dbf9c9c1a6ad7521443fb57202a44874f240ba25",
            server_urls = MAVEN_SERVER_URLS,
        )

    if not native.existing_rule("io_bazel_rules_python"):
        git_repository(
            name = "io_bazel_rules_python",
            remote = "https://github.com/bazelbuild/rules_python.git",
            commit = "fdbb17a4118a1728d19e638a5291b4c4266ea5b8",
            shallow_since = "1557865590 -0400",
        )

    if not native.existing_rule("rules_proto"):
        http_archive(
            name = "rules_proto",
            sha256 = "2490dca4f249b8a9a3ab07bd1ba6eca085aaf8e45a734af92aad0c42d9dc7aaf",
            strip_prefix = "rules_proto-218ffa7dfa5408492dc86c01ee637614f8695c45",
            urls = ["https://github.com/bazelbuild/rules_proto/archive/218ffa7dfa5408492dc86c01ee637614f8695c45.tar.gz"],
        )
