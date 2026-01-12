#include "generate.hpp"
#include <random>
#include <stdexcept>

Graph make_complete(int n) {
    Graph g(n);
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            g.add_edge(i, j);
    return g;
}

Graph make_cycle(int n) {
    if (n < 3) throw std::runtime_error("cycle needs n>=3");
    Graph g(n);
    for (int i = 0; i < n; i++) g.add_edge(i, (i + 1) % n);
    return g;
}

Graph make_grid(int rows, int cols) {
    if (rows <= 0 || cols <= 0) throw std::runtime_error("bad grid size");
    Graph g(rows * cols);
    auto id = [cols](int r, int c) { return r * cols + c; };

    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++) {
            if (r + 1 < rows) g.add_edge(id(r, c), id(r + 1, c));
            if (c + 1 < cols) g.add_edge(id(r, c), id(r, c + 1));
        }
    return g;
}

Graph make_random_gnp(int n, double p, uint64_t seed) {
    if (p < 0.0 || p > 1.0) throw std::runtime_error("p must be in [0,1]");
    Graph g(n);
    std::mt19937_64 rng(seed);
    std::bernoulli_distribution coin(p);

    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (coin(rng)) g.add_edge(i, j);

    return g;
}

Graph make_bipartite_random(int n_left, int n_right, double p, uint64_t seed) {
    if (p < 0.0 || p > 1.0) throw std::runtime_error("p must be in [0,1]");
    Graph g(n_left + n_right);
    std::mt19937_64 rng(seed);
    std::bernoulli_distribution coin(p);

    for (int i = 0; i < n_left; i++)
        for (int j = 0; j < n_right; j++)
            if (coin(rng)) g.add_edge(i, n_left + j);

    return g;
}
