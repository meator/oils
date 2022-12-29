# Benchmarking stub that's run with various shells, including dash

set -e

main() {
  local sh_run_path=$1
  local files_out_dir=$2
  local conf_dir=$3

  cd $conf_dir

  touch __TIMESTAMP

  "$sh_run_path" ./configure

  # This extra step is added to the resource usage of ./configure, but it's OK
  # for now

  echo "COPYING to $files_out_dir"
  find . -type f -newer __TIMESTAMP \
    | xargs -I {} -- cp --verbose {} $files_out_dir
}

main "$@"
