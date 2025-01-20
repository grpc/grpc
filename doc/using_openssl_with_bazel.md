To use system installation of openssl with bazel, Please follow the below steps:

 1. You need to have the header files installed (in /usr/include/openssl) and during runtime, the shared library must be installed.
 2. export CC=gcc
 3. export BAZEL_CACHE=$(bazel info output_base)/external
 4. bazel build :all --override_repository=boringssl=$BAZEL_CACHE/openssl
 5. While testing please add this flag while using openssl with bazel,--override_repository=google_cloud_cpp=$BAZEL_CACHE/google_cloud_cpp_openssl along with bazel build :all --override_repository=boringssl=$BAZEL_CACHE/openssl
 6. The above command will pick openssl from bazel cache.
