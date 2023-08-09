#!/bin/bash
set -uo pipefail

source "$(dirname "$BASH_SOURCE")/common.sh"

BUILD_DIR=${1-${PWD}}

export NANO_NODE_EXE=${BUILD_DIR}/nano_node$(get_exec_extension)
export NANO_RPC_EXE=${BUILD_DIR}/nano_rpc$(get_exec_extension)

overall_status=0

for script in $(dirname "$0")/../../systests/*.sh; do
    name=$(basename ${script})

    echo "::group::Running: $name"

    ./$script
    status=$?

    echo "::endgroup::"

    if [ $status -eq 0 ]; then
        echo "::notice file=$name::Passed"
    else
        echo "::error file=$name::Failed ($?)"
        overall_status=1
    fi
done

if [ $overall_status -eq 0 ]; then
    echo "::notice::All systests passed."
else
    echo "::error::Some systests failed."
    exit 1
fi
