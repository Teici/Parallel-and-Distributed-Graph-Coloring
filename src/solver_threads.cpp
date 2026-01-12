#include "coloring.hpp"
#include "timer.hpp"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <queue>
#include <thread>
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

struct Subproblem { std::vector<int> color; };

static bool backtrack_exact_stop(const Graph& g, int k,
                                 std::vector<int>& color,
                                 const std::vector<int>& degree,
                                 std::atomic<bool>& found,
                                 long long& nodes,
                                 long long& backtracks,
                                 const Timer& t,
                                 double max_seconds) {
    if (found.load(std::memory_order_relaxed)) return false;
    nodes++;

    if (max_seconds > 0.0 && t.seconds() > max_seconds) return false;

    int u = choose_vertex_dsatur(g, color, degree);
    if (u == -1) return true;

    for (int c = 0; c < k; c++) {
        if (found.load(std::memory_order_relaxed)) return false;
        if (max_seconds > 0.0 && t.seconds() > max_seconds) return false;

        if (!can_color(g, u, c, color)) continue;
        color[u] = c;
        if (backtrack_exact_stop(g, k, color, degree, found, nodes, backtracks, t, max_seconds)) return true;
        color[u] = -1;
    }

    backtracks++;
    return false;
}

static void generate_subproblems(const Graph& g, int k,
                                 const std::vector<int>& degree,
                                 int split_depth,
                                 std::vector<Subproblem>& out) {
    out.clear();
    out.push_back(Subproblem{std::vector<int>(g.n, -1)});

    for (int depth = 0; depth < split_depth; depth++) {
        std::vector<Subproblem> next;
        next.reserve(out.size() * (size_t)k);

        for (auto &sp : out) {
            int u = choose_vertex_dsatur(g, sp.color, degree);
            if (u == -1) { next.push_back(sp); continue; }

            for (int c = 0; c < k; c++) {
                if (!can_color(g, u, c, sp.color)) continue;
                auto child = sp.color;
                child[u] = c;
                next.push_back(Subproblem{std::move(child)});
            }
        }
        out.swap(next);
        if (out.empty()) break;
    }
}

template<typename T>
struct TSQueue {
    std::queue<T> q;
    std::mutex m;

    void push(T v) {
        std::lock_guard<std::mutex> lk(m);
        q.push(std::move(v));
    }

    bool pop(T& out) {
        std::lock_guard<std::mutex> lk(m);
        if (q.empty()) return false;
        out = std::move(q.front());
        q.pop();
        return true;
    }
};

ColoringResult color_threads_exact(const Graph& g, int k, int threads, int split_depth, double max_seconds) {
    if (k == 2) return color_two_color_bipartite(g);
    {
        auto gr = color_greedy_dsatur(g, k);
        if (gr.success) return gr;
    }

    ColoringResult res;

    std::vector<int> degree(g.n);
    for (int i = 0; i < g.n; i++) degree[i] = (int)g.adj[i].size();

    std::vector<Subproblem> subs;
    generate_subproblems(g, k, degree, split_depth, subs);

    TSQueue<Subproblem> work;
    for (auto &sp : subs) work.push(std::move(sp));

    std::atomic<bool> found(false);
    std::mutex sol_m;
    std::vector<int> solution;

    std::atomic<long long> nodes_sum(0), backs_sum(0);
    Timer t;

    std::vector<std::thread> pool;
    pool.reserve(threads);

    for (int i = 0; i < threads; i++) {
        pool.emplace_back([&]() {
            Subproblem sp;
            while (!found.load(std::memory_order_relaxed)) {
                if (max_seconds > 0.0 && t.seconds() > max_seconds) break;
                if (!work.pop(sp)) break;

                long long nodes = 0, backs = 0;
                auto local = sp.color;

                bool ok = backtrack_exact_stop(g, k, local, degree, found, nodes, backs, t, max_seconds);
                nodes_sum.fetch_add(nodes);
                backs_sum.fetch_add(backs);

                if (ok) {
                    bool expected = false;
                    if (found.compare_exchange_strong(expected, true)) {
                        std::lock_guard<std::mutex> lk(sol_m);
                        solution = std::move(local);
                    }
                    return;
                }
            }
        });
    }

    for (auto &th : pool) th.join();

    res.seconds = t.seconds();
    res.nodes = nodes_sum.load();
    res.backtracks = backs_sum.load();
    res.success = found.load();

    if (res.success) res.color = std::move(solution);
    return res;
}
