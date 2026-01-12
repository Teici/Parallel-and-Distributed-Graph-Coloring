#include "coloring.hpp"
#include "timer.hpp"

#include <algorithm>
#include <vector>

static bool can_color(const Graph& g, int u, int c, const std::vector<int>& color) {
    for (int v : g.adj[u]) if (color[v] == c) return false;
    return true;
}

static int choose_vertex_dsatur(const Graph& g,
                               const std::vector<int>& color,
                               const std::vector<int>& degree) {
    int best = -1, best_sat = -1, best_deg = -1;
    std::vector<int> seen; seen.reserve(64);

    for (int u = 0; u < g.n; u++) {
        if (color[u] != -1) continue;

        seen.clear();
        for (int v : g.adj[u]) {
            int c = color[v];
            if (c != -1) seen.push_back(c);
        }
        std::sort(seen.begin(), seen.end());
        seen.erase(std::unique(seen.begin(), seen.end()), seen.end());
        int sat = (int)seen.size();

        if (sat > best_sat || (sat == best_sat && degree[u] > best_deg)) {
            best = u; best_sat = sat; best_deg = degree[u];
        }
    }
    return best;
}

static bool backtrack_exact(const Graph& g, int k,
                            std::vector<int>& color,
                            const std::vector<int>& degree,
                            long long& nodes,
                            long long& backtracks,
                            const Timer& t,
                            double max_seconds) {
    nodes++;

    if (max_seconds > 0.0 && t.seconds() > max_seconds) return false; // time limit reached

    int u = choose_vertex_dsatur(g, color, degree);
    if (u == -1) return true;

    for (int c = 0; c < k; c++) {
        if (!can_color(g, u, c, color)) continue;
        color[u] = c;
        if (backtrack_exact(g, k, color, degree, nodes, backtracks, t, max_seconds)) return true;
        color[u] = -1;
    }

    backtracks++;
    return false;
}

ColoringResult color_serial_exact(const Graph& g, int k, double max_seconds) {
    if (k == 2) return color_two_color_bipartite(g);
    {
        auto gr = color_greedy_dsatur(g, k);
        if (gr.success) return gr;
    }

    ColoringResult res;
    res.color.assign(g.n, -1);

    std::vector<int> degree(g.n);
    for (int i = 0; i < g.n; i++) degree[i] = (int)g.adj[i].size();

    Timer t;
    res.success = backtrack_exact(g, k, res.color, degree, res.nodes, res.backtracks, t, max_seconds);
    res.seconds = t.seconds();

    if (!res.success) res.color.clear();
    return res;
}
