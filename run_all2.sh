#!/bin/bash

data_files=(
    case2
    case3
    case4
    case5
    case6
)

for data in "${data_files[@]}"; do
    echo "Processing $data"
    ./our_method_without_fixed_nodes "$data"
done

echo "All done!"