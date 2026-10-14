#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>

#include "nexus/vlsi/physical.hpp"

namespace nexus::vlsi {

RoutingGrid::RoutingGrid(int w, int h)
    : width(w),
      height(h),
      blocked(static_cast<std::size_t>(2 * w * h), 0),
      usage(static_cast<std::size_t>(2 * w * h), 0) {}

std::size_t RoutingGrid::index(int layer, int x, int y) const {
  return static_cast<std::size_t>((layer * height + y) * width + x);
}

void RoutingGrid::block(int x0, int y0, int x1, int y1) {
  for (int l = 0; l < 2; ++l) {
    for (int y = y0; y <= y1; ++y) {
      for (int x = x0; x <= x1; ++x) {
        blocked[index(l, x, y)] = 1;
      }
    }
  }
}

namespace {

struct State {
  double f;
  double g;
  std::size_t idx;
  bool operator>(const State& o) const { return f > o.f; }
};

// Dijkstra / A* from a set of sources to any target cell; `cell_cost` is the cost of entering a cell.
RoutedPath search(const RoutingGrid& g, const std::vector<std::size_t>& sources,
                  const std::function<bool(std::size_t)>& is_target, const std::function<double(std::size_t)>& cell_cost,
                  const std::function<double(int, int)>& heuristic) {
  const std::size_t total = g.blocked.size();
  std::vector<double> dist(total, std::numeric_limits<double>::infinity());
  std::vector<std::size_t> parent(total, total);
  std::priority_queue<State, std::vector<State>, std::greater<State>> pq;
  auto coords = [&](std::size_t i, int& l, int& x, int& y) {
    const auto plane = static_cast<std::size_t>(g.width * g.height);
    l = static_cast<int>(i / plane);
    const auto r = i % plane;
    y = static_cast<int>(r / static_cast<std::size_t>(g.width));
    x = static_cast<int>(r % static_cast<std::size_t>(g.width));
  };
  for (const auto s : sources) {
    if (g.blocked[s] != 0) {
      continue;
    }
    dist[s] = 0.0;
    int l, x, y;
    coords(s, l, x, y);
    pq.push(State{heuristic(x, y), 0.0, s});
  }
  RoutedPath path;
  while (!pq.empty()) {
    const State st = pq.top();
    pq.pop();
    if (st.g > dist[st.idx] + 1e-12) {
      continue;
    }
    ++path.expanded;
    if (is_target(st.idx)) {
      path.found = true;
      path.cost = st.g;
      for (std::size_t i = st.idx; i != total; i = parent[i]) {
        int l, x, y;
        coords(i, l, x, y);
        path.cells.push_back({l, x, y});
        if (dist[i] == 0.0 && std::find(sources.begin(), sources.end(), i) != sources.end()) {
          break;
        }
      }
      std::reverse(path.cells.begin(), path.cells.end());
      return path;
    }
    int l, x, y;
    coords(st.idx, l, x, y);
    // layer 0 moves horizontally, layer 1 vertically, a via switches layers
    std::vector<std::pair<std::size_t, double>> next;
    if (l == 0) {
      if (x > 0) next.emplace_back(g.index(0, x - 1, y), 1.0);
      if (x + 1 < g.width) next.emplace_back(g.index(0, x + 1, y), 1.0);
    } else {
      if (y > 0) next.emplace_back(g.index(1, x, y - 1), 1.0);
      if (y + 1 < g.height) next.emplace_back(g.index(1, x, y + 1), 1.0);
    }
    next.emplace_back(g.index(1 - l, x, y), g.via_cost);
    for (const auto& [ni, step] : next) {
      if (g.blocked[ni] != 0) {
        continue;
      }
      const double nd = st.g + step * cell_cost(ni);
      if (nd < dist[ni] - 1e-12) {
        dist[ni] = nd;
        parent[ni] = st.idx;
        int nl, nx, ny;
        coords(ni, nl, nx, ny);
        pq.push(State{nd + heuristic(nx, ny), nd, ni});
      }
    }
  }
  return path;
}

}  // namespace

RoutedPath lee_route(const RoutingGrid& g, Point s, Point t) {
  return search(
      g, {g.index(0, s.x, s.y), g.index(1, s.x, s.y)},
      [&](std::size_t i) { return i == g.index(0, t.x, t.y) || i == g.index(1, t.x, t.y); },
      [](std::size_t) { return 1.0; }, [](int, int) { return 0.0; });
}

RoutedPath astar_route(const RoutingGrid& g, Point s, Point t) {
  return search(
      g, {g.index(0, s.x, s.y), g.index(1, s.x, s.y)},
      [&](std::size_t i) { return i == g.index(0, t.x, t.y) || i == g.index(1, t.x, t.y); },
      [](std::size_t) { return 1.0; },
      [&](int x, int y) { return static_cast<double>(std::abs(x - t.x) + std::abs(y - t.y)); });
}

RoutingResult negotiated_route(RoutingGrid g, const std::vector<NetRequest>& nets, std::size_t max_iterations) {
  RoutingResult result;
  std::vector<double> history(g.blocked.size(), 0.0);
  double pres_fac = 0.5;
  std::vector<std::vector<std::size_t>> used_by(nets.size());  // grid cells occupied by each net
  result.nets.assign(nets.size(), {});
  std::fill(g.usage.begin(), g.usage.end(), 0);
  for (std::size_t it = 0; it < max_iterations; ++it) {
    result.iterations = it + 1;
    for (std::size_t n = 0; n < nets.size(); ++n) {
      // rip up
      for (const auto c : used_by[n]) {
        --g.usage[c];
      }
      used_by[n].clear();
      result.nets[n].clear();
      const auto& pins = nets[n].pins;
      if (pins.size() < 2) {
        continue;
      }
      std::set<std::size_t> tree{g.index(0, pins[0].x, pins[0].y), g.index(1, pins[0].x, pins[0].y)};
      auto cost = [&](std::size_t c) {
        if (tree.count(c) != 0) {
          return 0.0;
        }
        const double over = std::max(0, g.usage[c] + 1 - g.capacity);
        return (1.0 + history[c]) * (1.0 + pres_fac * over);
      };
      for (std::size_t p = 1; p < pins.size(); ++p) {
        const std::size_t t0 = g.index(0, pins[p].x, pins[p].y), t1 = g.index(1, pins[p].x, pins[p].y);
        const auto path = search(
            g, std::vector<std::size_t>(tree.begin(), tree.end()), [&](std::size_t i) { return i == t0 || i == t1; },
            cost, [](int, int) { return 0.0; });
        if (!path.found) {
          throw std::runtime_error("negotiated_route: net " + nets[n].name + " cannot be routed");
        }
        for (const auto& c : path.cells) {
          tree.insert(g.index(c[0], c[1], c[2]));
        }
        result.nets[n].push_back(path);
      }
      for (const auto c : tree) {
        ++g.usage[c];
        used_by[n].push_back(c);
      }
    }
    std::size_t overflow = 0;
    for (std::size_t c = 0; c < g.usage.size(); ++c) {
      if (g.usage[c] > g.capacity) {
        overflow += static_cast<std::size_t>(g.usage[c] - g.capacity);
        history[c] += 1.0;
      }
    }
    result.overflow = overflow;
    if (overflow == 0) {
      break;
    }
    pres_fac *= 1.6;
  }
  result.wirelength = 0.0;
  result.vias = 0;
  for (const auto& np : result.nets) {
    for (const auto& path : np) {
      for (std::size_t i = 1; i < path.cells.size(); ++i) {
        if (path.cells[i][0] != path.cells[i - 1][0]) {
          ++result.vias;
        } else {
          result.wirelength += 1.0;
        }
      }
    }
  }
  return result;
}

ChannelRouting left_edge_channel_route(const std::vector<int>& top, const std::vector<int>& bottom) {
  if (top.size() != bottom.size()) {
    throw std::runtime_error("channel: top and bottom must have the same length");
  }
  int max_net = 0;
  for (const int v : top) max_net = std::max(max_net, v);
  for (const int v : bottom) max_net = std::max(max_net, v);
  const auto nn = static_cast<std::size_t>(max_net) + 1;
  std::vector<int> left(nn, std::numeric_limits<int>::max()), right(nn, -1);
  for (std::size_t c = 0; c < top.size(); ++c) {
    for (const int v : {top[c], bottom[c]}) {
      if (v > 0) {
        left[static_cast<std::size_t>(v)] = std::min(left[static_cast<std::size_t>(v)], static_cast<int>(c));
        right[static_cast<std::size_t>(v)] = std::max(right[static_cast<std::size_t>(v)], static_cast<int>(c));
      }
    }
  }
  ChannelRouting r;
  r.track_of_net.assign(nn, -1);
  // density
  for (std::size_t c = 0; c < top.size(); ++c) {
    int d = 0;
    for (std::size_t v = 1; v < nn; ++v) {
      if (right[v] >= 0 && left[v] <= static_cast<int>(c) && static_cast<int>(c) <= right[v]) {
        ++d;
      }
    }
    r.density = std::max(r.density, d);
  }
  // vertical constraint graph: the net on top of a column must be above the net at its bottom
  std::vector<std::set<std::size_t>> above(nn);  // above[b] = nets that must be above b
  for (std::size_t c = 0; c < top.size(); ++c) {
    if (top[c] > 0 && bottom[c] > 0 && top[c] != bottom[c]) {
      above[static_cast<std::size_t>(bottom[c])].insert(static_cast<std::size_t>(top[c]));
    }
  }
  std::vector<std::size_t> pending;
  for (std::size_t v = 1; v < nn; ++v) {
    if (right[v] >= 0) {
      pending.push_back(v);
    }
  }
  std::sort(pending.begin(), pending.end(), [&](std::size_t a, std::size_t b) { return left[a] < left[b]; });
  int track = 0;
  while (!pending.empty()) {
    int last_right = -1;
    std::vector<std::size_t> placed;
    for (const auto v : pending) {
      const bool free_of_constraints = std::all_of(above[v].begin(), above[v].end(),
                                                   [&](std::size_t u) { return r.track_of_net[u] >= 0; });
      if (free_of_constraints && left[v] > last_right) {
        placed.push_back(v);
        last_right = right[v];
      }
    }
    if (placed.empty()) {
      r.cyclic = true;  // the remaining nets constrain each other in a cycle
      break;
    }
    for (const auto v : placed) {
      r.track_of_net[v] = track;
      pending.erase(std::find(pending.begin(), pending.end(), v));
    }
    ++track;
  }
  r.tracks = track;
  return r;
}

namespace {

int manhattan(const Point& a, const Point& b) { return std::abs(a.x - b.x) + std::abs(a.y - b.y); }

}  // namespace

double rectilinear_mst_length(const std::vector<Point>& pts) {
  if (pts.size() < 2) {
    return 0.0;
  }
  std::vector<bool> in(pts.size(), false);
  std::vector<int> best(pts.size(), std::numeric_limits<int>::max());
  best[0] = 0;
  double total = 0.0;
  for (std::size_t k = 0; k < pts.size(); ++k) {
    std::size_t u = pts.size();
    for (std::size_t i = 0; i < pts.size(); ++i) {
      if (!in[i] && (u == pts.size() || best[i] < best[u])) {
        u = i;
      }
    }
    in[u] = true;
    total += best[u];
    for (std::size_t i = 0; i < pts.size(); ++i) {
      if (!in[i]) {
        best[i] = std::min(best[i], manhattan(pts[u], pts[i]));
      }
    }
  }
  return total;
}

std::vector<Point> iterated_one_steiner(const std::vector<Point>& pts, double* length) {
  std::vector<Point> current = pts;
  std::set<std::pair<int, int>> hanan;
  for (const auto& a : pts) {
    for (const auto& b : pts) {
      hanan.insert({a.x, b.y});
    }
  }
  double cur = rectilinear_mst_length(current);
  for (;;) {
    double best_gain = 0.0;
    Point best_point;
    for (const auto& [x, y] : hanan) {
      const Point c{x, y};
      if (std::find(current.begin(), current.end(), c) != current.end()) {
        continue;
      }
      auto with = current;
      with.push_back(c);
      const double gain = cur - rectilinear_mst_length(with);
      if (gain > best_gain + 1e-9) {
        best_gain = gain;
        best_point = c;
      }
    }
    if (best_gain <= 0.0) {
      break;
    }
    current.push_back(best_point);
    cur -= best_gain;
  }
  if (length != nullptr) {
    *length = cur;
  }
  return current;
}

double exact_rsmt_length(const std::vector<Point>& pts) {
  if (pts.size() > 5) {
    throw std::runtime_error("exact_rsmt_length: at most 5 terminals");
  }
  std::vector<Point> hanan;
  for (const auto& a : pts) {
    for (const auto& b : pts) {
      const Point c{a.x, b.y};
      if (std::find(hanan.begin(), hanan.end(), c) == hanan.end() && std::find(pts.begin(), pts.end(), c) == pts.end()) {
        hanan.push_back(c);
      }
    }
  }
  double best = rectilinear_mst_length(pts);
  const std::size_t max_steiner = pts.size() >= 2 ? pts.size() - 2 : 0;
  std::vector<std::size_t> pick;
  std::function<void(std::size_t)> rec = [&](std::size_t from) {
    if (!pick.empty()) {
      auto all = pts;
      for (const auto i : pick) {
        all.push_back(hanan[i]);
      }
      best = std::min(best, rectilinear_mst_length(all));
    }
    if (pick.size() == max_steiner) {
      return;
    }
    for (std::size_t i = from; i < hanan.size(); ++i) {
      pick.push_back(i);
      rec(i + 1);
      pick.pop_back();
    }
  };
  rec(0);
  return best;
}

}  // namespace nexus::vlsi
