choco install bazel -y --version 0.23.2
cd github/grpc
bazel
set PATH=%PATH%;C:\python27\
python --version
c:\python27\python.exe --version
bazel --bazelrc=tools/remote_build/windows.bazelrc build :all --incompatible_disallow_filetype=false --google_credentials=%KOKORO_GFILE_DIR%/rbe-windows-credentials.json