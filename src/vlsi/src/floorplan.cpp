#include <algorithm>
#include <bit>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>

#include "nexus/vlsi/physical.hpp"

namespace nexus::vlsi {

namespace {

// a realisable (w, h) of a subtree, with the child options it came from
struct Shape {
  double w, h;
  int left = -1, right = -1;  // option indices in the children
  bool rotated = false;       // leaves only
};

std::vector<Shape> pareto(std::vector<Shape> s) {
  std::sort(s.begin(), s.end(), [](const Shape& a, const Shape& b) { return a.w != b.w ? a.w < b.w : a.h < b.h; });
  std::vector<Shape> out;
  for (const auto& x : s) {
    if (out.empty() || x.h < out.back().h - 1e-12) {
      out.push_back(x);
    }
  }
  return out;
}

std::vector<Shape> combine(const std::vector<Shape>& a, const std::vector<Shape>& b, int op) {
  std::vector<Shape> out;
  for (std::size_t i = 0; i < a.size(); ++i) {
    for (std::size_t j = 0; j < b.size(); ++j) {
      Shape s{};
      if (op == kCutV) {
        s.w = a[i].w + b[j].w;
        s.h = std::max(a[i].h, b[j].h);
      } else {
        s.w = std::max(a[i].w, b[j].w);
        s.h = a[i].h + b[j].h;
      }
      s.left = static_cast<int>(i);
      s.right = static_cast<int>(j);
      out.push_back(s);
    }
  }
  return pareto(out);
}

struct TreeNode {
  int op = 0;      // block index for leaves (>= 0) or a cut
  int left = -1, right = -1;
  std::vector<Shape> shapes;
};

std::vector<TreeNode> build_tree(const std::vector<Block>& blocks, const Polish& e) {
  std::vector<TreeNode> nodes;
  std::vector<int> stack;
  for (const int t : e) {
    TreeNode n;
    n.op = t;
    if (t >= 0) {
      const Block& b = blocks.at(static_cast<std::size_t>(t));
      std::vector<Shape> s{Shape{b.w, b.h, -1, -1, false}};
      if (b.rotatable && std::abs(b.w - b.h) > 1e-12) {
        s.push_back(Shape{b.h, b.w, -1, -1, true});
      }
      n.shapes = pareto(s);
    } else {
      if (stack.size() < 2) {
        throw std::runtime_error("floorplan: invalid Polish expression");
      }
      n.right = stack.back();
      stack.pop_back();
      n.left = stack.back();
      stack.pop_back();
      n.shapes = combine(nodes[static_cast<std::size_t>(n.left)].shapes, nodes[static_cast<std::size_t>(n.right)].shapes, t);
    }
    nodes.push_back(n);
    stack.push_back(static_cast<int>(nodes.size()) - 1);
  }
  if (stack.size() != 1) {
    throw std::runtime_error("floorplan: invalid Polish expression");
  }
  return nodes;
}

}  // namespace

bool is_normalized_polish(const Polish& e, std::size_t blocks) {
  if (e.size() != 2 * blocks - 1) {
    return false;
  }
  std::vector<bool> seen(blocks, false);
  int operands = 0, operators = 0;
  for (std::size_t i = 0; i < e.size(); ++i) {
    if (e[i] >= 0) {
      if (static_cast<std::size_t>(e[i]) >= blocks || seen[static_cast<std::size_t>(e[i])]) {
        return false;
      }
      seen[static_cast<std::size_t>(e[i])] = true;
      ++operands;
    } else {
      ++operators;
      if (operators >= operands) {
        return false;  // balloting property
      }
      if (i > 0 && e[i - 1] == e[i]) {
        return false;  // normalised: no two identical consecutive operators
      }
    }
  }
  return operands == static_cast<int>(blocks);
}

FloorplanResult evaluate_floorplan(const std::vector<Block>& blocks, const Polish& e) {
  const auto nodes = build_tree(blocks, e);
  FloorplanResult r;
  r.expression = e;
  const auto& root = nodes.back();
  std::size_t best = 0;
  for (std::size_t i = 1; i < root.shapes.size(); ++i) {
    if (root.shapes[i].w * root.shapes[i].h < root.shapes[best].w * root.shapes[best].h) {
      best = i;
    }
  }
  r.width = root.shapes[best].w;
  r.height = root.shapes[best].h;
  r.area = r.width * r.height;
  r.x.assign(blocks.size(), 0.0);
  r.y.assign(blocks.size(), 0.0);
  r.w.assign(blocks.size(), 0.0);
  r.h.assign(blocks.size(), 0.0);
  std::function<void(std::size_t, std::size_t, double, double)> place = [&](std::size_t ni, std::size_t opt, double x,
                                                                             double y) {
    const TreeNode& n = nodes[ni];
    const Shape& s = n.shapes[opt];
    if (n.op >= 0) {
      const auto b = static_cast<std::size_t>(n.op);
      r.x[b] = x;
      r.y[b] = y;
      r.w[b] = s.w;
      r.h[b] = s.h;
      return;
    }
    const auto li = static_cast<std::size_t>(n.left), ri = static_cast<std::size_t>(n.right);
    const Shape& ls = nodes[li].shapes[static_cast<std::size_t>(s.left)];
    place(li, static_cast<std::size_t>(s.left), x, y);
    if (n.op == kCutV) {
      place(ri, static_cast<std::size_t>(s.right), x + ls.w, y);
    } else {
      place(ri, static_cast<std::size_t>(s.right), x, y + ls.h);
    }
  };
  place(nodes.size() - 1, best, 0.0, 0.0);
  return r;
}

std::string format_polish(const Polish& e, const std::vector<Block>& blocks) {
  std::string out;
  for (const int t : e) {
    if (!out.empty()) {
      out += ' ';
    }
    out += t == kCutV ? "V" : (t == kCutH ? "H" : blocks[static_cast<std::size_t>(t)].name);
  }
  return out;
}

FloorplanResult anneal_floorplan(const std::vector<Block>& blocks, std::uint32_t seed, std::size_t moves) {
  const std::size_t n = blocks.size();
  if (n == 1) {
    return evaluate_floorplan(blocks, {0});
  }
  // initial expression: 0 1 V 2 V 3 V ...
  Polish e{0};
  for (std::size_t i = 1; i < n; ++i) {
    e.push_back(static_cast<int>(i));
    e.push_back(kCutV);
  }
  std::mt19937 rng(seed);
  auto cost = [&](const Polish& x) { return evaluate_floorplan(blocks, x).area; };
  double cur = cost(e);
  Polish best = e;
  double best_cost = cur;
  auto neighbour = [&](const Polish& x) {
    Polish y = x;
    std::uniform_int_distribution<int> which(0, 2);
    std::uniform_int_distribution<std::size_t> pos(0, y.size() - 1);
    for (int attempt = 0; attempt < 50; ++attempt) {
      y = x;
      const int m = which(rng);
      if (m == 0) {  // M1: swap two adjacent operands
        std::vector<std::size_t> ops;
        for (std::size_t i = 0; i < y.size(); ++i) {
          if (y[i] >= 0) {
            ops.push_back(i);
          }
        }
        std::uniform_int_distribution<std::size_t> k(0, ops.size() - 2);
        const std::size_t i = k(rng);
        std::swap(y[ops[i]], y[ops[i + 1]]);
      } else if (m == 1) {  // M2: complement a chain of operators
        std::size_t i = pos(rng);
        while (i < y.size() && y[i] >= 0) {
          ++i;
        }
        if (i == y.size()) {
          continue;
        }
        for (; i < y.size() && y[i] < 0; ++i) {
          y[i] = y[i] == kCutV ? kCutH : kCutV;
        }
      } else {  // M3: swap an adjacent operand and operator
        const std::size_t i = pos(rng);
        if (i + 1 >= y.size() || (y[i] >= 0) == (y[i + 1] >= 0)) {
          continue;
        }
        std::swap(y[i], y[i + 1]);
      }
      if (is_normalized_polish(y, n)) {
        return y;
      }
    }
    return x;
  };
  // initial temperature from the average uphill move
  double uphill = 0.0;
  int count = 0;
  for (int i = 0; i < 100; ++i) {
    const double d = cost(neighbour(e)) - cur;
    if (d > 0) {
      uphill += d;
      ++count;
    }
  }
  double temp = count > 0 ? -(uphill / count) / std::log(0.9) : 1.0;
  std::uniform_real_distribution<double> u(0.0, 1.0);
  const std::size_t per_stage = std::max<std::size_t>(10 * n, 50);
  for (std::size_t k = 0; k < moves; ++k) {
    const Polish y = neighbour(e);
    const double c = cost(y);
    if (c <= cur || u(rng) < std::exp(-(c - cur) / temp)) {
      e = y;
      cur = c;
      if (cur < best_cost) {
        best_cost = cur;
        best = e;
      }
    }
    if ((k + 1) % per_stage == 0) {
      temp *= 0.92;
    }
  }
  return evaluate_floorplan(blocks, best);
}

FloorplanResult exhaustive_floorplan(const std::vector<Block>& blocks) {
  const std::size_t n = blocks.size();
  if (n > 8) {
    throw std::runtime_error("exhaustive_floorplan: at most 8 blocks");
  }
  // dynamic programming over subsets: the Pareto shapes of the best slicing structures of every subset, with an
  // expression that realises each shape
  struct Opt {
    double w, h;
    Polish expr;
  };
  std::vector<std::vector<Opt>> best(std::size_t{1} << n);
  auto prune = [](std::vector<Opt> v) {
    std::sort(v.begin(), v.end(), [](const Opt& a, const Opt& b) { return a.w != b.w ? a.w < b.w : a.h < b.h; });
    std::vector<Opt> out;
    for (auto& x : v) {
      if (out.empty() || x.h < out.back().h - 1e-12) {
        out.push_back(std::move(x));
      }
    }
    return out;
  };
  for (std::size_t mask = 1; mask < best.size(); ++mask) {
    std::vector<Opt> cand;
    if ((mask & (mask - 1)) == 0) {
      const auto b = static_cast<std::size_t>(std::countr_zero(mask));
      cand.push_back(Opt{blocks[b].w, blocks[b].h, {static_cast<int>(b)}});
      if (blocks[b].rotatable) {
        cand.push_back(Opt{blocks[b].h, blocks[b].w, {static_cast<int>(b)}});
      }
    } else {
      for (std::size_t left = (mask - 1) & mask; left > 0; left = (left - 1) & mask) {
        const std::size_t right = mask ^ left;
        for (const auto& a : best[left]) {
          for (const auto& b : best[right]) {
            for (const int op : {kCutV, kCutH}) {
              Opt o;
              o.w = op == kCutV ? a.w + b.w : std::max(a.w, b.w);
              o.h = op == kCutV ? std::max(a.h, b.h) : a.h + b.h;
              o.expr = a.expr;
              o.expr.insert(o.expr.end(), b.expr.begin(), b.expr.end());
              o.expr.push_back(op);
              cand.push_back(std::move(o));
            }
          }
        }
      }
    }
    best[mask] = prune(std::move(cand));
  }
  const auto& all = best.back();
  std::size_t pick = 0;
  for (std::size_t i = 1; i < all.size(); ++i) {
    if (all[i].w * all[i].h < all[pick].w * all[pick].h) {
      pick = i;
    }
  }
  FloorplanResult r = evaluate_floorplan(blocks, all[pick].expr);
  return r;
}

}  // namespace nexus::vlsi
