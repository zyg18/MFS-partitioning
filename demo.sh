#!/bin/bash

data_files=(
    large_netlist_1.txt
    large_netlist_2.txt
    large_netlist_3.txt
    large_netlist_4.txt
)

for data in "${data_files[@]}"; do
    echo "Processing $data"
    ./MFSPart "$data"
done

echo "All done!"
