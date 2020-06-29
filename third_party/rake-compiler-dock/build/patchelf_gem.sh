#/bin/bash
set -ex

if [ -z $1 ] ; then
  echo "Gem file needed!" && exit 1;
fi

GEM=$1
GEM_FILENAME=$(basename -- "$GEM")
GEM_NAME="${GEM_FILENAME%.gem}"

# Extract all files onto a temporary directory
TMPDIR=$(mktemp -d -t gem-XXXXXXXXXX)
gem unpack $GEM --target=$TMPDIR
gem spec $GEM --ruby > ${TMPDIR}/${GEM_NAME}/${GEM_NAME}.gemspec

# Run patchelf to all so files to strip out unnecessary libcrypt.so.2 dependency
find $TMPDIR/${GEM_NAME} -name "*.so" \
    -printf '%p\n' \
    -exec patchelf --remove-needed libcrypt.so.2 {} \;

# Rebuild the gem again with modified so files
pushd $TMPDIR/${GEM_NAME}
gem build ${GEM_NAME}.gemspec
popd

# Keep the new result
mv $TMPDIR/${GEM_NAME}/${GEM_NAME}.gem $GEM

rm -rf $TMPDIR
