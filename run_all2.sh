#!/bin/bash

data_files=(
    case1
    case2
    case3
    case4
    case5
    case6
    case7
    case8
)

for data in "${data_files[@]}"; do
    echo "Processing $data"
    ./MFSPart "$data"
done

echo "All done!"
