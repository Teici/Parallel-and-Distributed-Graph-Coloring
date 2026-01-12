#include "io.hpp"

#include <fstream>
#include <stdexcept>

Graph read_graph_edge_list(const std::string& path, bool one_based) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("Cannot open file: " + path);

    int n = 0, m = 0;
    in >> n >> m;
    if (!in) throw std::runtime_error("Bad header (n m) in file: " + path);

    Graph g(n);
    for (int i = 0; i < m; i++) {
        int u, v;
        in >> u >> v;
        if (!in) throw std::runtime_error("Bad edge line in file: " + path);
        if (one_based) { u--; v--; }
        g.add_edge(u, v);
    }
    return g;
}

void write_graph_edge_list(const std::string& path, const Graph& g, bool one_based) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("Cannot write file: " + path);

    int m = 0;
    for (int u = 0; u < g.n; u++)
        for (int v : g.adj[u])
            if (u < v) m++;

    out << g.n << " " << m << "\n";
    for (int u = 0; u < g.n; u++) {
        for (int v : g.adj[u]) {
            if (u < v) {
                int a = one_based ? (u + 1) : u;
                int b = one_based ? (v + 1) : v;
                out << a << " " << b << "\n";
            }
        }
    }
}
