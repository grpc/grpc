gen_build_json_dirs="test/core/end2end test/core/bad_client"
gen_build_files=""
for gen_build_json in $gen_build_json_dirs
do
  output_file=`mktemp /tmp/genXXXXXX`
  $gen_build_json/gen_build_json.py > $output_file
  gen_build_files="$gen_build_files $output_file"
done
