#!/bin/sh

# To properly use this, you'll need to add:
#
#    "debug": true
#
# to build.json

cd `dirname $0`/../..

./tools/buildgen/generate_projects.sh

git diff |
grep \\+Project |
cut -d\" -f 4 |
sort -u |
grep _test$ |
while read p ; do
  mkdir -p templates/vsprojects/$p
  echo '<%namespace file="../vcxproj_defs.include" import="gen_project"/>${gen_project("'$p'", targets)}' > templates/vsprojects/$p/$p.vcxproj.template
done

git diff |
grep \\+Project |
cut -d\" -f 4 |
sort -u |
grep -v _test$ |
while read p ; do
  mkdir -p templates/vsprojects/$p
  echo '<%namespace file="../vcxproj_defs.include" import="gen_project"/>${gen_project("'$p'", libs)}' > templates/vsprojects/$p/$p.vcxproj.template
done

./tools/buildgen/generate_projects.sh
