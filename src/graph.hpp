#pragma once
#include <vector>
#include <stdexcept>

struct Graph {
    int n = 0;
    std::vector<std::vector<int>> adj;

    Graph() = default;
    explicit Graph(int n_) : n(n_), adj(n_) {}

    void add_edge(int u, int v) {
        if (u < 0 || v < 0 || u >= n || v >= n) throw std::out_of_range("bad vertex");
        if (u == v) return;
        adj[u].push_back(v);
        adj[v].push_back(u);
    }

    int m() const {
        long long sum = 0;
        for (auto& lst : adj) sum += (long long)lst.size();
        return (int)(sum / 2);
    }
};
