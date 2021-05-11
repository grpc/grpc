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
      .revision("05d8107f2971a37e6c77245b7c4c6b0a7e97bc99")
    ),
    .package(name: "BoringSSL-GRPC",
      url: "https://github.com/firebase/boringssl-SwiftPM.git",
      .branch("7bcafa2660bc58715c39637494550d1ed7cd7229")
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
        "src/core/ext/filters/client_channel/xds/xds_channel.cc",
        "src/core/ext/filters/load_reporting/",
        "src/core/ext/transport/cronet/",
        "src/core/ext/upb-generated/third_party/",
        "src/core/ext/upb-generated/src/proto/grpc/auth/",
        "src/core/ext/upbdefs-generated/envoy/config/rbac/",
        "src/core/ext/upbdefs-generated/google/api/expr/",
        "src/core/ext/upbdefs-generated/src/",
        "src/core/ext/upbdefs-generated/third_party/",
        "src/core/ext/upbdefs-generated/udpa/data/",
        "src/core/ext/xds/google_mesh_ca_certificate_provider_factory.h",
        "src/core/ext/xds/google_mesh_ca_certificate_provider_factory.cc",
        "src/core/lib/surface/init_unsecure.cc",
        "src/core/lib/security/authorization/",
        "src/core/plugin_registry/grpc_unsecure_plugin_registry.cc",
        "third_party/re2/re2/testing/",
        "third_party/re2/re2/fuzzing/",
        "third_party/re2/util/benchmark.cc",
        "third_party/re2/util/test.cc",
        "third_party/re2/util/fuzz.cc",
        "third_party/upb/upb/sink.c",
        "third_party/upb/upb/json_decode.c",
        "third_party/upb/upb/json_encode.c",
        "third_party/upb/upb/handlers.h",
        "third_party/upb/upb/sink.h",
        "third_party/upb/upb/json_encode.h",
        "third_party/upb/upb/json_decode.h",
        "third_party/upb/upb/handlers-inl.h",
        "third_party/upb/upb/handlers.c",
        "third_party/upb/upb/bindings/",
        "third_party/upb/upb/json/",
        "third_party/upb/upb/pb/",
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
        "third_party/upb/third_party/wyhash/wyhash.h",
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
        .unsafeFlags(["-Wno-module-import-in-extern-c"]),
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
        .unsafeFlags(["-Wno-module-import-in-extern-c"]),
      ]
    ),
  ],
  cLanguageStandard: .gnu11,
  cxxLanguageStandard: .cxx11
)
