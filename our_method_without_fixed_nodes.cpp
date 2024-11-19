//for no fixed nodes setting
#include <iostream>
#include <fstream>
#include <vector>
#include <tuple>
#include <algorithm>
#include <sstream>
#include <queue>
#include <set>
#include <random>
#include <cmath>
#include <unordered_set>
#include <unordered_map>
#include <Eigen/Sparse>
#include <Eigen/Dense>
#include <boost/heap/fibonacci_heap.hpp>
#include <boost/functional/hash.hpp>
#include <stack>
#include <ctime>

using Eigen::SparseMatrix;
using Eigen::Triplet;

// Equivalent of Python's csr_matrix
typedef SparseMatrix<int, Eigen::RowMajor> CSRMatrix;

CSRMatrix create_FPGA_matrix(const std::string& file_path) {
    std::ifstream f(file_path);
    int n, m;
    f >> n >> m;
  
    std::vector<Triplet<int>> triplets;
  
    for (int i = 0; i < m; ++i) {
        int u, v;
        f >> u >> v;
        if (u != v) {
          triplets.push_back(Triplet<int>(u, v, 1));
          triplets.push_back(Triplet<int>(v, u, 1)); // Add for transpose
        }
    }
  
    CSRMatrix A(n, n);
    A.setFromTriplets(triplets.begin(), triplets.end());
  
    return A;
}

std::tuple<CSRMatrix, std::vector<std::tuple<int, int>>> create_netlist_matrix(const std::string& file_path) {
    std::ifstream f(file_path);
    int n, m, u, v;
    std::string line;
    std::getline(f, line);
    n = std::stoi(line);
    std::getline(f, line);
    m = std::stoi(line);
  
    std::vector<Triplet<int>> triplets;
    for (int i = 0; i < m; ++i) {
        std::getline(f, line);
        std::stringstream ss(line);
        
        ss >> u;
        while (ss >> v) {
            if (u != v) {
                triplets.push_back(Triplet<int>(u, v, 1));
                triplets.push_back(Triplet<int>(v, u, 1)); // Add for symmetry
            }
        }
    }
  
    CSRMatrix A(n, n);
    A.setFromTriplets(triplets.begin(), triplets.end());
  
    std::vector<std::tuple<int, int>> fixed_nodes;
    int fpga_id = 0;
    while (std::getline(f, line)) {
        std::stringstream ss(line);
        int node_id;
        while (ss >> node_id) {
            fixed_nodes.push_back(std::make_tuple(node_id, fpga_id));
        }
        fpga_id++;
    }
  
    return std::make_tuple(A, fixed_nodes);
}

std::vector<std::tuple<int, int>> find_reachable_nodes(const CSRMatrix& A, 
                                                      int start_node, 
                                                      int max_distance, 
                                                      const std::unordered_set<int>& ignored_nodes_set) {
    std::vector<std::tuple<int, int>> reachable_nodes;
    std::unordered_set<int> visited;
    std::queue<std::pair<int, int>> queue;
    queue.push({start_node, 0});

    while (!queue.empty()) {
        auto [node_id, distance] = queue.front();
        queue.pop();

        if (visited.count(node_id)) {
            continue;
        }

        visited.insert(node_id);
        reachable_nodes.push_back({node_id, distance});

        if (distance < max_distance) {
            for (CSRMatrix::InnerIterator it(A, node_id); it; ++it) {
                int neighbor = it.index();
                if (visited.count(neighbor) == 0 && ignored_nodes_set.count(neighbor) == 0) {
                    queue.push({neighbor, distance + 1});
                }
            }
        }
    }

    return reachable_nodes;
}


std::tuple<CSRMatrix, std::vector<int>, std::vector<std::vector<int>>, std::vector<int>> coarsen_graph(
    CSRMatrix& graph,  
    std::vector<int>& node_weight,
    int capacity) 
{   
    int n = graph.rows();

    std::vector<int> mapping(n,0); 
    std::vector<std::vector<int>> remapping;
    std::vector<int> new_node_weight;
    std::unordered_set<int> merged_nodes;
    std::vector<int> degree(n, 0);
    
    for (int node_id = 0; node_id < n; ++node_id) {
        for (CSRMatrix::InnerIterator it(graph, node_id); it; ++it) {
            degree[node_id] += it.value();
        }
    }
    
    int new_node_id = 0;
    std::vector<std::tuple<double, int, int>> neighbor_priority;
    
    int num1 = 0, num2 = 0;
    
    for (int node_id = 0; node_id < n; ++node_id) {
    
        if (merged_nodes.find(node_id) != merged_nodes.end())
            continue;
        
        for (CSRMatrix::InnerIterator it(graph, node_id); it; ++it) {
            int node_id2 = it.index();
            if (merged_nodes.find(node_id2) != merged_nodes.end() || node_id2 <= node_id || node_weight[node_id] + node_weight[node_id2] > capacity){
                continue;
            }

            double A = (double)it.value() / (node_weight[node_id] * node_weight[node_id2]);
            double B = (double)it.value() * it.value() / (degree[node_id] * degree[node_id2]);
            
            if (A>B)
                num1++;
            else
                num2++;
            double priority = A;

            neighbor_priority.push_back({priority, node_id, node_id2});
        }

    }
    
    std::sort(neighbor_priority.begin(), neighbor_priority.end(),
          [](const std::tuple<double, int, int> &a, const std::tuple<double, int, int> &b) {
              return std::get<0>(a) > std::get<0>(b);
          });
    

    for (const auto& [_, node_id, node_id2] : neighbor_priority) {
    
        if (merged_nodes.find(node_id) != merged_nodes.end() || merged_nodes.find(node_id2) != merged_nodes.end()) {
            continue;
        }
        
        // merge node_id and neighbor_id
        merged_nodes.insert(node_id);
        merged_nodes.insert(node_id2);
        mapping[node_id] = new_node_id;
        mapping[node_id2] = new_node_id;
        remapping.push_back({node_id, node_id2});
        new_node_weight.push_back(node_weight[node_id] + node_weight[node_id2]);
        new_node_id++;
    }
    
    for (int node_id = 0; node_id < n; ++node_id) {
        if (merged_nodes.find(node_id) == merged_nodes.end()){
            merged_nodes.insert(node_id);
            mapping[node_id] = new_node_id;
            remapping.push_back({node_id});
            new_node_weight.push_back(node_weight[node_id]);
            new_node_id++;
        }
    }
    
    int k = new_node_id;

    std::vector<Triplet<int>> triplets;
    for (int i = 0; i < n; ++i) {
        triplets.emplace_back(i, mapping[i], 1);
    }
    CSRMatrix X(n, k);
    X.setFromTriplets(triplets.begin(), triplets.end());
    
    CSRMatrix Xt = X.transpose();
    
    CSRMatrix new_graph = Xt * graph * X;
    
    
    std::vector<Triplet<int>> tripletList;

    // Step 2: Iterate through all non-zero elements of new_graph
    for (int k = 0; k < new_graph.outerSize(); ++k) {
        for (CSRMatrix::InnerIterator it(new_graph, k); it; ++it) {
            if (it.row() != it.col()) {  // Only keep non-diagonal elements
                tripletList.emplace_back(it.row(), it.col(), it.value());
            }
        }
    }
    new_graph.setZero();
    new_graph.setFromTriplets(tripletList.begin(), tripletList.end());
    
    for(int i=0;i<new_node_weight.size();i++)
        if(new_node_weight[i]>capacity)
            std::cout<<"node is too larger in coarsen period."<<std::endl;
    
    return {new_graph, mapping, remapping, new_node_weight};
}

void DFS(int node, const Eigen::SparseMatrix<int, Eigen::RowMajor> &mat, 
         std::vector<int> &component, int label, int &currentComponentSize) {
    std::stack<int> stack;
    stack.push(node);

    while (!stack.empty()) {
        int current = stack.top();
        stack.pop();

        if (component[current] != -1) 
            continue;

        component[current] = label;

        ++currentComponentSize;

        for (Eigen::SparseMatrix<int, Eigen::RowMajor>::InnerIterator it(mat, current); it; ++it) {
            int neighbor = it.index();
            if (component[neighbor] == -1) {
                stack.push(neighbor);
            }
        }
    }
}

std::unordered_set<int> findLargestConnectedComponent(const Eigen::SparseMatrix<int, Eigen::RowMajor> &mat) {
    int n = mat.rows();
    std::vector<int> component(n, -1);
    int label = 0; 
    int maxSize = 0; 
    int maxLabel = -1;

    for (int i = 0; i < n; ++i) {
        if (component[i] == -1) {
            int currentComponentSize = 0;
            DFS(i, mat, component, label, currentComponentSize);

            if (currentComponentSize > maxSize) {
                maxSize = currentComponentSize;
                maxLabel = label;
            }

            ++label;
        }
    }
    
    std::unordered_set<int> largestComponent;
    for (int i = 0; i < n; ++i) {
        if (component[i] == maxLabel) {
            largestComponent.insert(i);
        }
    }
    
    return largestComponent;
}


std::vector<double> dijkstra_shortest_paths(const Eigen::SparseMatrix<int, Eigen::RowMajor>& graph, int start_node, std::vector<int>node_weight) {
    int n = graph.rows();
    std::vector<double> distances(n, std::numeric_limits<double>::infinity());
    distances[start_node] = 0;

    using P = std::pair<double, int>;  // pair (distance, node)
    std::priority_queue<P, std::vector<P>, std::greater<P>> pq;
    pq.push({0.0, start_node});

    while (!pq.empty()) {
        double dist = pq.top().first;
        int node = pq.top().second;
        pq.pop();

        if (dist > distances[node])
            continue;

        for (Eigen::SparseMatrix<int, Eigen::RowMajor>::InnerIterator it(graph, node); it; ++it) {
            int neighbor = it.index();
            double newDist = dist + 1.0 * node_weight[node] * node_weight[neighbor]/ it.value();
            if (newDist < distances[neighbor]) {
                distances[neighbor] = newDist;
                pq.push({newDist, neighbor});
            }
        }
    }

    return distances;
}


double communication_cost(const std::vector<int>& mapping, const Eigen::MatrixXd& fixed_distances_matrix, const CSRMatrix& fpga_matrix) {
    double total_cost = 0.0;

    for (int row = 0; row < fpga_matrix.outerSize(); ++row) {
        for (CSRMatrix::InnerIterator it(fpga_matrix, row); it; ++it) {

            int u_fpga = it.row(); 
            int v_fpga = it.col();
            
            if (u_fpga >= v_fpga)
                continue;
            
            int u_fixed = mapping[u_fpga];
            int v_fixed = mapping[v_fpga];

            total_cost += fixed_distances_matrix(u_fixed, v_fixed);
        }
    }

    return total_cost;
}


std::pair<std::vector<int>, double> simulated_annealing(int k, const Eigen::MatrixXd& fixed_distances_matrix, 
                                                        const CSRMatrix& fpga_matrix, 
                                                        int max_steps = 100, double initial_temp = 100.0, double cool_rate = 0.99) {
    
    std::random_device rd;
    std::mt19937 gen(rd());

    std::vector<int> current_mapping(k);
    for(int i = 0; i < k; ++i) {
        current_mapping[i] = i;
    }
    std::shuffle(current_mapping.begin(), current_mapping.end(), gen);


    std::vector<int> best_mapping = current_mapping;


    double current_cost = communication_cost(current_mapping, fixed_distances_matrix, fpga_matrix);
    double best_cost = current_cost;

    double temperature = initial_temp; 
    std::uniform_real_distribution<> dist_real(0.0, 1.0);

    for (int step = 0; step < max_steps; ++step) {

        std::uniform_int_distribution<> dist_int(0, k - 1);
        int i = dist_int(gen);
        int j = i;
        while (j == i) {
            j = dist_int(gen);
        }

        std::vector<int> new_mapping = current_mapping;
        std::swap(new_mapping[i], new_mapping[j]);

        double new_cost = communication_cost(new_mapping, fixed_distances_matrix, fpga_matrix);

        if (new_cost < current_cost || dist_real(gen) < std::exp((current_cost - new_cost) / temperature)) {
            current_mapping = new_mapping;
            current_cost = new_cost;

            if (current_cost < best_cost) {
                best_mapping = current_mapping;
                best_cost = current_cost;
            }
        }


        temperature *= cool_rate;


        if (temperature < 1e-5)
            break;
    }

    return {best_mapping, best_cost};
}

struct CompareFirst {
    bool operator()(const std::tuple<double, int>& a, const std::tuple<double, int>& b) const {
        return std::get<0>(a) < std::get<0>(b);
    }
};

struct CompareFirstInThree {
    bool operator()(const std::tuple<double, int, int>& a, const std::tuple<double, int, int>& b) const {
        return std::get<0>(a) < std::get<0>(b);
    }
};


int random_choice(std::vector<std::pair<int, double>>&gain, std::unordered_set<int>&exclude_fpga, double total_edge_weight){

    if (exclude_fpga.size() == gain.size() - 1){
        for (const auto& pair : gain){
            if (exclude_fpga.count(pair.first)==0)
                return pair.first;
        }
    }
    else if (exclude_fpga.size() >= gain.size()){
        std::cout<<"wrong in random choice"<<std::endl;
        std::exit(0);
        return -1;
    }
    
    double sum_exp_gain = 0.0;
    double max_gain = -1e20;
    for (const auto& pair : gain){
        if (exclude_fpga.count(pair.first)==1)
            continue;
        max_gain = (max_gain>pair.second)?max_gain:pair.second;
    }
    
    for (const auto& pair : gain){
        if (exclude_fpga.count(pair.first)==1)
            continue;
        double gain_pre_cal = (pair.second-max_gain)/(total_edge_weight);
        double exp_gain = std::exp(gain_pre_cal);
        sum_exp_gain += exp_gain;
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<double> dis(0.0, 1.0);
    double random_value = dis(gen);
    

    int fpga = 0;
    double accumulated_exp_gain = 0;
    
    for (const auto& pair : gain){
        if (exclude_fpga.count(pair.first)==1)
            continue;
        double gain_pre_cal = (pair.second-max_gain)/(total_edge_weight);
        accumulated_exp_gain += std::exp(gain_pre_cal)/sum_exp_gain;
        if (random_value <= accumulated_exp_gain){
            fpga = pair.first;
            break;
        }
    }

    return fpga;
}

std::vector<std::unordered_set<int>> initial_partitioning(
    const CSRMatrix& current_graph, 
    std::vector<std::unordered_set<int>> CP, 
    const std::vector<std::vector<int>> &fpga_distances, 
    const std::vector<int> &node_weight,
    const int capacity) 
{  
    int num_fpgas = fpga_distances.size();
    int n = current_graph.rows();
    int m = current_graph.nonZeros() / 2;
    
    std::unordered_set<int> fixed_nodes_set;
    std::unordered_set<int> violation_set;
    
    std::vector<int> nodes_fixed_in_fpga(num_fpgas, 0);
    
    for (int node_id = 0; node_id < n; ++node_id) {
        if (CP[node_id].size() == 1){
            int fpga_id_index = *CP[node_id].begin();
            nodes_fixed_in_fpga[fpga_id_index] += node_weight[node_id];
            fixed_nodes_set.insert(node_id);
        }
        else if(CP[node_id].size() == 0){       
            violation_set.insert(node_id);
        }
    }
    
    int cut_size2 = 0;
    int violation2 = 0;
    for(int i=0;i<current_graph.rows();i++){
        if (fixed_nodes_set.count(i)==0)
            continue;
        int fpga_id = *CP[i].begin();
        for (CSRMatrix::InnerIterator it(current_graph, i); it; ++it) {
            int neighbor_id = it.index();
            if (fixed_nodes_set.count(neighbor_id)==0 || neighbor_id < i)
                continue;
            int neighbor_fpga_id = *CP[neighbor_id].begin();
            if (fpga_distances[fpga_id][neighbor_fpga_id] >= 1) {
                cut_size2 += it.value();
            }
            if(fpga_distances[fpga_id][neighbor_fpga_id] > 1) {
                violation2 += it.value();
            }
        }
    }
    
    std::cout<<"cut_size:"<<cut_size2<<" violation:"<<violation2<<std::endl;
    
    
    std::cout << "fixed_nodes_set size is:"<< fixed_nodes_set.size() << "violation_set size is:"<< violation_set.size() << std::endl; 
    std::cout << std::endl;
    
    boost::heap::fibonacci_heap<std::tuple<double, int>, boost::heap::compare<CompareFirst>> fpga_heaps;
    std::unordered_map<int, boost::heap::fibonacci_heap<std::tuple<double, int>,boost::heap::compare<CompareFirst>>::handle_type> handleMap;
    
    std::vector<std::vector<int>> edge_weight_in_fpga_per_node(n, std::vector<int>(num_fpgas, 0));
    std::vector<int>edge_distributed(n,0);
    std::vector<int>all_edge(n,0);
    
    // Populate priority queue with unfixed nodes
    for (int node_id = 0; node_id < n; ++node_id) {
        if (CP[node_id].size() <= 1) continue;
        
        // Count neighbors already assigned to each FPGA
        for (CSRMatrix::InnerIterator it(current_graph, node_id); it; ++it) {
            int neighbor_id = it.index();

            if (CP[neighbor_id].size() == 1) {
                int neighbor_fpga_id = *(CP[neighbor_id].begin());
                edge_weight_in_fpga_per_node[node_id][neighbor_fpga_id] += it.value();
                edge_distributed[node_id] += it.value();
                all_edge[node_id] += it.value();
            }
            else
                all_edge[node_id] += it.value();
        }
        
        double gain =  (double)edge_distributed[node_id]/(all_edge[node_id]+1e-6);
        auto handle1 = fpga_heaps.push(std::make_tuple(gain, node_id));
        handleMap[node_id] = handle1;
        
    }
    
    bool remain = true;
    int beta = 1000;
    
    
    
    
    while(!fpga_heaps.empty()){

        auto [priority, node_id] = fpga_heaps.top();
            
        fpga_heaps.pop();
        auto itmap = handleMap.find(node_id);
        handleMap.erase(itmap);
        std::vector<std::pair<int, double>> gain;
        double total_edge_weight = 1;

        for (int fpga_id : CP[node_id]){
            if(node_weight[node_id] + nodes_fixed_in_fpga[fpga_id] > capacity)
                continue;

            double current_gain = edge_weight_in_fpga_per_node[node_id][fpga_id] - 1e4/(capacity+0.01-node_weight[node_id]-nodes_fixed_in_fpga[fpga_id]);
            
            total_edge_weight += edge_weight_in_fpga_per_node[node_id][fpga_id];
            
            for (CSRMatrix::InnerIterator it(current_graph, node_id); it; ++it) {
                int neighbor_id = it.index();
        
                if (CP[neighbor_id].size() == 1) {
                    int neighbor_fpga_id = *(CP[neighbor_id].begin());
                    int dist = fpga_distances[fpga_id][neighbor_fpga_id];
                    if(dist>1){
                        std::cout<<"I think it is wrong"<<std::endl;
                        std::exit(0);
                        current_gain -= beta * it.value() * (1+(dist-1)/3);
                    }
                }
            }
            gain.push_back({fpga_id, current_gain}); 
        }

        
        if(gain.empty()){
            CP[node_id].clear();
            violation_set.insert(node_id);
            if (fixed_nodes_set.count(node_id)==1)
                fixed_nodes_set.erase(node_id);
            continue;
        }    


        bool violation_in_all_fpga = true;
        int best_fpga;
        std::vector<std::tuple<int, int>> reachable_nodes;
        
        std::unordered_set<int>exclude_fpga;
        int fpga_size = gain.size();
        
        for (int i=0;i<fpga_size;i++){
            best_fpga = random_choice(gain, exclude_fpga, 0.4*total_edge_weight);
            //std::cout<<" Put node "<<node_id<<" whose weight is "<<node_weight[node_id]<<" in fpga: "<<best_fpga<<", its CP size is:"<<CP[node_id].size()<<std::endl;
            bool violation_in_this_fpga = false;
            int d = 0;
            for (const auto& distance : fpga_distances[best_fpga]) {
                d = std::max(d, distance);
            }
            reachable_nodes = find_reachable_nodes(current_graph, node_id, d, fixed_nodes_set);
    
            // Check violation
            for (const auto& [node_id2, distance] : reachable_nodes) {
                if (CP[node_id2].size() > 1) {
                    bool all_violate = std::all_of(CP[node_id2].begin(), CP[node_id2].end(),
                        [&](int c_fpga) { return fpga_distances[best_fpga][c_fpga] > distance; });
                    if (all_violate) {
                        violation_in_this_fpga = true;
                        break;
                    }
                }
            }
            if(violation_in_this_fpga){
                exclude_fpga.insert(best_fpga);
                total_edge_weight -= edge_weight_in_fpga_per_node[node_id][best_fpga];
                continue;
            }
            else{
                violation_in_all_fpga = false;
                break;
            }
        }

        if(violation_in_all_fpga){
            CP[node_id].clear();
            violation_set.insert(node_id);
            if (fixed_nodes_set.count(node_id)==1)
                fixed_nodes_set.erase(node_id);
            continue;
        }

        CP[node_id] = {best_fpga};
        fixed_nodes_set.insert(node_id);
        nodes_fixed_in_fpga[best_fpga] += node_weight[node_id];

        for (const auto& [neighbor_id, distance] : reachable_nodes) {
            if (CP[neighbor_id].size() > 1 && violation_set.count(neighbor_id) == 0 ) {
                std::unordered_set<int> CP_copy = CP[neighbor_id];
                for (int candidate_fpga : CP_copy) {
                    if (fpga_distances[best_fpga][candidate_fpga] > distance) {
                        CP[neighbor_id].erase(candidate_fpga);
                    }
                }
                if (CP[neighbor_id].size() == 1 && !fixed_nodes_set.count(neighbor_id)) {
                    fixed_nodes_set.insert(neighbor_id);
                    int fixed_fpga = *CP[neighbor_id].begin();
                    auto itmap = handleMap.find(neighbor_id);

                    if (itmap != handleMap.end()) {
                        fpga_heaps.erase(itmap->second);
                        handleMap.erase(itmap); 
                    }

                    // Adjust node priority and re-add to the FPGA heap
                    double new_gain = 1e20;
                    auto handle = fpga_heaps.push(std::make_tuple(new_gain, neighbor_id));
                    handleMap[neighbor_id] = handle;

                    continue;
                }       
            }
        }

        for (CSRMatrix::InnerIterator it(current_graph, node_id); it; ++it) {
            int neighbor_id = it.index();
            if (fixed_nodes_set.count(neighbor_id) == 1 || violation_set.count(neighbor_id) == 1)
                continue;
            if (CP[neighbor_id].size()>1){
                auto itmap = handleMap.find(neighbor_id);
                if (itmap != handleMap.end()) {
                    edge_distributed[neighbor_id] += it.value();
                    edge_weight_in_fpga_per_node[neighbor_id][best_fpga] += it.value();
                    double gain =  (double)edge_distributed[neighbor_id]/all_edge[neighbor_id];  //has excluded the fixed nodes.
                    
                    fpga_heaps.erase(itmap->second);
                    handleMap.erase(itmap);
                    
                    auto new_handle = fpga_heaps.push(std::make_tuple(gain, neighbor_id));
                    handleMap[neighbor_id] = new_handle;
                }
            }
        }

    }
    
    
    
    std::cout << "fixed_nodes_set size is:"<< fixed_nodes_set.size() << " violation_set size is:"<< violation_set.size() << std::endl; 
    
    
    
    
    int T1=0, T2=0;
    for (int node_id = 0; node_id < n; ++node_id) {
        if (CP[node_id].size() == 1){
            T1++;
        }
        else if(CP[node_id].size() == 0){       
            T2++;
        }
    }
    
    
    std::cout << "fixed_nodes_set size is:"<< T1 << " violation_set size is:"<< T2 << std::endl; 
    
    int T3=0, T4=0;
    for (int node_id = 0; node_id < n; ++node_id) {
        if (CP[node_id].size() == 1 && fixed_nodes_set.count(node_id)==0){
            T3++;
        }
        else if(CP[node_id].size() != 1 && fixed_nodes_set.count(node_id)==1){
            T4++;
        }
    }
    
    std::cout << "v1:"<< T3 << " v2:"<< T4 << std::endl;
    
    int T5=0, T6=0;
    for (int node_id = 0; node_id < n; ++node_id) {
        if (CP[node_id].size() == 0 && violation_set.count(node_id)==0){
            T5++;
        }
        else if(CP[node_id].size() != 0 && violation_set.count(node_id)==1){
            T6++;
        }
    }
    
    std::cout << "v3:"<< T5 << " v4:"<< T6 << std::endl; 
    
    std::vector<int> nodes_weight_fixed_in_this_fpga(num_fpgas, 0);
    std::vector<int> nodes_fixed_in_this_fpga(num_fpgas, 0);
    for (int node_id = 0; node_id < n; ++node_id) {
        if (CP[node_id].size() == 1){
            int fpga_id_index = *CP[node_id].begin();
            nodes_weight_fixed_in_this_fpga[fpga_id_index] += node_weight[node_id];
            nodes_fixed_in_this_fpga[fpga_id_index] += 1;
        }
    }
    
    std::cout << "nodes_weight_fixed_in_this_fpga: ";
    for (int i = 0; i < num_fpgas; ++i) {
        std::cout << nodes_weight_fixed_in_this_fpga[i] << " ";
    }
    std::cout << std::endl;
    
    
    std::cout << "nodes_fixed_in_this_fpga: ";
    for (int i = 0; i < num_fpgas; ++i) {
        std::cout << nodes_fixed_in_this_fpga[i] << " ";
    }
    std::cout << std::endl;
    
    
    int cut_size1 = 0;
    int violation1 = 0;
    for(int i=0;i<current_graph.rows();i++){
        if (violation_set.count(i))
            continue;
        int fpga_id = *CP[i].begin();
        for (CSRMatrix::InnerIterator it(current_graph, i); it; ++it) {
            int neighbor_id = it.index();
            if (violation_set.count(neighbor_id) || neighbor_id < i)
                continue;
            int neighbor_fpga_id = *CP[neighbor_id].begin();
            if (fpga_distances[fpga_id][neighbor_fpga_id] >= 1) {
                cut_size1 += it.value();
            }
            if(fpga_distances[fpga_id][neighbor_fpga_id] > 1) {
                violation1 += it.value();
            }
        }
    }
    std::cout<<"cut_size:"<<cut_size1<<" violation:"<<violation1<<std::endl;
    
    std::cout << std::endl;
    
    for (const int& node_id : violation_set){
        if (fixed_nodes_set.count(node_id)==1)
            std::cout<<"wrong"<<std::endl;
        //std:: cout<<"violation node:"<<node_id<<" node_weight"<<node_weight[node_id]<<std::endl;
    }
    
    boost::heap::fibonacci_heap<std::tuple<double, int, int>, boost::heap::compare<CompareFirstInThree>> violation_fpga_heaps;
    std::unordered_map<std::pair<int, int>, boost::heap::fibonacci_heap<std::tuple<double, int, int>, boost::heap::compare<CompareFirstInThree>>::handle_type, boost::hash<std::pair<int, int>>> ViohandleMap;

    for (const int& node_id : violation_set){
        std::vector<int>edge_weight_in_fpga(num_fpgas,0);
        for (CSRMatrix::InnerIterator it(current_graph, node_id); it; ++it) {
            int neighbor_id = it.index();
            if (CP[neighbor_id].size()==1){
                int neighbor_fpga_id = *(CP[neighbor_id].begin());
                edge_weight_in_fpga[neighbor_fpga_id] += it.value();
            }
        }
        for (int fpga_id=0;fpga_id<num_fpgas;fpga_id++){
            int priority = edge_weight_in_fpga[fpga_id];
            for (CSRMatrix::InnerIterator it(current_graph, node_id); it; ++it) {
                int neighbor_id = it.index();
                if (CP[neighbor_id].size()==1){
                    int neighbor_fpga_id = *(CP[neighbor_id].begin());
                    int dist = fpga_distances[fpga_id][neighbor_fpga_id];
                    if (dist > 1){
                        priority -= beta * it.value() *(1+(dist-1)/3);
                    }
                }
            }
            auto handle = violation_fpga_heaps.push(std::make_tuple(priority, node_id, fpga_id));
            ViohandleMap[{node_id, fpga_id}] = handle;
        }
    }

    while (!violation_fpga_heaps.empty()){
        auto [gain, node_id, fpga_id] = violation_fpga_heaps.top();
        auto itmap = ViohandleMap.find({node_id, fpga_id});
        ViohandleMap.erase(itmap); //need to delete it
        violation_fpga_heaps.pop();
        if (fixed_nodes_set.count(node_id) || nodes_fixed_in_fpga[fpga_id]+node_weight[node_id]> capacity)
            continue;

        CP[node_id] = {fpga_id};
        fixed_nodes_set.insert(node_id);
        violation_set.erase(node_id);
        nodes_fixed_in_fpga[fpga_id] += node_weight[node_id];
        for (CSRMatrix::InnerIterator it(current_graph, node_id); it; ++it) {
            int neighbor_id = it.index();
            if (violation_set.count(neighbor_id)){
                for (int neighbor_fpga_id=0;neighbor_fpga_id<num_fpgas;neighbor_fpga_id++){
                    int dist = fpga_distances[fpga_id][neighbor_fpga_id];
                    if (neighbor_fpga_id==fpga_id){
                        auto itmap = ViohandleMap.find({neighbor_id, neighbor_fpga_id});
                        if (itmap != ViohandleMap.end()) {
                            double old_gain = std::get<0>(*itmap->second);
                            double gain = old_gain + it.value();;
                
                            violation_fpga_heaps.erase(itmap->second);
                            ViohandleMap.erase(itmap);
                            
                            auto new_handle = violation_fpga_heaps.push(std::make_tuple(gain, neighbor_id, neighbor_fpga_id));
                            ViohandleMap[{neighbor_id, neighbor_fpga_id}] = new_handle;
                        }
                    }
                    else if(dist > 1){
                        
                        auto itmap = ViohandleMap.find({neighbor_id, neighbor_fpga_id});
                        if (itmap != ViohandleMap.end()) {
                            double old_gain = std::get<0>(*itmap->second);                             
                            double gain = old_gain - beta * it.value() *(1+(dist-1)/3);
                            
                            violation_fpga_heaps.erase(itmap->second);
                            ViohandleMap.erase(itmap);

                            auto new_handle = violation_fpga_heaps.push(std::make_tuple(gain, neighbor_id, neighbor_fpga_id));
                            ViohandleMap[{neighbor_id, neighbor_fpga_id}] = new_handle;
                        }
                    
                    }
                    
                }
            }
        }
        
    }

    
    std::cout << std::endl;
    
    
    
    std::cout << "fixed_nodes_set size is:"<< fixed_nodes_set.size() << " violation_set size is:"<< violation_set.size() << std::endl;
    
    T1=0, T2=0;
    for (int node_id = 0; node_id < n; ++node_id) {
        if (CP[node_id].size() == 1){
            T1++;
        }
        else if(CP[node_id].size() == 0){       
            T2++;
        }
    }
    
    std::cout << "fixed_nodes_set size is:"<< T1 << " violation_set size is:"<< T2 << std::endl; 
    
    T3=0, T4=0;
    for (int node_id = 0; node_id < n; ++node_id) {
        if (CP[node_id].size() == 1 && fixed_nodes_set.count(node_id)==0){
            T3++;
        }
        else if(CP[node_id].size() != 1 && fixed_nodes_set.count(node_id)==1){
            T4++;
        }
    }
    
    std::cout << "v1:"<< T3 << " v2:"<< T4 << std::endl;
    
    T5=0, T6=0;
    for (int node_id = 0; node_id < n; ++node_id) {
        if (CP[node_id].size() == 0 && violation_set.count(node_id)==0){
            T5++;
        }
        else if(CP[node_id].size() != 0 && violation_set.count(node_id)==1){
            T6++;
        }
    }
    
    std::cout << "v3:"<< T5 << " v4:"<< T6 << std::endl; 
    
    
    std::vector<int> nodes_weight_fixed_in_this_fpga2(num_fpgas, 0);
    std::vector<int> nodes_fixed_in_this_fpga2(num_fpgas, 0);
    for (int node_id = 0; node_id < n; ++node_id) {
        if (CP[node_id].size() == 1){
            int fpga_id_index = *CP[node_id].begin();
            nodes_weight_fixed_in_this_fpga2[fpga_id_index] += node_weight[node_id];
            nodes_fixed_in_this_fpga2[fpga_id_index] += 1;
        }
    }
    
    std::cout << "nodes_weight_fixed_in_this_fpga: ";
    for (int i = 0; i < num_fpgas; ++i) {
      std::cout << nodes_weight_fixed_in_this_fpga2[i] << " ";
    }
    std::cout << std::endl;
    
    
    std::cout << "nodes_fixed_in_this_fpga: ";
    for (int i = 0; i < num_fpgas; ++i) {
      std::cout << nodes_fixed_in_this_fpga2[i] << " ";
    }
    std::cout << std::endl;
    
    int cut_size3 = 0;
    int violation3 = 0;
    for(int i=0;i<current_graph.rows();i++){
        if (fixed_nodes_set.count(i)==0)
            continue;
        int fpga_id = *CP[i].begin();
        for (CSRMatrix::InnerIterator it(current_graph, i); it; ++it) {
            int neighbor_id = it.index();
            if (fixed_nodes_set.count(neighbor_id)==0 || neighbor_id < i)
                continue;
            int neighbor_fpga_id = *CP[neighbor_id].begin();

            if (fpga_distances[fpga_id][neighbor_fpga_id] >= 1) {
                cut_size3 += it.value();
            }
            if(fpga_distances[fpga_id][neighbor_fpga_id] > 1) {
                violation3 += it.value();
            }
        }
    }
    std::cout<<"cut_size:"<<cut_size3<<" violation:"<<violation3<<std::endl;
    
    return CP;
}


std::vector<std::unordered_set<int>> FM(
    const CSRMatrix &current_graph,
    std::vector<std::unordered_set<int>> CP,
    const std::vector<std::vector<int>> &fpga_distances, 
    const std::vector<int> &node_weight,
    double beta,
    const int capacity,
    bool speedup) 
{

    int n = current_graph.rows();
    int k = fpga_distances.size();
    int r = 2;
    std::vector<int>nodes_fixed_in_fpga(k,0);
    
    for (int node_id = 0; node_id < n; ++node_id) {
        if(CP[node_id].size()!=1){
            std::cout<<"wrong"<<std::endl;
            std::exit(0);
        }
        int fpga_id = *CP[node_id].begin();
        nodes_fixed_in_fpga[fpga_id] += node_weight[node_id];
    }
    
    boost::heap::fibonacci_heap<std::tuple<double, int, int>, boost::heap::compare<CompareFirstInThree>>FibHeap;
    std::unordered_map<std::pair<int, int>, boost::heap::fibonacci_heap<std::tuple<double, int, int>, boost::heap::compare<CompareFirstInThree>>::handle_type, boost::hash<std::pair<int, int>>> HandleMap;
    std::unordered_map<std::pair<int, int>, double, boost::hash<std::pair<int, int>>> gain_Map;
    
    for (int node_id = 0; node_id < n; ++node_id) {

        std::vector<double> priority(k, 0);
        if (CP[node_id].size() == 1) {
            int current_fpga_id = *CP[node_id].begin();
            for (CSRMatrix::InnerIterator it(current_graph, node_id); it; ++it) {
                int neighbor_id = it.index();
                int neighbor_fpga_id = *CP[neighbor_id].begin();
                priority[neighbor_fpga_id] += it.value(); //python wrong
                for (int fpga_id = 0; fpga_id < k; ++fpga_id) {
                    int dist = fpga_distances[neighbor_fpga_id][fpga_id];
                    if (dist > 1){
                        priority[fpga_id] -= beta * it.value() * (1+(dist-1)/3);
                    }
                }
            }
            for (int fpga_id = 0; fpga_id < k; ++fpga_id) {
                if (fpga_id == current_fpga_id) {
                    continue;
                }
                if(speedup == true && fpga_distances[current_fpga_id][fpga_id] > r)
                    continue;
                double gain = priority[fpga_id] - priority[current_fpga_id];
                auto handle = FibHeap.push(std::make_tuple(gain, node_id, fpga_id));
                HandleMap[{node_id, fpga_id}] = handle;
                gain_Map[{node_id, fpga_id}] = gain;
            }
        } 
        else {
            std::cout<<"wrong"<<std::endl;
            std::exit(0);
        }
    }

    std::unordered_set<int> moved_nodes;
    std::vector<std::tuple<int, int, int, double>> move_history;
    double total_gain = 0;
    int move_node_num=0;
    while (!FibHeap.empty()){

        auto [gain, node_id, fpga_id] = FibHeap.top();

        auto itmap = HandleMap.find({node_id, fpga_id});
        HandleMap.erase(itmap); //need to delete it
        FibHeap.pop();

        if (moved_nodes.count(node_id) || nodes_fixed_in_fpga[fpga_id] + node_weight[node_id] > capacity ) {
            continue;
        }

        for (int fpga = 0; fpga < k; ++fpga) {
            if (fpga != fpga_id) {
                auto itmap = HandleMap.find({node_id, fpga});
                if (itmap != HandleMap.end()) {
                    FibHeap.erase(itmap->second);
                    HandleMap.erase(itmap);
                }
            }
        }

        nodes_fixed_in_fpga[fpga_id] += node_weight[node_id];
        int original_fpga = *CP[node_id].begin();
        nodes_fixed_in_fpga[original_fpga] -= node_weight[node_id];
        moved_nodes.insert(node_id);
        total_gain += gain;
        move_history.push_back({node_id, original_fpga, fpga_id, total_gain});
        move_node_num++;
        
        if(speedup==true&&move_node_num>int(0.3*n)&&n>500)
            break;  
        
        for (CSRMatrix::InnerIterator it(current_graph, node_id); it; ++it) {
            int neighbor_id = it.index();
            if (moved_nodes.count(neighbor_id))
                continue;
            int neighbor_fpga_id = *CP[neighbor_id].begin();
            int dist1 = fpga_distances[neighbor_fpga_id][original_fpga];
            double priority1 = (neighbor_fpga_id == original_fpga) ? it.value() : 
                               ((dist1 > 1) ? - beta * it.value() * (1+(dist1-1)/3) : 0);
            
            int dist3 = fpga_distances[neighbor_fpga_id][fpga_id];
            double priority3 = (neighbor_fpga_id == fpga_id) ? it.value() : 
                               ((dist3 > 1) ? - beta * it.value() * (1+(dist3-1)/3) : 0);

            for (int fpga = 0; fpga < k; ++fpga) {

                if (fpga != neighbor_fpga_id) {
                
                    if(speedup == true && fpga_distances[neighbor_fpga_id][fpga] > r)
                        continue;
                    auto itmap = HandleMap.find({neighbor_id, fpga});
                    int dist2 = fpga_distances[fpga][original_fpga];
                    double priority2 = (fpga == original_fpga) ? it.value() : 
                                         ((dist2 > 1) ? -beta * it.value() * (1+(dist2-1)/3) : 0);
                    double priority_before = priority2 - priority1;
                    
                    int dist4 = fpga_distances[fpga][fpga_id];
                    double priority4 = (fpga == fpga_id) ? it.value() : 
                                       ((dist4 > 1) ? -beta * it.value() * (1+(dist4-1)/3) : 0);
                    double priority_after = priority4 - priority3;
                    double this_gain = priority_after - priority_before;
                    
                    if (itmap != HandleMap.end()) {
                        
                        double old_gain = std::get<0>(*itmap->second);
                        if(old_gain!=gain_Map[{neighbor_id, fpga}]){
                            std::cout<<"wrong"<<std::endl;
                            std::exit(0);
                        }
                        double new_gain = old_gain + this_gain;

                        FibHeap.erase(itmap->second);
                        HandleMap.erase(itmap);

                        auto new_handle = FibHeap.push(std::make_tuple(new_gain, neighbor_id, fpga));
                        HandleMap[{neighbor_id, fpga}] = new_handle;
                        gain_Map[{neighbor_id, fpga}] = new_gain;

                    }
                    else{
                        double old_gain = gain_Map[{neighbor_id, fpga}];
                        double new_gain = old_gain + this_gain;
                    
                        auto new_handle = FibHeap.push(std::make_tuple(new_gain, neighbor_id, fpga));
                        HandleMap[{neighbor_id, fpga}] = new_handle;
                        gain_Map[{neighbor_id, fpga}] = new_gain;
                    }
                }
            }
            
        }
    }

    int max_gain_index = 0;
    double max_gain = std::get<3>(move_history[0]);;
    for (int i=0;i<move_history.size();i++){
        int gain = std::get<3>(move_history[i]);
        if(gain>max_gain){
            max_gain_index = i;
            max_gain = gain;
        }
    }

    if (max_gain<0)
        return CP;
    else {
        for (size_t i = 0; i <= max_gain_index; ++i) {
            auto [node_id, original_fpga_id, new_fpga_id, total_gain] = move_history[i];
            if (CP[node_id].size()==1 && CP[node_id].count(original_fpga_id)) {
                CP[node_id] = {new_fpga_id};
            } else {
                std::cout<<"wrong"<<std::endl;;
            }
        }
    return CP;
    }
}


std::tuple<CSRMatrix,std::vector<std::tuple<int, int>>> remake_netlist(CSRMatrix&netlist_matrix,std::vector<std::tuple<int, int>>&fixed_nodes){

    int nnn = netlist_matrix.rows();
    std::vector<int> component(nnn, -1);
    int label = 0; 
    int maxSize = 0; 
    int maxLabel = -1;
    
    
    for (int i = 0; i < nnn; ++i) {
        if (component[i] == -1) {
            int currentComponentSize = 0;
            DFS(i, netlist_matrix, component, label, currentComponentSize);

            if (currentComponentSize > maxSize) {
                maxSize = currentComponentSize;
                maxLabel = label;
            }
            
            //std::cout<<"part: "<<label<<" size:"<<currentComponentSize<<std::endl;
            
            ++label;
        }
    }
    
    int new_index = 0;
    std::vector<Triplet<int>> triplets;
    std::vector<int>mapping(nnn,-1);
    for (int i = 0; i < nnn; ++i) {
        if(component[i]==maxLabel){
            triplets.emplace_back(i, new_index, 1);
            mapping[i]=new_index;
            new_index++;
        }
    }
    
    std::vector<std::tuple<int, int>>new_fixed_nodes;
    for(auto&[node_id,fpga_id]:fixed_nodes){
        if(component[node_id]==maxLabel){
            new_fixed_nodes.push_back({mapping[node_id], fpga_id}); 
        }
    }
    
    int k = new_index;
    
    CSRMatrix X(nnn, k);
    X.setFromTriplets(triplets.begin(), triplets.end());
    
    CSRMatrix Xt = X.transpose();
    
    CSRMatrix new_graph = Xt * netlist_matrix * X;
    
    
    std::vector<Triplet<int>> tripletList;

    // Step 2: Iterate through all non-zero elements of new_graph
    for (int k = 0; k < new_graph.outerSize(); ++k) {
        for (CSRMatrix::InnerIterator it(new_graph, k); it; ++it) {
            if (it.row() != it.col()) {  // Only keep non-diagonal elements
                tripletList.emplace_back(it.row(), it.col(), it.value());
            }
        }
    }
    new_graph.setZero();
    new_graph.setFromTriplets(tripletList.begin(), tripletList.end());
    
    
    return {new_graph,new_fixed_nodes};

}


Eigen::SparseMatrix<double, Eigen::RowMajor> computeLaplacian(const CSRMatrix& graph) {

    Eigen::VectorXi degrees = graph * Eigen::VectorXi::Ones(graph.cols());

    Eigen::SparseMatrix<double, Eigen::RowMajor> degree_matrix(graph.rows(), graph.cols());

    for (int i = 0; i < graph.rows(); ++i) {
        degree_matrix.insert(i, i) = static_cast<double>(degrees(i)); 
    }

    Eigen::SparseMatrix<double, Eigen::RowMajor> laplacian = degree_matrix - graph.cast<double>();

    return laplacian;
}

Eigen::VectorXd computeFiedlerVector(const Eigen::SparseMatrix<double, Eigen::RowMajor>& laplacian) {
    Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> solver(laplacian);

    if (solver.info() != Eigen::Success) {
        std::cerr << "Eigen decomposition failed!" << std::endl;
        std::exit(1);
    }

    int fiedler_index = 1;
    Eigen::VectorXd fiedler_vector = solver.eigenvectors().col(fiedler_index);
    return fiedler_vector;
}

std::vector<int> selectKNodesFromFiedler(const Eigen::VectorXd& fiedler_vector, int k) {

    std::vector<std::pair<double, int>> fiedler_pairs;
    for (int i = 0; i < fiedler_vector.size(); ++i) {
        fiedler_pairs.emplace_back(fiedler_vector[i], i);
    }

    if (fiedler_pairs.size() < k) {
        std::cerr << "Warning: largestComponent size is smaller than the number of nodes to select (k)." << std::endl;
        std::exit(0);
    }

    std::sort(fiedler_pairs.begin(), fiedler_pairs.end());

    std::vector<int> selected_nodes;
    
    selected_nodes.push_back(fiedler_pairs.front().second);
    selected_nodes.push_back(fiedler_pairs.back().second);

    if (k > 2) {
        double step = static_cast<double>(fiedler_pairs.size() - 1) / (k - 1);
        double position = step;
        
        for (int i = 1; i < k - 1; ++i) {
            selected_nodes.push_back(fiedler_pairs[std::round(position)].second);
            position += step;
        }
    }

    return selected_nodes;
}


int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <data_filename>" << std::endl;
        return 1;
    }

    std::string data = argv[1];
    std::string netlist_file_path = "./ICCAD2021-TopoPart-Benchmarks/generated_large_netlists/" + data;
    //std::string fixed_nodes_path = "./ICCAD2021-TopoPart-Benchmarks/Titan23_Benchmarks/Titan23_Benchmarks/" + data + ".simulated_annealing.8";

    // Example usage:
    CSRMatrix fpga_matrix = create_FPGA_matrix("./ICCAD2021-TopoPart-Benchmarks/FPGA_Graph/FPGA_Graph/MFS1");
    auto [netlist_matrix0, init_fixed_nodes0] = create_netlist_matrix(netlist_file_path);
    
    //
    auto [netlist_matrix, ignored_fixed_nodes] = remake_netlist(netlist_matrix0, init_fixed_nodes0);
    
    
    std::clock_t start_time = std::clock();
    
    int capacity = ceil(4*netlist_matrix.rows()/fpga_matrix.rows());
    std::cout << "capacity:" << capacity << std::endl;
    
    // Print the number of nodes and edges for FPGA matrix
    std::cout << "FPGA Matrix:" << std::endl;
    std::cout << "  Nodes: " << fpga_matrix.rows() << std::endl;
    std::cout << "  Edges: " << fpga_matrix.nonZeros() / 2 << std::endl; // Divided by 2 because of symmetry

    // Print the number of nodes and edges for Netlist matrix
    std::cout << "Netlist Matrix:" << std::endl;
    std::cout << "  Nodes: " << netlist_matrix.rows() << std::endl;
    std::cout << "  Edges: " << netlist_matrix.nonZeros() / 2 << std::endl; // Divided by 2 because of symmetry
    
    int n = netlist_matrix.rows();
    int num_fpgas = fpga_matrix.rows();
    
    std::vector<std::vector<int>> fpga_distances(num_fpgas, std::vector<int>(num_fpgas, 10000)); // Initialize with 10000 (unreachable)
    for (int fpga_id = 0; fpga_id < num_fpgas; ++fpga_id) {
        std::vector<std::tuple<int, int>> reachable_fpgas = find_reachable_nodes(fpga_matrix, fpga_id, num_fpgas - 1, std::unordered_set<int>());
        for (const auto& [neighbor_id, distance] : reachable_fpgas) {
            fpga_distances[fpga_id][neighbor_id] = distance;
        }
    }
    
    std::cout << "Coarsening phase" << std::endl;
    
    std::vector<CSRMatrix> coarsened_graphs;
    std::vector<std::vector<int>> coarsened_mappings;
    std::vector<std::vector<std::vector<int>>> coarsened_remappings;
    std::vector<std::vector<int>> coarsened_node_weight;
    
    std::vector<int> node_weight(n, 1);
    CSRMatrix current_graph = netlist_matrix;
    std::vector<int> current_node_weight = node_weight;
    
    
    bool coarsen_continue = true;
    int T = 0;
    
    while (coarsen_continue) {
        auto [new_graph, mapping, remapping, new_node_weight] = coarsen_graph(current_graph, current_node_weight, int(capacity*0.95));
        
        if (current_graph.rows() - new_graph.rows() < 30 ||  new_graph.rows() < 3*num_fpgas) {//current_graph.rows() - new_graph.rows() < 20 ||  new_graph.rows() < 3*num_fpgas
            coarsen_continue = false;
            std::cout << "Coarsening iteration: " << T << " " << new_graph.rows() << " " << new_graph.nonZeros()/2 << std::endl;
        }
        
        T++;
        //std::cout << "Coarsening iteration: " << T << " " << new_graph.rows() << " " << new_graph.nonZeros()/2 << std::endl;
        
        coarsened_graphs.push_back(new_graph);
        coarsened_mappings.push_back(mapping);
        coarsened_remappings.push_back(remapping);
        coarsened_node_weight.push_back(new_node_weight);
        
        current_graph = new_graph;
        current_node_weight = new_node_weight;
    }
    
    int start_node = 0;
    
    //std::cout<<1<<std::endl;
    
    std::unordered_map<int, std::vector<double>> distances_dict;

    // Step 1
    Eigen::SparseMatrix<double> laplacian = computeLaplacian(current_graph);

    // Step 2
    Eigen::VectorXd fiedler_vector = computeFiedlerVector(laplacian);

    // Step 3
    std::vector<int> fixed_nodes = selectKNodesFromFiedler(fiedler_vector, num_fpgas);

    Eigen::MatrixXd fixed_distances_matrix(num_fpgas, num_fpgas);

    for (int node : fixed_nodes) {
        distances_dict[node] = dijkstra_shortest_paths(current_graph, node, current_node_weight);
    }
    
    for (int i = 0; i < num_fpgas; ++i) {
        for (int j = 0; j < num_fpgas; ++j) {
            fixed_distances_matrix(i, j) = distances_dict[fixed_nodes[i]][fixed_nodes[j]];
        }
    }
    
    
    auto [best_mapping, best_cost] = simulated_annealing(num_fpgas, fixed_distances_matrix, fpga_matrix, int(10*num_fpgas)); //best_mapping[i]=j represents fpga i contained the fixed node[j]
    
    
    int coarsest_node_number = current_graph.rows();
    std::unordered_set<int> fixed_nodes_set;
    std::unordered_set<int> violation_set;
    std::unordered_set<int> fpga_set;
    std::queue<int> Q;
    for (int i = 0; i < num_fpgas; ++i) {
        fpga_set.insert(i);
    }
    
    
    std::vector<int> coarsest_nodes_weight_fixed_in_this_fpga(num_fpgas, 0);
    
    
    std::vector<std::unordered_set<int>> CP(coarsest_node_number, fpga_set);
    for (int i = 0; i<num_fpgas; i++){
        int node_id = fixed_nodes[best_mapping[i]];
        CP[node_id] = {i};
        fixed_nodes_set.insert(node_id);
        coarsest_nodes_weight_fixed_in_this_fpga[i] += current_node_weight[node_id];
        Q.push(node_id);
    }
    
    
    // 3. Iteratively process nodes in the queue
    while (!Q.empty()) {
        int node_id = Q.front();
        Q.pop();
        std::unordered_set<int> fpga_id = CP[node_id];
    
        if (fpga_id.size() != 1) {
            std::cout << "wrong" << std::endl;
            continue;
        }
    
        int fpga_id_index = *fpga_id.begin();
    
        // Calculate maximum hop distance to other FPGAs
        int d = 0;
        for (const auto& distance : fpga_distances[fpga_id_index]) {
            d = std::max(d, distance);
        }
    
        // Find nodes within distance d in the large graph
        std::vector<std::tuple<int, int>> reachable_nodes = find_reachable_nodes(current_graph, node_id, d, fixed_nodes_set);
    
        // Update candidate FPGAs for reachable nodes
        for (const auto& [node_id2, distance] : reachable_nodes) {
            if (fixed_nodes_set.count(node_id2)||violation_set.count(node_id2)) {
                continue;
            }
            std::unordered_set<int> candidates_copy = CP[node_id2];
            for (int candidate_fpga : candidates_copy) {
                if (fpga_distances[fpga_id_index][candidate_fpga] > distance) {
                    CP[node_id2].erase(candidate_fpga);
                }
            }
    
            if (CP[node_id2].size() == 1) {
                int fpga_id2 = *CP[node_id2].begin();
                if (coarsest_nodes_weight_fixed_in_this_fpga[fpga_id2] + current_node_weight[node_id2]>capacity){
                    CP[node_id2].erase(fpga_id2);
                    violation_set.insert(node_id2);
                }
                else{
                    fixed_nodes_set.insert(node_id2);
                    coarsest_nodes_weight_fixed_in_this_fpga[fpga_id2] += current_node_weight[node_id2];
                    Q.push(node_id2);
                }
            } 
            else if (CP[node_id2].empty()) {
                violation_set.insert(node_id2);
            }
        }
    }
    
    std::cout << "node_weight in each fpga: ";
    for(int i=0;i<num_fpgas;i++)
        std::cout << coarsest_nodes_weight_fixed_in_this_fpga[i]<<" ";
    std::cout << std::endl;
    
    std::cout << "Initial_partitioning phase" << std::endl;
    
    auto uncoarsen_CP = initial_partitioning(current_graph, CP, fpga_distances, current_node_weight, capacity);
    
    std::cout << std::endl;
    std::cout << "Uncoarsening phase" << std::endl;
    double beta = 1000.0;
    bool speedup = false;
    uncoarsen_CP = FM(current_graph, uncoarsen_CP, fpga_distances, current_node_weight, beta, capacity, speedup);
    int coarsen_level = coarsened_graphs.size();
    for (int i=coarsen_level-1;i>=0;i--){
        std::vector<std::vector<int>> map_to_previous_level = coarsened_remappings[i];
        CSRMatrix graph_previous_level;
        std::vector<int>node_weight_previous_level;
        if(i==coarsen_level-7)
            speedup=true;
        if (i>0){
            graph_previous_level = coarsened_graphs[i-1];
            node_weight_previous_level = coarsened_node_weight[i-1];
        }
        else{
            graph_previous_level = netlist_matrix;
            node_weight_previous_level = std::vector<int>(n, 1);
        }
        int nodenum = graph_previous_level.rows();
        int coarsen_nodenum = coarsened_graphs[i].rows();
        std::vector<std::unordered_set<int>> CP_previous_level(nodenum);

        for (int j=0;j<coarsen_nodenum;j++){
            for (int z=0;z<map_to_previous_level[j].size();z++){
                CP_previous_level[map_to_previous_level[j][z]] = uncoarsen_CP[j];
            }
        }
        for (int j=0;j<nodenum;j++)
            if(CP_previous_level[j].size()!=1)
                std::cout<<"wrong:"<<j<<std::endl;
        if(i<5)
            beta=20000.0;

        uncoarsen_CP = FM(graph_previous_level, CP_previous_level, fpga_distances, node_weight_previous_level, beta, capacity, speedup);
        for (int j = 0; j < uncoarsen_CP.size(); ++j) {
            if(uncoarsen_CP[j].size()!=1){
                std::cout<<"uncoarsen wrong"<<std::endl;
                std::exit(0);
            }
        }
    }
    
    
    
    
    int cut_size = 0;
    int violation = 0;
    for(int i=0;i<n;i++){
        int fpga_id = *uncoarsen_CP[i].begin();
        for (CSRMatrix::InnerIterator it(netlist_matrix, i); it; ++it) {
            int neighbor_id = it.index();
            if (neighbor_id < i)
                continue;
            int neighbor_fpga_id = *uncoarsen_CP[neighbor_id].begin();
            if (fpga_distances[fpga_id][neighbor_fpga_id] >= 1) {
                cut_size += it.value();
            }
            if(fpga_distances[fpga_id][neighbor_fpga_id] > 1) {
                violation += it.value();
            }
        }
    }
    std::cout<<"cut_size:"<<cut_size<<" violation:"<<violation<<std::endl;
    
    
    std::vector<int> nodes_weight_fixed_in_this_fpga(num_fpgas, 0);
    std::vector<int> nodes_fixed_in_this_fpga(num_fpgas, 0);
    std::cout<<n<<std::endl;
    for (int node_id = 0; node_id < n; ++node_id) {
        if (uncoarsen_CP[node_id].size() == 1){
            int fpga_id_index = *uncoarsen_CP[node_id].begin();
            nodes_weight_fixed_in_this_fpga[fpga_id_index] += 1;
            nodes_fixed_in_this_fpga[fpga_id_index] += 1;
        }
        else
            std::cout<<"CP's size is not 1 for node:"<<node_id<<std::endl;
    }
    
    std::cout << "nodes_weight_fixed_in_this_fpga: ";
    for (int i = 0; i < num_fpgas; ++i) {
      std::cout << nodes_weight_fixed_in_this_fpga[i] << " ";
    }
    std::cout << std::endl;
    
    
    std::cout << "nodes_fixed_in_this_fpga: ";
    for (int i = 0; i < num_fpgas; ++i) {
      std::cout << nodes_fixed_in_this_fpga[i] << " ";
    }
    std::cout << std::endl;
    
    std::clock_t end_time = std::clock();
    double cpu_time_used = static_cast<double>(end_time - start_time) / CLOCKS_PER_SEC;
  
    std::cout << "Running time: " << cpu_time_used << " s" << std::endl;

    return 0;
    
}