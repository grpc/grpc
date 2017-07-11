#!/bin/bash

# Run this script in the top nanopb directory to create a binary package
# for Mac OS X users.

# Requires: protobuf, python-protobuf, pyinstaller

set -e
set -x

VERSION=`git describe --always`-macosx-x86
DEST=dist/$VERSION

rm -rf $DEST
mkdir -p $DEST

# Export the files from newest commit
git archive HEAD | tar x -C $DEST

# Rebuild the Python .proto files
make -BC $DEST/generator/proto

# Package the Python libraries
( cd $DEST/generator; pyinstaller nanopb_generator.py )
mv $DEST/generator/dist/nanopb_generator $DEST/generator-bin

# Remove temp files
rm -rf $DEST/generator/dist $DEST/generator/build $DEST/generator/nanopb_generator.spec

# Make the nanopb generator available as a protoc plugin
cp $DEST/generator-bin/nanopb_generator $DEST/generator-bin/protoc-gen-nanopb

# Package the protoc compiler
cp `which protoc` $DEST/generator-bin/protoc.bin
LIBPROTOC=$(otool -L `which protoc` | grep -o '/.*libprotoc[^ ]*')
LIBPROTOBUF=$(otool -L `which protoc` | grep -o '/.*libprotobuf[^ ]*')
cp $LIBPROTOC $LIBPROTOBUF $DEST/generator-bin/
cat > $DEST/generator-bin/protoc << EOF
#!/bin/bash
SCRIPTDIR=\$(dirname "\$0")
export DYLD_LIBRARY_PATH=\$SCRIPTDIR
export PATH=\$SCRIPTDIR:\$PATH
exec "\$SCRIPTDIR/protoc.bin" "\$@"
EOF
chmod +x $DEST/generator-bin/protoc

# Tar it all up
( cd dist; tar -czf $VERSION.tar.gz $VERSION )

