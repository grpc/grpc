#!/bin/bash

set -ex

# check_env checks that all required environment variables are set
check_env () {
  env_vars=("$@")
  missing_var=0
  for var in "${env_vars[@]}"; do
    if [ -z "${!var}" ]; then
      echo "${var} not set"
      missing_var=1
    fi
  done
  return $missing_var
}

# substitute_env_in_files injects environment variables to files passed as
# arguments
substitute_env_in_files () {
  files=("$@")
  for file in "${files[@]}"; do
    tmp=$(mktemp)
    echo "Making substitutions in ${file}"
    cp --attributes-only --preserve ${file} ${tmp}
    cat ${file} | envsubst > $tmp && mv $tmp $file
  done
}

# Check required environment variables
check_env GCP_PROJECT_ID GCP_GRAFANA_SERVICE GCP_DATA_TRANSFER_SERVICE BQ_PROJECT_ID PG_USER PG_PASS PG_DATABASE GRAFANA_ADMIN_PASS CLOUD_SQL_INSTANCE || exit 1

# Configure postgres replicator
git clone --depth 1 https://github.com/grpc/test-infra
mkdir postgres_replicator
mv test-infra postgres_replicator
cp postgres_replicator_config/* postgres_replicator

# Configure grafana
mkdir grafana
cp -r grafana_config/* grafana

# Substitute environment variables
substitute_env_in_files \
  "./grafana/app.yaml" \
  "./postgres_replicator/app.yaml" \
  "./postgres_replicator/replicator_config.yaml"
