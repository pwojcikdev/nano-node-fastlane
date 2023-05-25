./core_test
core_test_res=${?}

./rpc_test
rpc_test_res=${?}

echo "Core Test return code: ${core_test_res}"
echo "RPC  Test return code: ${rpc_test_res}"

if [[ ${core_test_res} != 0 || ${rpc_test_res} != 0 ]]; then
    return 1
else
    return 0
fi