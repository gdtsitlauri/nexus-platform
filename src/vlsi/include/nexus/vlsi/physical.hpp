#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "nexus/vlsi/netlist.hpp"

// Physical design algorithms (NEY705): partitioning, floorplanning, placement and routing.
namespace nexus::vlsi {

// ---------------------------------------------------------------- hypergraph of a design

struct Hypergraph {
  std::size_t num_cells = 0;
  std::vector<std::vector<std::size_t>> nets;  // each net lists the cells it connects (2 or more)
  std::vector<double> cell_area;               // default 1
  std::vector<std::vector<std::size_t>> cell_nets() const;
};

// Cells of a mapped netlist become hypergraph cells; every multi-terminal signal net becomes a hyperedge. Primary
// inputs/outputs are returned as pads: pad k connects to the cells listed in pads[k].
// `net_ids` (optional) receives the netlist net of every hyperedge.
Hypergraph to_hypergraph(const Netlist& n, std::vector<std::vector<std::size_t>>* pads = nullptr,
                         std::vector<std::size_t>* net_ids = nullptr);

// Random hypergraph with a planted two-way structure (for tests and demonstrations).
Hypergraph planted_hypergraph(std::size_t cells, std::size_t nets_inside, std::size_t nets_across,
                              std::uint32_t seed);

// ---------------------------------------------------------------- partitioning

std::size_t cut_size(const Hypergraph& h, const std::vector<int>& side);

// Kernighan-Lin: pair swaps on the clique-expanded graph, keeping both halves equal in size.
std::vector<int> kernighan_lin(const Hypergraph& h, std::vector<int> side, std::size_t* passes = nullptr);

// Fiduccia-Mattheyses: single-cell moves with gain buckets and a balance tolerance (fraction of total area).
std::vector<int> fiduccia_mattheyses(const Hypergraph& h, std::vector<int> side, double tolerance = 0.1,
                                     std::size_t* passes = nullptr);

std::vector<int> random_bisection(std::size_t cells, std::uint32_t seed);

// ---------------------------------------------------------------- slicing floorplans

struct Block {
  std::string name;
  double w = 1.0, h = 1.0;
  bool rotatable = true;
};

// Normalised Polish expression: operands are block indices, -1 = vertical cut 'V' (side by side), -2 = horizontal
// cut 'H' (stacked).
using Polish = std::vector<int>;
constexpr int kCutV = -1;
constexpr int kCutH = -2;

bool is_normalized_polish(const Polish& e, std::size_t blocks);
struct FloorplanResult {
  Polish expression;
  double width = 0.0, height = 0.0, area = 0.0;
  std::vector<double> x, y, w, h;  // placed blocks
};
// Minimum-area realisation of a slicing expression (Stockmeyer shape-function combination, rotations allowed).
FloorplanResult evaluate_floorplan(const std::vector<Block>& blocks, const Polish& e);
// Wong-Liu simulated annealing over normalised Polish expressions (moves M1, M2, M3).
FloorplanResult anneal_floorplan(const std::vector<Block>& blocks, std::uint32_t seed, std::size_t moves = 20000);
// Exhaustive minimum over all normalised Polish expressions (blocks <= 6; for checking the annealer).
FloorplanResult exhaustive_floorplan(const std::vector<Block>& blocks);
std::string format_polish(const Polish& e, const std::vector<Block>& blocks);

// ---------------------------------------------------------------- placement

struct Placement {
  std::vector<double> x, y;  // cell centres
};
struct PadPositions {
  std::vector<std::vector<std::size_t>> pads;  // pad -> cells
  std::vector<double> x, y;                    // pad positions
};
PadPositions pads_on_boundary(const std::vector<std::vector<std::size_t>>& pads, double width, double height);

// Half-perimeter wire length over nets and pad connections.
double hpwl(const Hypergraph& h, const Placement& p, const PadPositions* pads = nullptr);

// Quadratic (analytical) placement: minimise the sum of squared two-point distances with the clique net model;
// the linear systems are solved with conjugate gradients. Needs pads to anchor the solution.
// `cg_iterations` and `residual` (relative residual norm of the x and y solves, maximum) are optional outputs.
Placement quadratic_placement(const Hypergraph& h, const PadPositions& pads, std::size_t* cg_iterations = nullptr,
                              double* residual = nullptr);

// Legalisation onto a grid of rows x columns slots (one cell per slot), nearest free slot in order of x.
Placement legalize(const Placement& global, std::size_t rows, std::size_t cols);
bool is_legal(const Placement& p, std::size_t rows, std::size_t cols);

// Simulated-annealing detailed placement on the slot grid (pairwise swaps, HPWL cost).
Placement anneal_placement(const Hypergraph& h, Placement start, std::size_t rows, std::size_t cols,
                           const PadPositions* pads, std::uint32_t seed, std::size_t moves = 50000);

// Min-cut placement: recursive Fiduccia-Mattheyses bisection of the slot grid.
Placement mincut_placement(const Hypergraph& h, std::size_t rows, std::size_t cols, std::uint32_t seed);

Placement random_placement(std::size_t cells, std::size_t rows, std::size_t cols, std::uint32_t seed);

// ---------------------------------------------------------------- routing

struct Point {
  int x = 0, y = 0;
  bool operator==(const Point&) const = default;
};

// Two-layer routing grid: layer 0 runs horizontally, layer 1 vertically; vias cost `via_cost`.
struct RoutingGrid {
  int width = 0, height = 0;
  std::vector<std::uint8_t> blocked;  // per (layer, x, y)
  std::vector<int> usage;             // tracks used per (layer, x, y)
  int capacity = 1;
  double via_cost = 3.0;
  RoutingGrid(int w, int h);
  std::size_t index(int layer, int x, int y) const;
  void block(int x0, int y0, int x1, int y1);  // both layers, inclusive
};

struct RoutedPath {
  std::vector<std::array<int, 3>> cells;  // (layer, x, y)
  double cost = 0.0;
  std::size_t expanded = 0;
  bool found = false;
};

// Lee maze routing (breadth-first wave expansion; uniform cost per step, vias cost via_cost via Dijkstra).
RoutedPath lee_route(const RoutingGrid& g, Point s, Point t);
// A* with an admissible Manhattan-distance heuristic: same cost as Lee, fewer expansions.
RoutedPath astar_route(const RoutingGrid& g, Point s, Point t);

struct NetRequest {
  std::string name;
  std::vector<Point> pins;
};
struct RoutingResult {
  std::vector<std::vector<RoutedPath>> nets;  // per net: one path per pin after the first
  std::size_t overflow = 0;                   // grid cells used beyond capacity
  std::size_t iterations = 0;
  double wirelength = 0.0;
  std::size_t vias = 0;
};
// Multi-net routing with negotiated congestion (PathFinder): rip up and re-route every net with present and
// history congestion costs until no grid cell is over capacity.
RoutingResult negotiated_route(RoutingGrid g, const std::vector<NetRequest>& nets, std::size_t max_iterations = 30);

// Channel routing with the left-edge algorithm constrained by the vertical constraint graph (no doglegs).
struct ChannelRouting {
  std::vector<int> track_of_net;  // per net id (1-based nets; index 0 unused)
  int tracks = 0;
  int density = 0;                // maximum number of nets crossing a column: a lower bound on tracks
  bool cyclic = false;            // vertical constraints form a cycle (needs doglegs)
};
ChannelRouting left_edge_channel_route(const std::vector<int>& top, const std::vector<int>& bottom);

// Rectilinear Steiner trees: minimum spanning tree and the iterated 1-Steiner heuristic on the Hanan grid.
double rectilinear_mst_length(const std::vector<Point>& pts);
std::vector<Point> iterated_one_steiner(const std::vector<Point>& pts, double* length);
// Exact minimum rectilinear Steiner tree for up to 5 terminals (all Hanan-grid Steiner point subsets).
double exact_rsmt_length(const std::vector<Point>& pts);

}  // namespace nexus::vlsi
