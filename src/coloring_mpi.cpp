#include "graph.hpp"
#include "io.hpp"
#include "timer.hpp"

#include <iostream>
#include <vector>
#include <algorithm>
#include <atomic>

#ifdef HAS_MPI
#include <mpi.h>
#endif

// Same helpers (copy from serial/threads)
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

static bool backtrack_exact_stop(const Graph& g, int k,
                                std::vector<int>& color,
                                const std::vector<int>& degree,
                                std::atomic<bool>& stop,
                                long long& nodes,
                                long long& backtracks) {
    if (stop.load(std::memory_order_relaxed)) return false;
    nodes++;

    int u = choose_vertex_dsatur(g, color, degree);
    if (u == -1) return true;

    for (int c = 0; c < k; c++) {
        if (stop.load(std::memory_order_relaxed)) return false;
        if (!can_color(g, u, c, color)) continue;
        color[u] = c;
        if (backtrack_exact_stop(g, k, color, degree, stop, nodes, backtracks)) return true;
        color[u] = -1;
    }
    backtracks++;
    return false;
}

static void generate_subproblems(const Graph& g, int k,
                                 const std::vector<int>& degree,
                                 int split_depth,
                                 std::vector<std::vector<int>>& out_colors) {
    out_colors.clear();
    out_colors.push_back(std::vector<int>(g.n, -1));

    for (int depth = 0; depth < split_depth; depth++) {
        std::vector<std::vector<int>> next;
        next.reserve(out_colors.size() * (size_t)k);

        for (auto &col : out_colors) {
            int u = choose_vertex_dsatur(g, col, degree);
            if (u == -1) { next.push_back(col); continue; }
            for (int c = 0; c < k; c++) {
                if (!can_color(g, u, c, col)) continue;
                auto child = col;
                child[u] = c;
                next.push_back(std::move(child));
            }
        }
        out_colors.swap(next);
        if (out_colors.empty()) break;
    }
}

static bool verify_coloring(const Graph& g, const std::vector<int>& color, int k) {
    if ((int)color.size() != g.n) return false;
    for (int u = 0; u < g.n; u++) {
        if (color[u] < 0 || color[u] >= k) return false;
        for (int v : g.adj[u]) {
            if (u < v && color[u] == color[v]) return false;
        }
    }
    return true;
}

int main(int argc, char** argv) {
#ifndef HAS_MPI
    std::cerr << "MPI not available (HAS_MPI not defined). Reconfigure with MPI.\n";
    return 1;
#else
    MPI_Init(&argc, &argv);

    int rank = 0, size = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (argc < 5) {
        if (rank == 0) {
            std::cerr << "Usage: mpirun -np <p> color_mpi <graph_file> <k> <split_depth> <one_based=0|1>\n";
        }
        MPI_Finalize();
        return 1;
    }

    std::string path = argv[1];
    int k = std::stoi(argv[2]);
    int split_depth = std::stoi(argv[3]);
    bool one_based = (std::stoi(argv[4]) != 0);

    Graph g = read_graph_edge_list(path, one_based);

    std::vector<int> degree(g.n);
    for (int i = 0; i < g.n; i++) degree[i] = (int)g.adj[i].size();

    const int TAG_WORK = 1;
    const int TAG_STOP = 2;
    const int TAG_SOL  = 3;

    std::atomic<bool> stop(false);

    Timer t;

    if (rank == 0) {
        std::vector<std::vector<int>> subs;
        generate_subproblems(g, k, degree, split_depth, subs);

        std::cout << "MPI master: n=" << g.n << " m=" << g.m()
                  << " k=" << k << " split_depth=" << split_depth
                  << " subproblems=" << subs.size()
                  << " workers=" << (size - 1) << "\n";

        int next_job = 0;
        int active_workers = 0;

        // Send initial jobs
        for (int w = 1; w < size && next_job < (int)subs.size(); w++) {
            MPI_Send(subs[next_job].data(), g.n, MPI_INT, w, TAG_WORK, MPI_COMM_WORLD);
            next_job++;
            active_workers++;
        }

        std::vector<int> solution;
        bool found = false;

        while (active_workers > 0 && !found) {
            // wait for either solution or worker request/finish
            MPI_Status st;
            // We’ll receive either:
            // - TAG_SOL: solution vector from worker
            // - TAG_STOP: worker says "done/no solution"
            std::vector<int> buf(g.n);

            MPI_Recv(buf.data(), g.n, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &st);
            int src = st.MPI_SOURCE;

            if (st.MPI_TAG == TAG_SOL) {
                solution = std::move(buf);
                found = true;
                break;
            } else if (st.MPI_TAG == TAG_STOP) {
                // worker finished its job without solution; give new job if available
                if (next_job < (int)subs.size()) {
                    MPI_Send(subs[next_job].data(), g.n, MPI_INT, src, TAG_WORK, MPI_COMM_WORLD);
                    next_job++;
                } else {
                    // no more work: tell worker to stop
                    MPI_Send(nullptr, 0, MPI_INT, src, TAG_STOP, MPI_COMM_WORLD);
                    active_workers--;
                }
            }
        }

        // Broadcast stop to all workers
        for (int w = 1; w < size; w++) {
            MPI_Send(nullptr, 0, MPI_INT, w, TAG_STOP, MPI_COMM_WORLD);
        }

        double sec = t.seconds();
        std::cout << "time=" << sec << "s found=" << (found ? "true" : "false") << "\n";
        if (found) {
            std::cout << "verify=" << (verify_coloring(g, solution, k) ? "OK" : "FAIL") << "\n";
        }
    } else {
        while (true) {
            MPI_Status st;
            std::vector<int> assignment(g.n);

            MPI_Recv(assignment.data(), g.n, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &st);
            if (st.MPI_TAG == TAG_STOP) break;

            long long nodes = 0, backs = 0;
            bool ok = backtrack_exact_stop(g, k, assignment, degree, stop, nodes, backs);

            if (ok) {
                MPI_Send(assignment.data(), g.n, MPI_INT, 0, TAG_SOL, MPI_COMM_WORLD);
                break;
            } else {
                // tell master: finished without solution (reuse same size payload)
                MPI_Send(assignment.data(), g.n, MPI_INT, 0, TAG_STOP, MPI_COMM_WORLD);
            }
        }
    }

    MPI_Finalize();
    return 0;
#endif
}
