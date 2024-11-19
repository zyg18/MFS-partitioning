#!/bin/bash

data_files=(
    large_netlist_10000000.txt
    large_netlist_20000000.txt
    large_netlist_30000000.txt
    large_netlist_40000000.txt
    large_netlist_50000000.txt
    large_netlist_60000000.txt
)

for data in "${data_files[@]}"; do
    echo "Processing $data"
    ./our_method_without_fixed_nodes "$data"
done

echo "All done!"