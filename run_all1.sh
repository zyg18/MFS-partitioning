#!/bin/bash

data_files=(
    sparcT1_core
    neuron
    stereo_vision
    des90
    SLAM_spheric
    cholesky_mc
    segmentation
    bitonic_mesh
    dart
    openCV
    stap_qrd
    minres
    cholesky_bdti
    denoise
    sparcT2_core
    gsm_switch
    mes_noc
    LU_Network
    LU230
    sparcT1_chip2
    directrf
    bitcoin_miner
)

for data in "${data_files[@]}"; do
    echo "Processing $data"
    ./our_method_without_fixed_nodes "$data"
done

echo "All done!"