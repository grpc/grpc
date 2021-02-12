#!/usr/bin/env python3

from subprocess import check_output
import glob
import os
import shutil

def generateProtobufs(output):
  bazel_bin = check_output(['bazel', 'info', 'bazel-bin']).decode().strip()

  go_protos = check_output([
      'bazel',
      'query',
      'kind("go_proto_library", ...)',
  ]).split()

  check_output(['bazel', 'build', '-c', 'fastbuild'] + go_protos)

  for rule in go_protos:
    rule_dir, proto = rule.decode()[2:].rsplit(':', 1)
    input_dir = os.path.join(bazel_bin, rule_dir, 'linux_amd64_stripped',
                             proto + '%', 'github.com/cncf/udpa/go', rule_dir)
    input_files = glob.glob(os.path.join(input_dir, '*.go'))
    output_dir = os.path.join(output, rule_dir)

    # Ensure the output directory exists
    os.makedirs(output_dir, 0o755, exist_ok=True)
    for generated_file in input_files:
      output_file = shutil.copy(generated_file, output_dir)
      os.chmod(output_file, 0o644)


if __name__ == "__main__":
  workspace = check_output(['bazel', 'info', 'workspace']).decode().strip()
  output = os.path.join(workspace, 'go')
  generateProtobufs(output)
