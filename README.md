# README

## Compilation

Compile the methods with the following commands:

- **Without Fixed Nodes**:  
  ```bash
  g++ -o our_method_without_fixed_nodes our_method_without_fixed_nodes.cpp -std=c++17
  ```

- **With Fixed Nodes**:  
  ```bash
  g++ -o our_method_with_fixed_nodes our_method_with_fixed_nodes.cpp -std=c++17
  ```

- **Reference Method (DATE2024)**:  
  ```bash
  g++ -o date2024 date2024.cpp -std=c++17
  ```

## Running Experiments

1. **Titan23 Benchmark on MFS1**:  
   ```bash
   ./run_all1.sh
   ```

2. **Generated Datasets (case1–case8) on MFS2**:  
   ```bash
   ./run_all2.sh
   ```

3. **Large Netlists on MFS1**:  
   ```bash
   ./demo.sh
   ``` 
