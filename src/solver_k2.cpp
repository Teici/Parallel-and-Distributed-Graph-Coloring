#include "coloring.hpp"
#include "timer.hpp"
#include <queue>

ColoringResult color_two_color_bipartite(const Graph& g) {
    ColoringResult res;
    res.color.assign(g.n, -1);

    Timer t;
    std::queue<int> q;

    for (int start = 0; start < g.n; start++) {
        if (res.color[start] != -1) continue;
        res.color[start] = 0;
        q.push(start);

        while (!q.empty()) {
            int u = q.front(); q.pop();
            res.nodes++;

            for (int v : g.adj[u]) {
                if (res.color[v] == -1) {
                    res.color[v] = 1 - res.color[u];
                    q.push(v);
                } else if (res.color[v] == res.color[u]) {
                    res.success = false;
                    res.seconds = t.seconds();
                    res.color.clear();
                    return res;
                }
            }
        }
    }

    res.success = true;
    res.seconds = t.seconds();
    return res;
}
