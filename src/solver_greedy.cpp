#include "coloring.hpp"
#include "timer.hpp"
#include <algorithm>
#include <vector>

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

static bool can_color(const Graph& g, int u, int c, const std::vector<int>& color) {
    for (int v : g.adj[u]) if (color[v] == c) return false;
    return true;
}

ColoringResult color_greedy_dsatur(const Graph& g, int k) {
    ColoringResult res;
    res.color.assign(g.n, -1);

    std::vector<int> degree(g.n);
    for (int i = 0; i < g.n; i++) degree[i] = (int)g.adj[i].size();

    Timer t;

    for (int step = 0; step < g.n; step++) {
        int u = choose_vertex_dsatur(g, res.color, degree);
        if (u == -1) break;

        res.nodes++;

        bool placed = false;
        for (int c = 0; c < k; c++) {
            if (can_color(g, u, c, res.color)) {
                res.color[u] = c;
                placed = true;
                break;
            }
        }
        if (!placed) {
            res.success = false;
            res.seconds = t.seconds();
            res.color.clear();
            return res;
        }
    }

    res.success = true;
    res.seconds = t.seconds();
    return res;
}
