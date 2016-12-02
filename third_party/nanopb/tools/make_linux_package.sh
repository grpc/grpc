#!/bin/bash

# Run this script in the top nanopb directory to create a binary package
# for Linux users.

set -e
set -x

VERSION=`git describe --always`-linux-x86
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

# Package the protoc compiler
cp `which protoc` $DEST/generator-bin/protoc.bin
LIBPROTOC=$(ldd `which protoc` | grep -o '/.*libprotoc[^ ]*')
LIBPROTOBUF=$(ldd `which protoc` | grep -o '/.*libprotobuf[^ ]*')
cp $LIBPROTOC $LIBPROTOBUF $DEST/generator-bin/
cat > $DEST/generator-bin/protoc << EOF
#!/bin/bash
SCRIPTDIR=\$(dirname "\$0")
export LD_LIBRARY_PATH=\$SCRIPTDIR
export PATH=\$SCRIPTDIR:\$PATH
exec "\$SCRIPTDIR/protoc.bin" "\$@"
EOF
chmod +x $DEST/generator-bin/protoc

# Tar it all up
( cd dist; tar -czf $VERSION.tar.gz $VERSION )

