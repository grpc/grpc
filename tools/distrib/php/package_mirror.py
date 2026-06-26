# Copyright 2026 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import os
import shutil
import xml.etree.ElementTree as ET


def main():
    # Parse package.xml to find all files to include in the mirror
    tree = ET.parse("package.xml")
    root = tree.getroot()

    # PEAR package.xml namespace
    ns = {"ns": "http://pear.php.net/dtd/package-2.0"}
    files = root.findall(".//ns:file", ns)

    dest_dir = "mirror"
    os.makedirs(dest_dir, exist_ok=True)

    for f in files:
        file_path = f.attrib.get("name")
        if not file_path:
            continue

        src = file_path
        dst = os.path.join(dest_dir, file_path)

        if os.path.exists(src):
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(src, dst)
            print(f"Copied: {src} -> {dst}")
        else:
            print(f"Warning: Source file not found: {src}")

    # Copy package.xml itself into the mirror for compatibility/reference
    shutil.copy2("package.xml", os.path.join(dest_dir, "package.xml"))
    print("Copied package.xml to mirror")


if __name__ == "__main__":
    main()
