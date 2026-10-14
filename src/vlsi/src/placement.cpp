#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>
#include <random>
#include <stdexcept>

#include "nexus/vlsi/physical.hpp"

namespace nexus::vlsi {

PadPositions pads_on_boundary(const std::vector<std::vector<std::size_t>>& pads, double width, double height) {
  PadPositions p;
  p.pads = pads;
  const double perimeter = 2.0 * (width + height);
  for (std::size_t k = 0; k < pads.size(); ++k) {
    double t = perimeter * (static_cast<double>(k) + 0.5) / static_cast<double>(pads.size());
    double x = 0.0, y = 0.0;
    if (t < width) {
      x = t;
      y = 0.0;
    } else if ((t -= width) < height) {
      x = width;
      y = t;
    } else if ((t -= height) < width) {
      x = width - t;
      y = height;
    } else {
      t -= width;
      x = 0.0;
      y = height - t;
    }
    p.x.push_back(x);
    p.y.push_back(y);
  }
  return p;
}

double hpwl(const Hypergraph& h, const Placement& p, const PadPositions* pads) {
  double total = 0.0;
  for (const auto& net : h.nets) {
    double x0 = 1e300, x1 = -1e300, y0 = 1e300, y1 = -1e300;
    for (const auto c : net) {
      x0 = std::min(x0, p.x[c]);
      x1 = std::max(x1, p.x[c]);
      y0 = std::min(y0, p.y[c]);
      y1 = std::max(y1, p.y[c]);
    }
    total += (x1 - x0) + (y1 - y0);
  }
  if (pads != nullptr) {
    for (std::size_t k = 0; k < pads->pads.size(); ++k) {
      if (pads->pads[k].empty()) {
        continue;
      }
      double x0 = pads->x[k], x1 = pads->x[k], y0 = pads->y[k], y1 = pads->y[k];
      for (const auto c : pads->pads[k]) {
        x0 = std::min(x0, p.x[c]);
        x1 = std::max(x1, p.x[c]);
        y0 = std::min(y0, p.y[c]);
        y1 = std::max(y1, p.y[c]);
      }
      total += (x1 - x0) + (y1 - y0);
    }
  }
  return total;
}

namespace {

struct SparseMatrix {
  std::vector<std::vector<std::pair<std::size_t, double>>> rows;
  void add(std::size_t i, std::size_t j, double v) {
    for (auto& [col, val] : rows[i]) {
      if (col == j) {
        val += v;
        return;
      }
    }
    rows[i].emplace_back(j, v);
  }
  std::vector<double> mul(const std::vector<double>& x) const {
    std::vector<double> y(rows.size(), 0.0);
    for (std::size_t i = 0; i < rows.size(); ++i) {
      for (const auto& [j, v] : rows[i]) {
        y[i] += v * x[j];
      }
    }
    return y;
  }
};

double dot(const std::vector<double>& a, const std::vector<double>& b) {
  return std::inner_product(a.begin(), a.end(), b.begin(), 0.0);
}

// conjugate gradients for a symmetric positive definite system
std::vector<double> conjugate_gradient(const SparseMatrix& a, const std::vector<double>& b, std::size_t& iters,
                                       double& rel_residual) {
  std::vector<double> x(b.size(), 0.0), r = b, p = r;
  double rr = dot(r, r);
  const double bnorm = std::sqrt(std::max(dot(b, b), 1e-300));
  iters = 0;
  for (; iters < 10 * b.size() + 10 && std::sqrt(rr) > 1e-10 * bnorm; ++iters) {
    const auto ap = a.mul(p);
    const double alpha = rr / dot(p, ap);
    for (std::size_t i = 0; i < x.size(); ++i) {
      x[i] += alpha * p[i];
      r[i] -= alpha * ap[i];
    }
    const double rr_new = dot(r, r);
    const double beta = rr_new / rr;
    for (std::size_t i = 0; i < p.size(); ++i) {
      p[i] = r[i] + beta * p[i];
    }
    rr = rr_new;
  }
  // true residual
  const auto ax = a.mul(x);
  double res = 0.0;
  for (std::size_t i = 0; i < b.size(); ++i) {
    res += (b[i] - ax[i]) * (b[i] - ax[i]);
  }
  rel_residual = std::sqrt(res) / bnorm;
  return x;
}

}  // namespace

Placement quadratic_placement(const Hypergraph& h, const PadPositions& pads, std::size_t* cg_iterations,
                              double* residual) {
  const std::size_t n = h.num_cells;
  SparseMatrix a;
  a.rows.resize(n);
  std::vector<double> bx(n, 0.0), by(n, 0.0);
  // clique model: a net with k pins (cells and pads) adds weight 1/(k-1) between every pair
  auto connect_cells = [&](const std::vector<std::size_t>& cells, std::size_t extra_pins, double px, double py) {
    const std::size_t k = cells.size() + extra_pins;
    if (k < 2) {
      return;
    }
    const double w = 1.0 / static_cast<double>(k - 1);
    for (std::size_t i = 0; i < cells.size(); ++i) {
      for (std::size_t j = i + 1; j < cells.size(); ++j) {
        a.add(cells[i], cells[i], w);
        a.add(cells[j], cells[j], w);
        a.add(cells[i], cells[j], -w);
        a.add(cells[j], cells[i], -w);
      }
      if (extra_pins > 0) {  // a fixed pad contributes to the diagonal and the right-hand side
        a.add(cells[i], cells[i], w);
        bx[cells[i]] += w * px;
        by[cells[i]] += w * py;
      }
    }
  };
  for (const auto& net : h.nets) {
    connect_cells(net, 0, 0.0, 0.0);
  }
  for (std::size_t k = 0; k < pads.pads.size(); ++k) {
    connect_cells(pads.pads[k], 1, pads.x[k], pads.y[k]);
  }
  // a tiny pull to the centre keeps disconnected cells well defined
  double cx = 0.0, cy = 0.0;
  for (std::size_t k = 0; k < pads.x.size(); ++k) {
    cx += pads.x[k];
    cy += pads.y[k];
  }
  if (!pads.x.empty()) {
    cx /= static_cast<double>(pads.x.size());
    cy /= static_cast<double>(pads.y.size());
  }
  for (std::size_t i = 0; i < n; ++i) {
    a.add(i, i, 1e-6);
    bx[i] += 1e-6 * cx;
    by[i] += 1e-6 * cy;
  }
  std::size_t ix = 0, iy = 0;
  double rx = 0.0, ry = 0.0;
  Placement p;
  p.x = conjugate_gradient(a, bx, ix, rx);
  p.y = conjugate_gradient(a, by, iy, ry);
  if (cg_iterations != nullptr) {
    *cg_iterations = ix + iy;
  }
  if (residual != nullptr) {
    *residual = std::max(rx, ry);
  }
  return p;
}

Placement legalize(const Placement& global, std::size_t rows, std::size_t cols) {
  const std::size_t n = global.x.size();
  if (n > rows * cols) {
    throw std::runtime_error("legalize: more cells than slots");
  }
  std::vector<std::size_t> order(n);
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return global.x[a] < global.x[b]; });
  // scale the global placement into the slot region
  double x0 = 1e300, x1 = -1e300, y0 = 1e300, y1 = -1e300;
  for (std::size_t i = 0; i < n; ++i) {
    x0 = std::min(x0, global.x[i]);
    x1 = std::max(x1, global.x[i]);
    y0 = std::min(y0, global.y[i]);
    y1 = std::max(y1, global.y[i]);
  }
  auto sx = [&](double x) { return x1 > x0 ? (x - x0) / (x1 - x0) * static_cast<double>(cols) : static_cast<double>(cols) / 2.0; };
  auto sy = [&](double y) { return y1 > y0 ? (y - y0) / (y1 - y0) * static_cast<double>(rows) : static_cast<double>(rows) / 2.0; };
  std::vector<bool> used(rows * cols, false);
  Placement p;
  p.x.assign(n, 0.0);
  p.y.assign(n, 0.0);
  for (const auto c : order) {
    const double tx = sx(global.x[c]), ty = sy(global.y[c]);
    double best = 1e300;
    std::size_t slot = 0;
    for (std::size_t s = 0; s < rows * cols; ++s) {
      if (used[s]) {
        continue;
      }
      const double dx = static_cast<double>(s % cols) + 0.5 - tx;
      const double dy = static_cast<double>(s / cols) + 0.5 - ty;
      const double d = dx * dx + dy * dy;
      if (d < best) {
        best = d;
        slot = s;
      }
    }
    used[slot] = true;
    p.x[c] = static_cast<double>(slot % cols) + 0.5;
    p.y[c] = static_cast<double>(slot / cols) + 0.5;
  }
  return p;
}

bool is_legal(const Placement& p, std::size_t rows, std::size_t cols) {
  std::vector<bool> used(rows * cols, false);
  for (std::size_t i = 0; i < p.x.size(); ++i) {
    const double fx = p.x[i] - 0.5, fy = p.y[i] - 0.5;
    if (fx < 0 || fy < 0 || std::abs(fx - std::round(fx)) > 1e-9 || std::abs(fy - std::round(fy)) > 1e-9) {
      return false;
    }
    const auto cx = static_cast<std::size_t>(std::llround(fx));
    const auto cy = static_cast<std::size_t>(std::llround(fy));
    if (cx >= cols || cy >= rows || used[cy * cols + cx]) {
      return false;
    }
    used[cy * cols + cx] = true;
  }
  return true;
}

Placement random_placement(std::size_t cells, std::size_t rows, std::size_t cols, std::uint32_t seed) {
  std::vector<std::size_t> slots(rows * cols);
  std::iota(slots.begin(), slots.end(), 0);
  std::mt19937 rng(seed);
  std::shuffle(slots.begin(), slots.end(), rng);
  Placement p;
  for (std::size_t i = 0; i < cells; ++i) {
    p.x.push_back(static_cast<double>(slots[i] % cols) + 0.5);
    p.y.push_back(static_cast<double>(slots[i] / cols) + 0.5);
  }
  return p;
}

namespace {

double net_hpwl(const std::vector<std::size_t>& net, const Placement& p, const double* pad_x, const double* pad_y) {
  double x0 = 1e300, x1 = -1e300, y0 = 1e300, y1 = -1e300;
  if (pad_x != nullptr) {
    x0 = x1 = *pad_x;
    y0 = y1 = *pad_y;
  }
  for (const auto c : net) {
    x0 = std::min(x0, p.x[c]);
    x1 = std::max(x1, p.x[c]);
    y0 = std::min(y0, p.y[c]);
    y1 = std::max(y1, p.y[c]);
  }
  return (x1 - x0) + (y1 - y0);
}

}  // namespace

Placement anneal_placement(const Hypergraph& h, Placement p, std::size_t rows, std::size_t cols,
                           const PadPositions* pads, std::uint32_t seed, std::size_t moves) {
  const std::size_t n = h.num_cells;
  // nets touching each cell, including pad nets (encoded after the signal nets)
  std::vector<std::vector<std::size_t>> touch(n);
  for (std::size_t e = 0; e < h.nets.size(); ++e) {
    for (const auto c : h.nets[e]) {
      touch[c].push_back(e);
    }
  }
  const std::size_t pad_base = h.nets.size();
  if (pads != nullptr) {
    for (std::size_t k = 0; k < pads->pads.size(); ++k) {
      for (const auto c : pads->pads[k]) {
        touch[c].push_back(pad_base + k);
      }
    }
  }
  auto cost_of = [&](std::size_t e) {
    if (e < pad_base) {
      return net_hpwl(h.nets[e], p, nullptr, nullptr);
    }
    const std::size_t k = e - pad_base;
    return net_hpwl(pads->pads[k], p, &pads->x[k], &pads->y[k]);
  };
  // slot occupancy
  std::vector<long> slot(rows * cols, -1);
  for (std::size_t i = 0; i < n; ++i) {
    const auto s = static_cast<std::size_t>(p.y[i] - 0.5) * cols + static_cast<std::size_t>(p.x[i] - 0.5);
    slot[s] = static_cast<long>(i);
  }
  std::mt19937 rng(seed);
  std::uniform_int_distribution<std::size_t> pick_cell(0, n - 1);
  std::uniform_int_distribution<std::size_t> pick_slot(0, rows * cols - 1);
  std::uniform_real_distribution<double> u(0.0, 1.0);
  double temp = 2.0;
  const std::size_t per_stage = std::max<std::size_t>(n * 4, 100);
  for (std::size_t m = 0; m < moves; ++m) {
    const std::size_t a = pick_cell(rng);
    const std::size_t s = pick_slot(rng);
    const long other = slot[s];
    const auto sa = static_cast<std::size_t>(p.y[a] - 0.5) * cols + static_cast<std::size_t>(p.x[a] - 0.5);
    if (sa == s) {
      continue;
    }
    std::vector<std::size_t> nets = touch[a];
    if (other >= 0) {
      nets.insert(nets.end(), touch[static_cast<std::size_t>(other)].begin(), touch[static_cast<std::size_t>(other)].end());
    }
    std::sort(nets.begin(), nets.end());
    nets.erase(std::unique(nets.begin(), nets.end()), nets.end());
    double before = 0.0;
    for (const auto e : nets) {
      before += cost_of(e);
    }
    auto move_to = [&](std::size_t cell, std::size_t sl) {
      p.x[cell] = static_cast<double>(sl % cols) + 0.5;
      p.y[cell] = static_cast<double>(sl / cols) + 0.5;
    };
    move_to(a, s);
    if (other >= 0) {
      move_to(static_cast<std::size_t>(other), sa);
    }
    double after = 0.0;
    for (const auto e : nets) {
      after += cost_of(e);
    }
    const double delta = after - before;
    if (delta <= 0 || u(rng) < std::exp(-delta / temp)) {
      slot[s] = static_cast<long>(a);
      slot[sa] = other;
    } else {
      move_to(a, sa);  // undo
      if (other >= 0) {
        move_to(static_cast<std::size_t>(other), s);
      }
    }
    if ((m + 1) % per_stage == 0) {
      temp *= 0.95;
    }
  }
  return p;
}

Placement mincut_placement(const Hypergraph& h, std::size_t rows, std::size_t cols, std::uint32_t seed) {
  Placement p;
  p.x.assign(h.num_cells, 0.0);
  p.y.assign(h.num_cells, 0.0);
  std::function<void(std::vector<std::size_t>, std::size_t, std::size_t, std::size_t, std::size_t, bool)> split =
      [&](std::vector<std::size_t> cells, std::size_t r0, std::size_t r1, std::size_t c0, std::size_t c1,
          bool vertical) {
        const std::size_t slots = (r1 - r0) * (c1 - c0);
        if (cells.empty()) {
          return;
        }
        if (slots == 1 || cells.size() == 1) {
          // place in row-major order inside the region
          std::size_t k = 0;
          for (std::size_t r = r0; r < r1 && k < cells.size(); ++r) {
            for (std::size_t c = c0; c < c1 && k < cells.size(); ++c, ++k) {
              p.x[cells[k]] = static_cast<double>(c) + 0.5;
              p.y[cells[k]] = static_cast<double>(r) + 0.5;
            }
          }
          return;
        }
        // cut the longer side if the requested direction cannot be cut
        if (vertical && c1 - c0 < 2) {
          vertical = false;
        }
        if (!vertical && r1 - r0 < 2) {
          vertical = true;
        }
        const std::size_t r_mid = (r0 + r1) / 2, c_mid = (c0 + c1) / 2;
        const std::size_t slots_a = vertical ? (r1 - r0) * (c_mid - c0) : (r_mid - r0) * (c1 - c0);
        // sub-hypergraph of these cells
        std::vector<long> local(h.num_cells, -1);
        for (std::size_t i = 0; i < cells.size(); ++i) {
          local[cells[i]] = static_cast<long>(i);
        }
        Hypergraph sub;
        sub.num_cells = cells.size();
        sub.cell_area.assign(cells.size(), 1.0);
        for (const auto& net : h.nets) {
          std::vector<std::size_t> part;
          for (const auto c : net) {
            if (local[c] >= 0) {
              part.push_back(static_cast<std::size_t>(local[c]));
            }
          }
          if (part.size() >= 2) {
            sub.nets.push_back(part);
          }
        }
        // initial split with exactly the right sizes, then FM with a tight balance
        const std::size_t want_a = std::min(cells.size(), std::max<std::size_t>(
                                                              1, cells.size() * slots_a / slots));
        std::vector<int> side(cells.size(), 1);
        std::vector<std::size_t> idx(cells.size());
        std::iota(idx.begin(), idx.end(), 0);
        std::mt19937 rng(seed + static_cast<std::uint32_t>(r0 * 131 + c0 * 7 + cells.size()));
        std::shuffle(idx.begin(), idx.end(), rng);
        for (std::size_t i = 0; i < want_a; ++i) {
          side[idx[i]] = 0;
        }
        if (cells.size() > 2) {
          side = fiduccia_mattheyses(sub, side, 0.5 / static_cast<double>(cells.size()));
        }
        std::vector<std::size_t> a, b;
        for (std::size_t i = 0; i < cells.size(); ++i) {
          (side[i] == 0 ? a : b).push_back(cells[i]);
        }
        // keep each part within its region's capacity
        const std::size_t slots_b = slots - slots_a;
        while (a.size() > slots_a) {
          b.push_back(a.back());
          a.pop_back();
        }
        while (b.size() > slots_b) {
          a.push_back(b.back());
          b.pop_back();
        }
        if (vertical) {
          split(a, r0, r1, c0, c_mid, !vertical);
          split(b, r0, r1, c_mid, c1, !vertical);
        } else {
          split(a, r0, r_mid, c0, c1, !vertical);
          split(b, r_mid, r1, c0, c1, !vertical);
        }
      };
  std::vector<std::size_t> all(h.num_cells);
  std::iota(all.begin(), all.end(), 0);
  if (h.num_cells > rows * cols) {
    throw std::runtime_error("mincut_placement: more cells than slots");
  }
  split(all, 0, rows, 0, cols, true);
  return p;
}

}  // namespace nexus::vlsi
