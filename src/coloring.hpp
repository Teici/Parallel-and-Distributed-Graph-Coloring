#pragma once
#include "graph.hpp"
#include <vector>

struct ColoringResult {
    bool success = false;
    std::vector<int> color;
    long long nodes = 0;
    long long backtracks = 0;
    double seconds = 0.0;
};

bool verify_coloring(const Graph& g, const std::vector<int>& color, int k);

ColoringResult color_two_color_bipartite(const Graph& g);

ColoringResult color_greedy_dsatur(const Graph& g, int k);

ColoringResult color_serial_exact(const Graph& g, int k, double max_seconds = 0.0);
ColoringResult color_threads_exact(const Graph& g, int k, int threads, int split_depth, double max_seconds = 0.0);
