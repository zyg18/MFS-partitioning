import random
import numpy as np

# Function to generate a netlist
def generate_netlist(n, alpha, output_path):
    netlist = []
    total_nets = 0
    k_min = 2
    k_max = min(n, 1000)
    parent = list(range(n))

    # Helper function for union-find to find the root of a node
    def find(x):
        if parent[x] != x:
            parent[x] = find(parent[x])
        return parent[x]

    # Helper function for union-find to unite two components
    def union(x, y):
        root_x = find(x)
        root_y = find(y)
        if root_x != root_y:
            parent[root_y] = root_x

    # Generate the number of nets for each connection size k
    for k in range(k_min, k_max + 1):
        num_nets = int(0.3* n * (k ** -alpha))
        total_nets += num_nets
        for _ in range(num_nets):
            net = random.sample(range(n), k)
            netlist.append(net)
            # Union all nodes in the net to ensure connectivity
            for i in range(1, k):
                union(net[0], net[i])
    
    print(f"Initial total nets for n={n}: {total_nets}")
    
    # Ensure the entire graph is connected by adding 2-pin nets
    for i in range(1, n):
        if find(i) != find(0):
            node_a = random.randint(0, n - 1)
            while find(node_a) == find(i):
                node_a = random.randint(0, n - 1)
            netlist.append([node_a, i])
            total_nets += 1
            union(node_a, i)
    
    print(f"Final total nets for n={n}: {total_nets}")
    
    # Write netlist to file in hypergraph format
    with open(output_path, 'w') as file:
        file.write(f"{n}\n")  # Number of points
        file.write(f"{total_nets}\n")  # Number of nets
        for net in netlist:
            file.write(" ".join(map(str, net)) + "\n")
            
        for _ in range(1, 9):
            file.write(f"{random.randint(0, n - 1)}")

# Main function to generate netlists
def main():
    alpha = 1.79  # Power-law exponent
    n_values = [int(1e7), int(2e7), int(3e7), int(4e7), int(5e7), int(6e7)]
    
    for n in n_values:
        output_path = f"./ICCAD2021-TopoPart-Benchmarks/generated_large_netlists/large_netlist_{n}.txt"
        print(f"Generating netlist for n={n}...")
        generate_netlist(n, alpha, output_path)
        print(f"Netlist for n={n} generated and saved to '{output_path}'\n")

if __name__ == "__main__":
    main()
