#pragma once
#include "graph.hpp"
#include <string>

Graph read_graph_edge_list(const std::string& path, bool one_based = false);
void write_graph_edge_list(const std::string& path, const Graph& g, bool one_based = false);
