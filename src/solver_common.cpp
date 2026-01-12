#include "coloring.hpp"

bool verify_coloring(const Graph& g, const std::vector<int>& color, int k) {
    if ((int)color.size() != g.n) return false;
    for (int u = 0; u < g.n; u++) {
        if (color[u] < 0 || color[u] >= k) return false;
        for (int v : g.adj[u]) {
            if (u < v && color[u] == color[v]) return false;
        }
    }
    return true;
}
