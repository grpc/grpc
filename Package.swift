// swift-tools-version:5.2
// The swift-tools-version declares the minimum version of Swift required to build this package.
import PackageDescription

let package = Package(
  name: "gRPC",
  products: [
    .library(
      name: "gRPC-Core",
      targets: [
        "gRPC-Core",
      ]
    ),
    .library(
      name: "gRPC-cpp",
      targets: [
        "gRPC-cpp",
      ]
    )
  ],

  dependencies: [
    .package(
      name: "abseil",
      url: "https://github.com/firebase/abseil-cpp-SwiftPM.git",
      "0.20220203.0"..<"0.20220204.0"
    ),
    .package(
      name: "BoringSSL-GRPC",
      url: "https://github.com/firebase/boringssl-SwiftPM.git",
      "0.9.0"..<"0.10.0"
    ),
  ],

  targets: [
    .target(
      name: "gRPC-Core",
      dependencies: [
        .product(name:"abseil", package: "abseil"),
        .product(name:"openssl_grpc", package: "BoringSSL-GRPC"),
      ],
      path: ".",
      exclude: [
        "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_channel.cc",
        "src/core/ext/filters/load_reporting/",
        "src/core/ext/transport/cronet/",
        "src/core/ext/xds/google_mesh_ca_certificate_provider_factory.h",
        "src/core/ext/xds/google_mesh_ca_certificate_provider_factory.cc",
        "src/core/lib/surface/init_unsecure.cc",
        "src/core/lib/security/authorization/authorization_policy_provider_null_vtable.cc",
        "src/core/plugin_registry/grpc_unsecure_plugin_registry.cc",
        "third_party/re2/re2/testing/",
        "third_party/re2/re2/fuzzing/",
        "third_party/re2/util/benchmark.cc",
        "third_party/re2/util/test.cc",
        "third_party/re2/util/fuzz.cc",
        "third_party/upb/upb/bindings/",
        "third_party/upb/upb/msg_test.cc",
      ],
      sources: [
        "src/core/ext/filters/",
        "src/core/ext/transport/",
        "src/core/ext/upb-generated/",
        "src/core/ext/upbdefs-generated/",
        "src/core/ext/xds/",
        "src/core/lib/",
        "src/core/plugin_registry/grpc_plugin_registry.cc",
        "src/core/tsi/",
        "third_party/re2/re2/",
        "third_party/re2/util/",
        "third_party/upb/upb/",
        "third_party/xxhash/xxhash.h",
      ],
      publicHeadersPath: "spm-core-include",
      cSettings: [
        .headerSearchPath("./"),
        .headerSearchPath("include/"),
        .headerSearchPath("third_party/re2/"),
        .headerSearchPath("third_party/upb/"),
        .headerSearchPath("third_party/xxhash/"),
        .headerSearchPath("src/core/ext/upb-generated/"),
        .headerSearchPath("src/core/ext/upbdefs-generated/"),
        .define("GRPC_ARES", to: "0"),
      ],
      linkerSettings: [
        .linkedFramework("CoreFoundation"),
        .linkedLibrary("z"),
      ]
    ),
    .target(
      name: "gRPC-cpp",
      dependencies: [
        .product(name:"abseil", package: "abseil"),
        "gRPC-Core",
      ],
      path: ".",
      exclude: [
        "src/cpp/client/cronet_credentials.cc",
        "src/cpp/client/channel_test_peer.cc",
        "src/cpp/common/alts_util.cc",
        "src/cpp/common/alts_context.cc",
        "src/cpp/common/insecure_create_auth_context.cc",
        "src/cpp/server/admin/",
        "src/cpp/server/channelz/",
        "src/cpp/server/csds/",
        "src/cpp/server/load_reporter/",
        "src/cpp/ext/",
        "src/cpp/util/core_stats.cc",
        "src/cpp/util/core_stats.h",
        "src/cpp/util/error_details.cc",
      ],
      sources: [
        "src/cpp/",
      ],
      publicHeadersPath: "spm-cpp-include",
      cSettings: [
        .headerSearchPath("./"),
        .headerSearchPath("include/"),
        .headerSearchPath("third_party/upb/"),
        .headerSearchPath("src/core/ext/upb-generated"),
      ]
    ),
    .testTarget(
      name: "build-test",
      dependencies: [
        "gRPC-cpp",
      ],
      path: "test/spm_build"
    ),
  ],
  cLanguageStandard: .gnu11,
  cxxLanguageStandard: .cxx14
)
