#pragma once
#include "graph.hpp"
#include <cstdint>

Graph make_complete(int n);
Graph make_cycle(int n);
Graph make_grid(int rows, int cols);
Graph make_random_gnp(int n, double p, uint64_t seed);
Graph make_bipartite_random(int n_left, int n_right, double p, uint64_t seed);
