#!/bin/bash

# Run this script in the top nanopb directory to create a binary package
# for Windows users. This script is designed to run under MingW/MSYS bash
# and requires the following tools: git, make, zip, unix2dos

set -e
set -x

VERSION=`git describe --always`-windows-x86
DEST=dist/$VERSION

rm -rf $DEST
mkdir -p $DEST

# Export the files from newest commit
git archive HEAD | tar x -C $DEST

# Rebuild the Python .proto files
make -BC $DEST/generator/proto

# Make the nanopb generator available as a protoc plugin
cp $DEST/generator/nanopb_generator.py $DEST/generator/protoc-gen-nanopb.py

# Package the Python libraries
( cd $DEST/generator; bbfreeze nanopb_generator.py protoc-gen-nanopb.py )
mv $DEST/generator/dist $DEST/generator-bin

# Remove temp file
rm $DEST/generator/protoc-gen-nanopb.py

# The python interpreter requires MSVCR90.dll.
# FIXME: Find a way around hardcoding this path
cp /c/windows/winsxs/x86_microsoft.vc90.crt_1fc8b3b9a1e18e3b_9.0.30729.4974_none_50940634bcb759cb/MSVCR90.DLL $DEST/generator-bin/
cat > $DEST/generator-bin/Microsoft.VC90.CRT.manifest <<EOF
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
    <noInheritable></noInheritable>
    <assemblyIdentity type="win32" name="Microsoft.VC90.CRT" version="9.0.21022.8" processorArchitecture="x86" publicKeyToken="1fc8b3b9a1e18e3b"></assemblyIdentity>
    <file name="msvcr90.dll" hashalg="SHA1" hash="e0dcdcbfcb452747da530fae6b000d47c8674671"><asmv2:hash xmlns:asmv2="urn:schemas-microsoft-com:asm.v2" xmlns:dsig="http://www.w3.org/2000/09/xmldsig#"><dsig:Transforms><dsig:Transform Algorithm="urn:schemas-microsoft-com:HashTransforms.Identity"></dsig:Transform></dsig:Transforms><dsig:DigestMethod Algorithm="http://www.w3.org/2000/09/xmldsig#sha1"></dsig:DigestMethod><dsig:DigestValue>KSaO8M0iCtPF6YEr79P1dZsnomY=</dsig:DigestValue></asmv2:hash></file> <file name="msvcp90.dll" hashalg="SHA1" hash="81efe890e4ef2615c0bb4dda7b94bea177c86ebd"><asmv2:hash xmlns:asmv2="urn:schemas-microsoft-com:asm.v2" xmlns:dsig="http://www.w3.org/2000/09/xmldsig#"><dsig:Transforms><dsig:Transform Algorithm="urn:schemas-microsoft-com:HashTransforms.Identity"></dsig:Transform></dsig:Transforms><dsig:DigestMethod Algorithm="http://www.w3.org/2000/09/xmldsig#sha1"></dsig:DigestMethod><dsig:DigestValue>ojDmTgpYMFRKJYkPcM6ckpYkWUU=</dsig:DigestValue></asmv2:hash></file> <file name="msvcm90.dll" hashalg="SHA1" hash="5470081b336abd7b82c6387567a661a729483b04"><asmv2:hash xmlns:asmv2="urn:schemas-microsoft-com:asm.v2" xmlns:dsig="http://www.w3.org/2000/09/xmldsig#"><dsig:Transforms><dsig:Transform Algorithm="urn:schemas-microsoft-com:HashTransforms.Identity"></dsig:Transform></dsig:Transforms><dsig:DigestMethod Algorithm="http://www.w3.org/2000/09/xmldsig#sha1"></dsig:DigestMethod><dsig:DigestValue>tVogb8kezDre2mXShlIqpp8ErIg=</dsig:DigestValue></asmv2:hash></file>
</assembly>
EOF

# Package the protoc compiler
cp `which protoc.exe` $DEST/generator-bin/
cp `which MSVCR100.DLL` $DEST/generator-bin/
cp `which MSVCP100.DLL` $DEST/generator-bin/

# Convert line breaks for convenience
find $DEST -name '*.c' -o -name '*.h' -o -name '*.txt' \
    -o -name '*.proto' -o -name '*.py' -o -name '*.options' \
    -exec unix2dos '{}' \;

# Zip it all up
( cd dist; zip -r $VERSION.zip $VERSION )
