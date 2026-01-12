#include "graph.hpp"
#include "io.hpp"
#include "coloring.hpp"
#include "timer.hpp"
#include "generate.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <atomic>
#include <iomanip>
#include <cstdint>

#ifdef HAS_MPI
#include <mpi.h>
#endif

static std::string get_arg(int argc, char** argv, const std::string& key, const std::string& def = "") {
    for (int i = 1; i + 1 < argc; i++) {
        if (std::string(argv[i]) == key) return std::string(argv[i + 1]);
    }
    return def;
}

static void usage() {
    std::cerr <<
        "Usage:\n"
        "  main --mode serial  --graph <file> --k <k> [--one_based 0|1] [--max_sec <sec>]\n"
        "  main --mode threads --graph <file> --k <k> --threads <t> --split <d> [--one_based 0|1] [--max_sec <sec>]\n"
        "  mpirun -np <p> main --mode mpi --graph <file> --k <k> --split <d> [--one_based 0|1] [--max_sec <sec>]\n"
        "\n"
        "  main --mode gen --type complete --n <n> --out <file>\n"
        "  main --mode gen --type cycle    --n <n> --out <file>\n"
        "  main --mode gen --type grid     --rows <r> --cols <c> --out <file>\n"
        "  main --mode gen --type random   --n <n> --p <p> --seed <s> --out <file>\n"
        "  main --mode gen --type bipartite --left <L> --right <R> --p <p> --seed <s> --out <file>\n"
        "\n"
        "  main --mode bench --solver serial|threads|mpi --graph <file> --k <k> --runs <R>\n"
        "      [--threads <t> --split <d>] [--max_sec <sec>]\n";
}

#ifdef HAS_MPI
static ColoringResult color_mpi_exact(const Graph& g, int k, int split_depth, double max_seconds) {
    int rank = 0, size = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (k == 2) {
        MPI_Barrier(MPI_COMM_WORLD);
        ColoringResult r;
        if (rank == 0) r = color_two_color_bipartite(g);
        MPI_Barrier(MPI_COMM_WORLD);
        return r;
    }
    {
        MPI_Barrier(MPI_COMM_WORLD);
        ColoringResult gr;
        if (rank == 0) gr = color_greedy_dsatur(g, k);
        MPI_Barrier(MPI_COMM_WORLD);
        if (rank == 0 && gr.success) return gr;
    }

    if (size == 1) {
        if (rank == 0) return color_serial_exact(g, k, max_seconds);
        return {};
    }


    std::vector<int> degree(g.n);
    for (int i = 0; i < g.n; i++) degree[i] = (int)g.adj[i].size();

    auto can_color = [&](int u, int c, const std::vector<int>& col) {
        for (int v : g.adj[u]) if (col[v] == c) return false;
        return true;
    };

    auto choose_vertex_dsatur = [&](const std::vector<int>& col) {
        int best = -1, best_sat = -1, best_deg = -1;
        std::vector<int> seen; seen.reserve(64);

        for (int u = 0; u < g.n; u++) {
            if (col[u] != -1) continue;
            seen.clear();
            for (int v : g.adj[u]) {
                int cc = col[v];
                if (cc != -1) seen.push_back(cc);
            }
            std::sort(seen.begin(), seen.end());
            seen.erase(std::unique(seen.begin(), seen.end()), seen.end());
            int sat = (int)seen.size();

            if (sat > best_sat || (sat == best_sat && degree[u] > best_deg)) {
                best = u; best_sat = sat; best_deg = degree[u];
            }
        }
        return best;
    };

    auto generate_subs = [&]() {
        std::vector<std::vector<int>> out;
        out.push_back(std::vector<int>(g.n, -1));
        for (int depth = 0; depth < split_depth; depth++) {
            std::vector<std::vector<int>> next;
            next.reserve(out.size() * (size_t)k);
            for (auto &col : out) {
                int u = choose_vertex_dsatur(col);
                if (u == -1) { next.push_back(col); continue; }
                for (int c = 0; c < k; c++) {
                    if (!can_color(u, c, col)) continue;
                    auto child = col;
                    child[u] = c;
                    next.push_back(std::move(child));
                }
            }
            out.swap(next);
            if (out.empty()) break;
        }
        return out;
    };

    std::atomic<bool> stop(false);
    auto backtrack = [&](auto&& self, std::vector<int>& col, long long& nodes, long long& backs, const Timer& t) -> bool {
        nodes++;
        if (max_seconds > 0.0 && t.seconds() > max_seconds) return false;
        if (stop.load(std::memory_order_relaxed)) return false;

        int u = choose_vertex_dsatur(col);
        if (u == -1) return true;

        for (int c = 0; c < k; c++) {
            if (stop.load(std::memory_order_relaxed)) return false;
            if (max_seconds > 0.0 && t.seconds() > max_seconds) return false;
            if (!can_color(u, c, col)) continue;

            col[u] = c;
            if (self(self, col, nodes, backs, t)) return true;
            col[u] = -1;
        }
        backs++;
        return false;
    };

    const int TAG_WORK = 10;
    const int TAG_STOP = 11;
    const int TAG_RESULT = 12;
    const int TAG_SOL = 13;

    MPI_Barrier(MPI_COMM_WORLD);
    Timer t_all;

    if (rank == 0) {
        auto subs = generate_subs();
        int next_job = 0;
        int active = 0;

        for (int w = 1; w < size && next_job < (int)subs.size(); w++) {
            MPI_Send(subs[next_job].data(), g.n, MPI_INT, w, TAG_WORK, MPI_COMM_WORLD);
            next_job++; active++;
        }

        bool found = false;
        std::vector<int> solution;

        long long nodes_total = 0;
        long long backs_total = 0;

        while (active > 0 && !found) {
            long long stats[3] = {0, 0, 0}; // status,nodes,backs
            MPI_Status st{};
            MPI_Recv(stats, 3, MPI_LONG_LONG, MPI_ANY_SOURCE, TAG_RESULT, MPI_COMM_WORLD, &st);
            int src = st.MPI_SOURCE;

            nodes_total += stats[1];
            backs_total += stats[2];

            if (stats[0] == 1) {
                solution.assign(g.n, -1);
                MPI_Recv(solution.data(), g.n, MPI_INT, src, TAG_SOL, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                found = true;
                break;
            }

            if (next_job < (int)subs.size() && !(max_seconds > 0.0 && t_all.seconds() > max_seconds)) {
                MPI_Send(subs[next_job].data(), g.n, MPI_INT, src, TAG_WORK, MPI_COMM_WORLD);
                next_job++;
            } else {
                MPI_Send(nullptr, 0, MPI_INT, src, TAG_STOP, MPI_COMM_WORLD);
                active--;
            }
        }

        for (int w = 1; w < size; w++) MPI_Send(nullptr, 0, MPI_INT, w, TAG_STOP, MPI_COMM_WORLD);

        MPI_Barrier(MPI_COMM_WORLD);
        ColoringResult res;
        res.seconds = t_all.seconds();
        res.success = found;
        res.nodes = nodes_total;
        res.backtracks = backs_total;
        if (found) res.color = std::move(solution);
        return res;
    } else {
        while (true) {
            std::vector<int> assignment(g.n, -1);
            MPI_Status st{};
            MPI_Recv(assignment.data(), g.n, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &st);
            if (st.MPI_TAG == TAG_STOP) break;

            long long nodes = 0, backs = 0;
            bool ok = backtrack(backtrack, assignment, nodes, backs, t_all);

            long long stats[3] = { ok ? 1LL : 0LL, nodes, backs };
            MPI_Send(stats, 3, MPI_LONG_LONG, 0, TAG_RESULT, MPI_COMM_WORLD);
            if (ok) {
                MPI_Send(assignment.data(), g.n, MPI_INT, 0, TAG_SOL, MPI_COMM_WORLD);
                break;
            }
        }
        MPI_Barrier(MPI_COMM_WORLD);
        return {};
    }
}
#endif

int main(int argc, char** argv) {
    std::string mode = get_arg(argc, argv, "--mode", "");
    if (mode.empty()) { usage(); return 1; }

    bool want_mpi = false;
    if (mode == "mpi") want_mpi = true;
    if (mode == "bench" && get_arg(argc, argv, "--solver", "") == "mpi") want_mpi = true;

#ifdef HAS_MPI
    if (want_mpi) MPI_Init(&argc, &argv);
#endif

    int rank = 0;
#ifdef HAS_MPI
    if (want_mpi) MPI_Comm_rank(MPI_COMM_WORLD, &rank);
#endif

    if (mode == "gen") {
        if (rank == 0) {
            std::string type = get_arg(argc, argv, "--type", "");
            std::string out = get_arg(argc, argv, "--out", "");
            if (type.empty() || out.empty()) { usage(); return 1; }

            Graph gg;
            if (type == "complete") {
                int n = std::stoi(get_arg(argc, argv, "--n", "0"));
                gg = make_complete(n);
            } else if (type == "cycle") {
                int n = std::stoi(get_arg(argc, argv, "--n", "0"));
                gg = make_cycle(n);
            } else if (type == "grid") {
                int r = std::stoi(get_arg(argc, argv, "--rows", "0"));
                int c = std::stoi(get_arg(argc, argv, "--cols", "0"));
                gg = make_grid(r, c);
            } else if (type == "random") {
                int n = std::stoi(get_arg(argc, argv, "--n", "0"));
                double p = std::stod(get_arg(argc, argv, "--p", "0.0"));
                uint64_t seed = (uint64_t)std::stoull(get_arg(argc, argv, "--seed", "1"));
                gg = make_random_gnp(n, p, seed);
            } else if (type == "bipartite") {
                int L = std::stoi(get_arg(argc, argv, "--left", "0"));
                int R = std::stoi(get_arg(argc, argv, "--right", "0"));
                double p = std::stod(get_arg(argc, argv, "--p", "0.0"));
                uint64_t seed = (uint64_t)std::stoull(get_arg(argc, argv, "--seed", "1"));
                gg = make_bipartite_random(L, R, p, seed);
            } else {
                std::cerr << "Unknown --type: " << type << "\n";
                return 1;
            }

            write_graph_edge_list(out, gg, false);
            std::cout << "Wrote " << out << " n=" << gg.n << " m=" << gg.m() << "\n";
        }
#ifdef HAS_MPI
        if (want_mpi) MPI_Finalize();
#endif
        return 0;
    }

    std::string graph_path = get_arg(argc, argv, "--graph", "");
    std::string k_str = get_arg(argc, argv, "--k", "");
    bool one_based = (get_arg(argc, argv, "--one_based", "0") != "0");
    double max_sec = std::stod(get_arg(argc, argv, "--max_sec", "0"));

    if (graph_path.empty() || k_str.empty()) { if (rank == 0) usage(); return 1; }
    int k = std::stoi(k_str);

    Graph g = read_graph_edge_list(graph_path, one_based);

    if (mode == "serial") {
        if (rank == 0) {
            auto res = color_serial_exact(g, k, max_sec);
            std::cout << "success=" << (res.success ? "true" : "false")
                      << " time=" << std::setprecision(10) << res.seconds << "s"
                      << " nodes=" << res.nodes << " backtracks=" << res.backtracks << "\n";
            if (res.success) std::cout << "verify=" << (verify_coloring(g, res.color, k) ? "OK" : "FAIL") << "\n";
        }
#ifdef HAS_MPI
        if (want_mpi) MPI_Finalize();
#endif
        return 0;
    }

    if (mode == "threads") {
        if (rank == 0) {
            int threads = std::stoi(get_arg(argc, argv, "--threads", "8"));
            int split = std::stoi(get_arg(argc, argv, "--split", "5"));
            auto res = color_threads_exact(g, k, threads, split, max_sec);
            std::cout << "success=" << (res.success ? "true" : "false")
                      << " time=" << std::setprecision(10) << res.seconds << "s"
                      << " nodes=" << res.nodes << " backtracks=" << res.backtracks << "\n";
            if (res.success) std::cout << "verify=" << (verify_coloring(g, res.color, k) ? "OK" : "FAIL") << "\n";
        }
#ifdef HAS_MPI
        if (want_mpi) MPI_Finalize();
#endif
        return 0;
    }

    if (mode == "mpi") {
#ifndef HAS_MPI
        if (rank == 0) std::cerr << "Built without MPI.\n";
        return 1;
#else
        int split = std::stoi(get_arg(argc, argv, "--split", "5"));
        auto res = color_mpi_exact(g, k, split, max_sec);
        if (rank == 0) {
            std::cout << "success=" << (res.success ? "true" : "false")
                      << " time=" << std::setprecision(10) << res.seconds << "s"
                      << " nodes=" << res.nodes << " backtracks=" << res.backtracks << "\n";
            if (res.success) std::cout << "verify=" << (verify_coloring(g, res.color, k) ? "OK" : "FAIL") << "\n";
        }
        MPI_Finalize();
        return 0;
#endif
    }

    if (mode == "bench") {
        std::string solver = get_arg(argc, argv, "--solver", "serial");
        int runs = std::stoi(get_arg(argc, argv, "--runs", "5"));
        if (runs < 1) runs = 1;

        if (rank == 0) std::cout << "run,time,success,nodes,backtracks\n";

        double sum = 0.0;
        int ok = 0;

        for (int r = 0; r < runs; r++) {
            ColoringResult rr;

            if (solver == "serial") {
                if (rank == 0) rr = color_serial_exact(g, k, max_sec);
            } else if (solver == "threads") {
                if (rank == 0) {
                    int threads = std::stoi(get_arg(argc, argv, "--threads", "8"));
                    int split = std::stoi(get_arg(argc, argv, "--split", "5"));
                    rr = color_threads_exact(g, k, threads, split, max_sec);
                }
            } else if (solver == "mpi") {
#ifdef HAS_MPI
                int split = std::stoi(get_arg(argc, argv, "--split", "5"));
                rr = color_mpi_exact(g, k, split, max_sec);
#else
                if (rank == 0) std::cerr << "Built without MPI.\n";
                break;
#endif
            } else {
                if (rank == 0) std::cerr << "Unknown --solver\n";
                break;
            }

            if (rank == 0) {
                std::cout << r << "," << std::setprecision(10) << rr.seconds << ","
                          << (rr.success ? 1 : 0) << "," << rr.nodes << "," << rr.backtracks << "\n";
                sum += rr.seconds;
                ok += rr.success ? 1 : 0;
            }
        }

        if (rank == 0) {
            std::cout << "avg," << std::setprecision(10) << (sum / runs)
                      << ",ok=" << ok << "/" << runs << ",,\n";
        }

#ifdef HAS_MPI
        if (want_mpi) MPI_Finalize();
#endif
        return 0;
    }

    if (rank == 0) usage();

#ifdef HAS_MPI
    if (want_mpi) MPI_Finalize();
#endif
    return 1;
}
