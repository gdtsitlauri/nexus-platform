#include "nexus/compiler/polyhedral/polyhedral.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <limits>
#include <numeric>
#include <set>
#include <sstream>
#include <tuple>

namespace nexus::compiler::polyhedral {

struct Expression {
  enum class Kind { Constant, Index, Read, Add, Sub, Mul, Neg } kind = Kind::Constant;
  std::int64_t value = 0;     // constant / index position / read position
  std::shared_ptr<Expression> lhs;
  std::shared_ptr<Expression> rhs;
};

namespace {

constexpr std::size_t kEnumerationLimit = 2000;

std::int64_t floor_div(std::int64_t a, std::int64_t b) {
  const std::int64_t q = a / b;
  return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

std::int64_t ceil_div(std::int64_t a, std::int64_t b) {
  return -floor_div(-a, b);
}

std::int64_t eval_affine(const Affine& affine, const std::vector<std::int64_t>& point) {
  std::int64_t value = affine.back();
  for (std::size_t index = 0; index + 1 < affine.size(); ++index) {
    value += affine[index] * point[index];
  }
  return value;
}

// ---- parsing ----------------------------------------------------------------------------------
class NestParser {
 public:
  explicit NestParser(const std::string& text) {
    for (std::size_t position = 0; position < text.size();) {
      const char ch = text[position];
      if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
        ++position;
      } else if (ch == '#') {
        while (position < text.size() && text[position] != '\n') {
          ++position;
        }
      } else if (std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_') {
        std::size_t end = position;
        while (end < text.size() && (std::isalnum(static_cast<unsigned char>(text[end])) != 0 || text[end] == '_')) {
          ++end;
        }
        tokens_.push_back(text.substr(position, end - position));
        position = end;
      } else {
        tokens_.emplace_back(1, ch);
        ++position;
      }
    }
  }

  LoopNest parse() {
    if (accept("params")) {
      do {
        const std::string name = identifier();
        expect("=");
        nest_.params[name] = integer();
      } while (accept(","));
    }
    loop();
    if (position_ != tokens_.size()) {
      throw std::string("unexpected '" + tokens_[position_] + "' after the loop nest");
    }
    const std::size_t width = nest_.indices.size() + 1;
    for (auto* bounds : {&nest_.lower, &nest_.upper}) {
      for (auto& bound : *bounds) {
        bound.insert(bound.end() - 1, width - bound.size(), 0);
      }
    }
    return nest_;
  }

 private:
  [[nodiscard]] bool peek(const std::string& token) const {
    return position_ < tokens_.size() && tokens_[position_] == token;
  }

  bool accept(const std::string& token) {
    if (peek(token)) {
      ++position_;
      return true;
    }
    return false;
  }

  void expect(const std::string& token) {
    if (!accept(token)) {
      throw std::string("expected '" + token + "'" + (position_ < tokens_.size() ? " near '" + tokens_[position_] + "'" : ""));
    }
  }

  std::string identifier() {
    if (position_ >= tokens_.size() || std::isalpha(static_cast<unsigned char>(tokens_[position_][0])) == 0) {
      throw std::string("expected an identifier");
    }
    return tokens_[position_++];
  }

  std::int64_t integer() {
    const bool negative = accept("-");
    if (position_ >= tokens_.size() || std::isdigit(static_cast<unsigned char>(tokens_[position_][0])) == 0) {
      throw std::string("expected an integer");
    }
    const std::int64_t value = std::stoll(tokens_[position_++]);
    return negative ? -value : value;
  }

  void loop() {
    expect("for");
    const std::string index = identifier();
    if (std::find(nest_.indices.begin(), nest_.indices.end(), index) != nest_.indices.end()) {
      throw std::string("loop index '" + index + "' reused");
    }
    expect("=");
    Affine lower = affine();
    expect("to");
    Affine upper = affine();
    nest_.indices.push_back(index);
    nest_.lower.push_back(std::move(lower));
    nest_.upper.push_back(std::move(upper));
    expect("{");
    if (peek("for")) {
      loop();
      expect("}");
      return;
    }
    while (!accept("}")) {
      if (peek("for")) {
        throw std::string("only perfect loop nests are supported (statements and loops cannot be mixed)");
      }
      statement();
    }
    if (nest_.statements.empty()) {
      throw std::string("the innermost loop has no statements");
    }
  }

  // Affine expression over the indices declared so far (params are substituted).
  Affine affine() {
    Affine result = affine_term();
    while (peek("+") || peek("-")) {
      const bool minus = tokens_[position_++] == "-";
      Affine term = affine_term();
      add(result, term, minus ? -1 : 1);
    }
    return result;
  }

  void add(Affine& lhs, Affine rhs, std::int64_t sign) {
    if (lhs.size() < rhs.size()) {
      lhs.insert(lhs.end() - 1, rhs.size() - lhs.size(), 0);
    }
    if (rhs.size() < lhs.size()) {
      rhs.insert(rhs.end() - 1, lhs.size() - rhs.size(), 0);
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
      lhs[index] += sign * rhs[index];
    }
  }

  Affine affine_term() {
    Affine result = affine_factor();
    while (accept("*")) {
      Affine factor = affine_factor();
      const bool lhs_constant = std::all_of(result.begin(), result.end() - 1, [](std::int64_t c) { return c == 0; });
      const bool rhs_constant = std::all_of(factor.begin(), factor.end() - 1, [](std::int64_t c) { return c == 0; });
      if (!lhs_constant && !rhs_constant) {
        throw std::string("non-affine product of loop indices");
      }
      const std::int64_t scale = lhs_constant ? result.back() : factor.back();
      Affine scaled = lhs_constant ? factor : result;
      for (auto& coefficient : scaled) {
        coefficient *= scale;
      }
      result = std::move(scaled);
    }
    return result;
  }

  Affine affine_factor() {
    Affine result(nest_.indices.size() + 1, 0);
    if (accept("-")) {
      Affine inner = affine_factor();
      for (auto& coefficient : inner) {
        coefficient = -coefficient;
      }
      return inner;
    }
    if (accept("(")) {
      Affine inner = affine();
      expect(")");
      return inner;
    }
    if (position_ < tokens_.size() && std::isdigit(static_cast<unsigned char>(tokens_[position_][0])) != 0) {
      result.back() = integer();
      return result;
    }
    const std::string name = identifier();
    const auto index = std::find(nest_.indices.begin(), nest_.indices.end(), name);
    if (index != nest_.indices.end()) {
      result[static_cast<std::size_t>(index - nest_.indices.begin())] = 1;
      return result;
    }
    const auto param = nest_.params.find(name);
    if (param == nest_.params.end()) {
      throw std::string("unknown name '" + name + "' in affine expression");
    }
    result.back() = param->second;
    return result;
  }

  Access access(const std::string& array) {
    Access result{array, {}, false};
    while (accept("[")) {
      Affine subscript = affine();
      subscript.insert(subscript.end() - 1, nest_.indices.size() + 1 - subscript.size(), 0);
      result.subscripts.push_back(std::move(subscript));
      expect("]");
    }
    if (result.subscripts.empty()) {
      throw std::string("array '" + array + "' needs at least one subscript");
    }
    return result;
  }

  void statement() {
    const std::size_t begin = position_;
    Statement statement;
    statement.target = access(identifier());
    statement.target.write = true;
    expect("=");
    current_ = &statement;
    statement.rhs = expression();
    accept(";");
    for (std::size_t index = begin; index < position_; ++index) {
      statement.text += tokens_[index];
      statement.text += (tokens_[index] == "[" || (index + 1 < position_ && tokens_[index + 1] == "[") ||
                         (index + 1 < position_ && tokens_[index + 1] == "]") || tokens_[index] == ";")
                            ? ""
                            : " ";
    }
    while (!statement.text.empty() && (statement.text.back() == ' ' || statement.text.back() == ';')) {
      statement.text.pop_back();
    }
    nest_.statements.push_back(std::move(statement));
  }

  std::shared_ptr<Expression> make(Expression::Kind kind, std::int64_t value = 0, std::shared_ptr<Expression> lhs = {},
                                   std::shared_ptr<Expression> rhs = {}) {
    auto expr = std::make_shared<Expression>();
    expr->kind = kind;
    expr->value = value;
    expr->lhs = std::move(lhs);
    expr->rhs = std::move(rhs);
    return expr;
  }

  std::shared_ptr<Expression> expression() {
    auto lhs = term();
    while (peek("+") || peek("-")) {
      const bool minus = tokens_[position_++] == "-";
      lhs = make(minus ? Expression::Kind::Sub : Expression::Kind::Add, 0, lhs, term());
    }
    return lhs;
  }

  std::shared_ptr<Expression> term() {
    auto lhs = factor();
    while (accept("*")) {
      lhs = make(Expression::Kind::Mul, 0, lhs, factor());
    }
    return lhs;
  }

  std::shared_ptr<Expression> factor() {
    if (accept("-")) {
      return make(Expression::Kind::Neg, 0, factor());
    }
    if (accept("(")) {
      auto inner = expression();
      expect(")");
      return inner;
    }
    if (position_ < tokens_.size() && std::isdigit(static_cast<unsigned char>(tokens_[position_][0])) != 0) {
      return make(Expression::Kind::Constant, integer());
    }
    const std::string name = identifier();
    if (peek("[")) {
      current_->reads.push_back(access(name));
      return make(Expression::Kind::Read, static_cast<std::int64_t>(current_->reads.size() - 1));
    }
    const auto index = std::find(nest_.indices.begin(), nest_.indices.end(), name);
    if (index != nest_.indices.end()) {
      return make(Expression::Kind::Index, index - nest_.indices.begin());
    }
    const auto param = nest_.params.find(name);
    if (param != nest_.params.end()) {
      return make(Expression::Kind::Constant, param->second);
    }
    throw std::string("unknown name '" + name + "'");
  }

  std::vector<std::string> tokens_;
  std::size_t position_ = 0;
  LoopNest nest_;
  Statement* current_ = nullptr;
};

// ---- Fourier-Motzkin over integer constraints  a.x <= b ---------------------------------------
struct Constraint {
  std::vector<std::int64_t> a;
  std::int64_t b = 0;
  bool operator<(const Constraint& other) const { return std::tie(a, b) < std::tie(other.a, other.b); }
};

// Divides by the gcd of the coefficients and floors the bound (integer tightening).
// Returns false when the constraint is a contradiction 0 <= b < 0.
bool normalize(Constraint& constraint) {
  std::int64_t divisor = 0;
  for (const auto coefficient : constraint.a) {
    divisor = std::gcd(divisor, std::abs(coefficient));
  }
  if (divisor == 0) {
    return constraint.b >= 0;
  }
  for (auto& coefficient : constraint.a) {
    coefficient /= divisor;
  }
  constraint.b = floor_div(constraint.b, divisor);
  return true;
}

// Eliminates variable `variable`; returns nullopt on an explicit contradiction.
std::optional<std::vector<Constraint>> eliminate(const std::vector<Constraint>& system, std::size_t variable) {
  std::vector<Constraint> positive;
  std::vector<Constraint> negative;
  std::set<Constraint> result;
  for (const auto& constraint : system) {
    if (constraint.a[variable] > 0) {
      positive.push_back(constraint);
    } else if (constraint.a[variable] < 0) {
      negative.push_back(constraint);
    } else {
      result.insert(constraint);
    }
  }
  for (const auto& p : positive) {
    for (const auto& q : negative) {
      Constraint combined;
      combined.a.resize(p.a.size());
      const std::int64_t pf = -q.a[variable];
      const std::int64_t qf = p.a[variable];
      for (std::size_t index = 0; index < p.a.size(); ++index) {
        combined.a[index] = pf * p.a[index] + qf * q.a[index];
      }
      combined.b = pf * p.b + qf * q.b;
      if (!normalize(combined)) {
        return std::nullopt;
      }
      result.insert(combined);
    }
  }
  std::vector<Constraint> output;
  for (auto constraint : result) {
    if (std::all_of(constraint.a.begin(), constraint.a.end(), [](std::int64_t c) { return c == 0; })) {
      if (constraint.b < 0) {
        return std::nullopt;
      }
      continue;
    }
    output.push_back(constraint);
  }
  return output;
}

bool fm_feasible(std::vector<Constraint> system, std::size_t variables) {
  for (auto& constraint : system) {
    if (!normalize(constraint)) {
      return false;
    }
  }
  for (std::size_t variable = 0; variable < variables; ++variable) {
    auto next = eliminate(system, variable);
    if (!next.has_value()) {
      return false;
    }
    system = std::move(*next);
  }
  return true;
}

// Domain of the nest in variables [offset, offset + depth) of a `width`-variable system.
void add_domain(const LoopNest& nest, std::size_t offset, std::size_t width, std::vector<Constraint>& system) {
  const std::size_t depth = nest.indices.size();
  for (std::size_t loop = 0; loop < depth; ++loop) {
    Constraint lower{std::vector<std::int64_t>(width, 0), 0};  // lower(x) - x_k <= 0
    Constraint upper{std::vector<std::int64_t>(width, 0), 0};  // x_k - upper(x) <= 0
    for (std::size_t index = 0; index < depth; ++index) {
      lower.a[offset + index] = nest.lower[loop][index];
      upper.a[offset + index] = -nest.upper[loop][index];
    }
    lower.a[offset + loop] -= 1;
    upper.a[offset + loop] += 1;
    lower.b = -nest.lower[loop].back();
    upper.b = nest.upper[loop].back();
    system.push_back(lower);
    system.push_back(upper);
  }
}

// Instances s (variables 0..d-1) and t (d..2d-1) of the nest where `source` at s and `target` at t touch
// the same element and s precedes t at loop level `level` (level d + 1: same iteration).
std::vector<Constraint> dependence_system(const LoopNest& nest, const Access& source, const Access& target,
                                          std::size_t level) {
  const std::size_t depth = nest.indices.size();
  const std::size_t width = 2 * depth;
  std::vector<Constraint> system;
  add_domain(nest, 0, width, system);
  add_domain(nest, depth, width, system);
  for (std::size_t dim = 0; dim < source.subscripts.size(); ++dim) {
    Constraint equal{std::vector<std::int64_t>(width, 0), 0};
    const auto& f = source.subscripts[dim];
    const auto& g = target.subscripts[dim];
    for (std::size_t index = 0; index < depth; ++index) {
      equal.a[index] = f[index];
      equal.a[depth + index] = -g[index];
    }
    equal.b = g.back() - f.back();
    system.push_back(equal);
    Constraint opposite = equal;
    for (auto& c : opposite.a) {
      c = -c;
    }
    opposite.b = -equal.b;
    system.push_back(opposite);
  }
  for (std::size_t loop = 0; loop + 1 < level && loop < depth; ++loop) {
    Constraint le{std::vector<std::int64_t>(width, 0), 0};
    le.a[loop] = 1;
    le.a[depth + loop] = -1;
    Constraint ge = le;
    ge.a[loop] = -1;
    ge.a[depth + loop] = 1;
    system.push_back(le);
    system.push_back(ge);
  }
  if (level <= depth) {
    Constraint before{std::vector<std::int64_t>(width, 0), -1};  // s_l - t_l <= -1
    before.a[level - 1] = 1;
    before.a[depth + level - 1] = -1;
    system.push_back(before);
  }
  return system;
}

void enumerate_domain(const LoopNest& nest, std::size_t loop, std::vector<std::int64_t>& point,
                      const std::function<bool(const std::vector<std::int64_t>&)>& visit, bool& stop) {
  if (stop) {
    return;
  }
  if (loop == nest.indices.size()) {
    stop = !visit(point);
    return;
  }
  const std::int64_t low = eval_affine(nest.lower[loop], point);
  const std::int64_t high = eval_affine(nest.upper[loop], point);
  for (std::int64_t value = low; value <= high && !stop; ++value) {
    point[loop] = value;
    enumerate_domain(nest, loop + 1, point, visit, stop);
  }
  point[loop] = 0;
}

std::vector<std::vector<std::int64_t>> domain_points(const LoopNest& nest, std::size_t limit) {
  std::vector<std::vector<std::int64_t>> points;
  std::vector<std::int64_t> point(nest.indices.size(), 0);
  bool stop = false;
  enumerate_domain(nest, 0, point, [&](const std::vector<std::int64_t>& p) {
    points.push_back(p);
    return points.size() <= limit;
  }, stop);
  return points;
}

std::string affine_text(const Affine& affine, const std::vector<std::string>& names) {
  std::string text;
  for (std::size_t index = 0; index + 1 < affine.size(); ++index) {
    const std::int64_t c = affine[index];
    if (c == 0) {
      continue;
    }
    const std::string sign = c < 0 ? " - " : (text.empty() ? "" : " + ");
    const std::int64_t magnitude = std::abs(c);
    text += (text.empty() && c < 0 ? "-" : sign) + (magnitude == 1 ? "" : std::to_string(magnitude) + "*") + names[index];
  }
  const std::int64_t constant = affine.back();
  if (text.empty()) {
    return std::to_string(constant);
  }
  if (constant != 0) {
    text += (constant < 0 ? " - " : " + ") + std::to_string(std::abs(constant));
  }
  return text;
}

std::string kind_name(DependenceKind kind) {
  switch (kind) {
    case DependenceKind::Flow:
      return "flow";
    case DependenceKind::Anti:
      return "anti";
    case DependenceKind::Output:
      return "output";
  }
  return "?";
}

std::int64_t initial_value(const std::string& array, const std::vector<std::int64_t>& index) {
  std::int64_t hash = static_cast<std::int64_t>(std::hash<std::string>{}(array) % 1000U);
  for (const auto value : index) {
    hash = hash * 31 + value;
  }
  return ((hash % 97) + 97) % 97 + 1;
}

using Memory = std::map<std::string, std::map<std::vector<std::int64_t>, std::int64_t>>;

std::int64_t evaluate(const Expression& expr, const Statement& statement, const std::vector<std::int64_t>& point,
                      Memory& memory) {
  switch (expr.kind) {
    case Expression::Kind::Constant:
      return expr.value;
    case Expression::Kind::Index:
      return point[static_cast<std::size_t>(expr.value)];
    case Expression::Kind::Read: {
      const Access& access = statement.reads[static_cast<std::size_t>(expr.value)];
      std::vector<std::int64_t> index;
      for (const auto& subscript : access.subscripts) {
        index.push_back(eval_affine(subscript, point));
      }
      const auto& array = memory[access.array];
      const auto found = array.find(index);
      return found == array.end() ? initial_value(access.array, index) : found->second;
    }
    case Expression::Kind::Add:
      return evaluate(*expr.lhs, statement, point, memory) + evaluate(*expr.rhs, statement, point, memory);
    case Expression::Kind::Sub:
      return evaluate(*expr.lhs, statement, point, memory) - evaluate(*expr.rhs, statement, point, memory);
    case Expression::Kind::Mul:
      return (evaluate(*expr.lhs, statement, point, memory) * evaluate(*expr.rhs, statement, point, memory)) % 1000003;
    case Expression::Kind::Neg:
      return -evaluate(*expr.lhs, statement, point, memory);
  }
  return 0;
}

void run_instance(const LoopNest& nest, const std::vector<std::int64_t>& point, Memory& memory) {
  for (const auto& statement : nest.statements) {
    const std::int64_t value = evaluate(*statement.rhs, statement, point, memory);
    std::vector<std::int64_t> index;
    for (const auto& subscript : statement.target.subscripts) {
      index.push_back(eval_affine(subscript, point));
    }
    memory[statement.target.array][index] = value;
  }
}

// ---- unimodular algebra ------------------------------------------------------------------------
std::optional<Matrix> integer_inverse(const Matrix& matrix) {
  const std::size_t n = matrix.size();
  // Gauss-Jordan over rationals represented as (numerator, denominator) with int64.
  std::vector<std::vector<std::pair<std::int64_t, std::int64_t>>> work(n, std::vector<std::pair<std::int64_t, std::int64_t>>(2 * n, {0, 1}));
  for (std::size_t row = 0; row < n; ++row) {
    for (std::size_t col = 0; col < n; ++col) {
      work[row][col] = {matrix[row][col], 1};
    }
    work[row][n + row] = {1, 1};
  }
  auto reduce = [](std::pair<std::int64_t, std::int64_t> value) {
    const std::int64_t g = std::gcd(value.first, value.second);
    if (g != 0) {
      value.first /= g;
      value.second /= g;
    }
    if (value.second < 0) {
      value.first = -value.first;
      value.second = -value.second;
    }
    return value;
  };
  for (std::size_t col = 0; col < n; ++col) {
    std::size_t pivot = col;
    while (pivot < n && work[pivot][col].first == 0) {
      ++pivot;
    }
    if (pivot == n) {
      return std::nullopt;
    }
    std::swap(work[pivot], work[col]);
    const auto lead = work[col][col];
    for (auto& value : work[col]) {
      value = reduce({value.first * lead.second, value.second * lead.first});
    }
    for (std::size_t row = 0; row < n; ++row) {
      if (row == col || work[row][col].first == 0) {
        continue;
      }
      const auto factor = work[row][col];
      for (std::size_t k = 0; k < 2 * n; ++k) {
        const auto product = reduce({factor.first * work[col][k].first, factor.second * work[col][k].second});
        work[row][k] = reduce({work[row][k].first * product.second - product.first * work[row][k].second,
                               work[row][k].second * product.second});
      }
    }
  }
  Matrix inverse(n, std::vector<std::int64_t>(n, 0));
  for (std::size_t row = 0; row < n; ++row) {
    for (std::size_t col = 0; col < n; ++col) {
      const auto value = work[row][n + col];
      if (value.second != 1) {
        return std::nullopt;  // not unimodular
      }
      inverse[row][col] = value.first;
    }
  }
  return inverse;
}

std::vector<std::int64_t> transform_vector(const Matrix& matrix, const std::vector<std::int64_t>& vector) {
  std::vector<std::int64_t> result(matrix.size(), 0);
  for (std::size_t row = 0; row < matrix.size(); ++row) {
    for (std::size_t col = 0; col < vector.size(); ++col) {
      result[row] += matrix[row][col] * vector[col];
    }
  }
  return result;
}

bool lexicographically_positive(const std::vector<std::int64_t>& vector) {
  for (const auto value : vector) {
    if (value != 0) {
      return value > 0;
    }
  }
  return false;
}

struct Bound {
  std::vector<std::int64_t> coefficients;  // over outer new indices
  std::int64_t constant = 0;
  std::int64_t divisor = 1;
};

struct Projection {
  std::vector<std::vector<Bound>> lowers;  // per new loop, in terms of the outer new indices
  std::vector<std::vector<Bound>> uppers;
};

// Domain in new coordinates y = T x  (x = T^-1 y): constraints a.x <= b become (a T^-1).y <= b.
std::vector<Constraint> transformed_domain(const LoopNest& nest, const Matrix& inverse) {
  const std::size_t depth = nest.indices.size();
  std::vector<Constraint> original;
  add_domain(nest, 0, depth, original);
  std::vector<Constraint> system;
  for (const auto& constraint : original) {
    Constraint transformed{std::vector<std::int64_t>(depth, 0), constraint.b};
    for (std::size_t col = 0; col < depth; ++col) {
      for (std::size_t k = 0; k < depth; ++k) {
        transformed.a[col] += constraint.a[k] * inverse[k][col];
      }
    }
    if (normalize(transformed)) {
      system.push_back(transformed);
    }
  }
  return system;
}

// Bounds of y_k: project out y_{k+1..d-1} by Fourier-Motzkin.
Projection project_bounds(std::vector<Constraint> projected, std::size_t depth) {
  Projection projection;
  projection.lowers.resize(depth);
  projection.uppers.resize(depth);
  for (std::size_t k = depth; k-- > 0;) {
    for (const auto& constraint : projected) {
      const std::int64_t c = constraint.a[k];
      if (c == 0) {
        continue;
      }
      // c*y_k <= b - sum_{j<k} a_j y_j
      Bound bound{std::vector<std::int64_t>(k, 0), constraint.b, c > 0 ? c : -c};
      for (std::size_t j = 0; j < k; ++j) {
        bound.coefficients[j] = c > 0 ? -constraint.a[j] : constraint.a[j];
      }
      if (c < 0) {
        bound.constant = -constraint.b;
      }
      (c > 0 ? projection.uppers : projection.lowers)[k].push_back(bound);
    }
    if (k > 0) {
      auto next = eliminate(projected, k);
      projected = next.value_or(std::vector<Constraint>{});
    }
  }
  return projection;
}

std::int64_t bound_value(const std::vector<Bound>& bounds, const std::vector<std::int64_t>& y, bool lower) {
  std::int64_t result = lower ? std::numeric_limits<std::int64_t>::min() : std::numeric_limits<std::int64_t>::max();
  for (const auto& bound : bounds) {
    std::int64_t value = bound.constant;
    for (std::size_t j = 0; j < bound.coefficients.size(); ++j) {
      value += bound.coefficients[j] * y[j];
    }
    result = lower ? std::max(result, ceil_div(value, bound.divisor)) : std::min(result, floor_div(value, bound.divisor));
  }
  return result;
}

// `extra` is one more term of the max (lower) or min (upper), such as a tile edge.
std::string bounds_text(const std::vector<Bound>& bounds, bool lower, const std::vector<std::string>& names,
                        const std::string& extra = "") {
  std::vector<std::string> items;
  for (const auto& bound : bounds) {
    Affine affine(bound.coefficients);
    affine.push_back(bound.constant);
    const std::string body = affine_text(affine, names);
    items.push_back(bound.divisor == 1 ? body
                                       : std::string(lower ? "ceil(" : "floor(") + "(" + body + ")/" +
                                             std::to_string(bound.divisor) + ")");
  }
  if (!extra.empty()) {
    items.push_back(extra);
  }
  if (items.size() == 1) {
    return items.front();
  }
  std::string text = lower ? "max(" : "min(";
  for (std::size_t index = 0; index < items.size(); ++index) {
    text += (index ? ", " : "") + items[index];
  }
  return text + ")";
}

}  // namespace

// ---------------------------------------------------------------------------------------------
NestResult parse_loop_nest(const std::string& text) {
  try {
    return {NestParser(text).parse(), ""};
  } catch (const std::string& error) {
    return {std::nullopt, error};
  }
}

DependenceAnalysis analyze_dependences(const LoopNest& nest) {
  DependenceAnalysis analysis;
  const std::size_t depth = nest.indices.size();
  analysis.parallel.assign(depth, true);
  const auto points = domain_points(nest, kEnumerationLimit);
  const bool enumerable = points.size() <= kEnumerationLimit;

  struct Ref {
    std::size_t statement;
    const Access* access;
  };
  std::vector<Ref> refs;
  for (std::size_t s = 0; s < nest.statements.size(); ++s) {
    refs.push_back({s, &nest.statements[s].target});
    for (const auto& read : nest.statements[s].reads) {
      refs.push_back({s, &read});
    }
  }

  for (const auto& source : refs) {
    for (const auto& target : refs) {
      if (source.access->array != target.access->array || (!source.access->write && !target.access->write) ||
          source.access->subscripts.size() != target.access->subscripts.size()) {
        continue;
      }
      const DependenceKind kind = source.access->write ? (target.access->write ? DependenceKind::Output : DependenceKind::Flow)
                                                       : DependenceKind::Anti;
      for (std::size_t level = 1; level <= depth + 1; ++level) {
        if (level == depth + 1 && source.statement >= target.statement) {
          continue;  // loop-independent dependences follow textual order between statements
        }
        if (level == depth + 1 && source.access == target.access) {
          continue;
        }
        const std::size_t width = 2 * depth;
        const auto system = dependence_system(nest, *source.access, *target.access, level);
        ++analysis.fm_tests;
        const bool feasible = fm_feasible(system, width);

        std::set<std::vector<std::int64_t>> distances;
        if (enumerable) {
          for (const auto& s : points) {
            for (const auto& t : points) {
              bool ordered = true;
              for (std::size_t loop = 0; loop + 1 < level && loop < depth; ++loop) {
                ordered = ordered && s[loop] == t[loop];
              }
              if (level <= depth) {
                ordered = ordered && s[level - 1] < t[level - 1];
              }
              if (!ordered) {
                continue;
              }
              bool same = true;
              for (std::size_t dim = 0; dim < source.access->subscripts.size() && same; ++dim) {
                same = eval_affine(source.access->subscripts[dim], s) == eval_affine(target.access->subscripts[dim], t);
              }
              if (same) {
                std::vector<std::int64_t> distance(depth);
                for (std::size_t loop = 0; loop < depth; ++loop) {
                  distance[loop] = t[loop] - s[loop];
                }
                distances.insert(distance);
              }
            }
          }
        }
        if (!feasible && distances.empty()) {
          continue;
        }
        if (!feasible) {
          ++analysis.fm_misses;  // FM is a sound relaxation: this would be a bug
        }
        if (enumerable && distances.empty()) {
          ++analysis.fm_false_positives;
          continue;
        }
        Dependence dependence;
        dependence.kind = kind;
        dependence.source_statement = source.statement;
        dependence.target_statement = target.statement;
        dependence.array = source.access->array;
        dependence.level = level;
        dependence.distances.assign(distances.begin(), distances.end());
        if (distances.size() == 1) {
          dependence.distance = *distances.begin();
        }
        dependence.direction = "(";
        for (std::size_t loop = 0; loop < depth; ++loop) {
          bool less = false;
          bool equal = false;
          bool greater = false;
          for (const auto& distance : distances) {
            less = less || distance[loop] > 0;
            equal = equal || distance[loop] == 0;
            greater = greater || distance[loop] < 0;
          }
          const int kinds = (less ? 1 : 0) + (equal ? 1 : 0) + (greater ? 1 : 0);
          dependence.direction += (loop ? "," : "");
          dependence.direction += !enumerable ? "?" : kinds > 1 ? "*" : less ? "<" : equal ? "=" : ">";
        }
        dependence.direction += ")";
        if (level <= depth) {
          analysis.parallel[level - 1] = false;
        }
        analysis.dependences.push_back(std::move(dependence));
      }
    }
  }
  return analysis;
}

std::string print_dependences(const LoopNest& nest, const DependenceAnalysis& analysis) {
  std::ostringstream out;
  out << "loop nest (" << nest.indices.size() << " loops):\n";
  for (std::size_t loop = 0; loop < nest.indices.size(); ++loop) {
    out << std::string(2 * loop + 2, ' ') << "for " << nest.indices[loop] << " = " << affine_text(nest.lower[loop], nest.indices)
        << " to " << affine_text(nest.upper[loop], nest.indices) << '\n';
  }
  for (std::size_t s = 0; s < nest.statements.size(); ++s) {
    out << std::string(2 * nest.indices.size() + 2, ' ') << "S" << s + 1 << ": " << nest.statements[s].text << '\n';
  }
  out << "dependences (" << analysis.dependences.size() << ", " << analysis.fm_tests << " Fourier-Motzkin tests, "
      << analysis.fm_false_positives << " real-shadow false positive(s) removed by enumeration):\n";
  for (const auto& dependence : analysis.dependences) {
    out << "  " << kind_name(dependence.kind) << ' ' << dependence.array << ": S" << dependence.source_statement + 1
        << " -> S" << dependence.target_statement + 1 << "  "
        << (dependence.level <= nest.indices.size() ? "carried by loop " + nest.indices[dependence.level - 1]
                                                     : std::string("loop-independent"))
        << "  direction " << dependence.direction;
    if (dependence.distance.has_value()) {
      out << "  distance (";
      for (std::size_t index = 0; index < dependence.distance->size(); ++index) {
        out << (index ? "," : "") << (*dependence.distance)[index];
      }
      out << ')';
    }
    out << '\n';
  }
  out << "parallel loops:";
  bool any = false;
  for (std::size_t loop = 0; loop < nest.indices.size(); ++loop) {
    if (analysis.parallel[loop]) {
      out << ' ' << nest.indices[loop];
      any = true;
    }
  }
  out << (any ? "" : " none") << '\n';
  return out.str();
}

Matrix multiply(const Matrix& lhs, const Matrix& rhs) {
  Matrix result(lhs.size(), std::vector<std::int64_t>(rhs.front().size(), 0));
  for (std::size_t row = 0; row < lhs.size(); ++row) {
    for (std::size_t col = 0; col < rhs.front().size(); ++col) {
      for (std::size_t k = 0; k < rhs.size(); ++k) {
        result[row][col] += lhs[row][k] * rhs[k][col];
      }
    }
  }
  return result;
}

namespace {
Matrix identity(std::size_t depth) {
  Matrix matrix(depth, std::vector<std::int64_t>(depth, 0));
  for (std::size_t index = 0; index < depth; ++index) {
    matrix[index][index] = 1;
  }
  return matrix;
}
}  // namespace

std::optional<Matrix> interchange_matrix(std::size_t depth, std::size_t a, std::size_t b) {
  if (a == 0 || b == 0 || a > depth || b > depth) {
    return std::nullopt;
  }
  Matrix matrix = identity(depth);
  std::swap(matrix[a - 1], matrix[b - 1]);
  return matrix;
}

std::optional<Matrix> reversal_matrix(std::size_t depth, std::size_t loop) {
  if (loop == 0 || loop > depth) {
    return std::nullopt;
  }
  Matrix matrix = identity(depth);
  matrix[loop - 1][loop - 1] = -1;
  return matrix;
}

std::optional<Matrix> skew_matrix(std::size_t depth, std::size_t target, std::size_t source, std::int64_t factor) {
  if (target == 0 || source == 0 || target > depth || source > depth || target == source) {
    return std::nullopt;
  }
  Matrix matrix = identity(depth);
  matrix[target - 1][source - 1] = factor;
  return matrix;
}

Memory execute(const LoopNest& nest) {
  Memory memory;
  std::vector<std::int64_t> point(nest.indices.size(), 0);
  bool stop = false;
  enumerate_domain(nest, 0, point, [&](const std::vector<std::int64_t>& p) {
    run_instance(nest, p, memory);
    return true;
  }, stop);
  return memory;
}

TransformResult apply_transformation(const LoopNest& nest, const DependenceAnalysis& analysis, const Matrix& transform) {
  TransformResult result;
  const std::size_t depth = nest.indices.size();
  const auto inverse = integer_inverse(transform);
  if (!inverse.has_value()) {
    result.reason = "the transformation matrix is not unimodular";
    return result;
  }
  // Legality: every carried distance must stay lexicographically positive.
  for (const auto& dependence : analysis.dependences) {
    if (dependence.level > depth) {
      continue;
    }
    for (const auto& distance : dependence.distances) {
      if (!lexicographically_positive(transform_vector(transform, distance))) {
        std::ostringstream reason;
        reason << "illegal: " << kind_name(dependence.kind) << " dependence on " << dependence.array << " with distance (";
        for (std::size_t index = 0; index < distance.size(); ++index) {
          reason << (index ? "," : "") << distance[index];
        }
        reason << ") would be reversed";
        result.reason = reason.str();
        return result;
      }
    }
  }
  result.legal = true;
  result.reason = "legal: all transformed dependence distances are lexicographically positive";
  result.parallel_after.assign(depth, true);
  for (const auto& dependence : analysis.dependences) {
    if (dependence.level > depth) {
      continue;
    }
    for (const auto& distance : dependence.distances) {
      const auto moved = transform_vector(transform, distance);
      for (std::size_t loop = 0; loop < depth; ++loop) {
        if (moved[loop] != 0) {
          result.parallel_after[loop] = false;
          break;
        }
      }
    }
  }

  const auto projection = project_bounds(transformed_domain(nest, *inverse), depth);
  const auto& lowers = projection.lowers;
  const auto& uppers = projection.uppers;

  std::vector<std::string> new_names;
  for (std::size_t k = 0; k < depth; ++k) {
    new_names.push_back("c" + std::to_string(k + 1));
  }
  std::ostringstream code;
  for (std::size_t k = 0; k < depth; ++k) {
    code << std::string(2 * k, ' ') << "for " << new_names[k] << " = " << bounds_text(lowers[k], true, new_names)
         << " to " << bounds_text(uppers[k], false, new_names) << " {\n";
  }
  for (std::size_t loop = 0; loop < depth; ++loop) {
    Affine row((*inverse)[loop]);
    row.push_back(0);
    code << std::string(2 * depth, ' ') << nest.indices[loop] << " = " << affine_text(row, new_names) << '\n';
  }
  for (const auto& statement : nest.statements) {
    code << std::string(2 * depth, ' ') << statement.text << '\n';
  }
  for (std::size_t k = depth; k-- > 0;) {
    code << std::string(2 * k, ' ') << "}\n";
  }
  result.code = code.str();

  // Verification: run the original nest and the generated one.
  const Memory expected = execute(nest);
  Memory actual;
  std::vector<std::int64_t> y(depth, 0);
  std::size_t transformed_instances = 0;
  bool out_of_domain = false;
  std::function<void(std::size_t)> walk = [&](std::size_t k) {
    if (k == depth) {
      const auto x = transform_vector(*inverse, y);
      for (std::size_t loop = 0; loop < depth; ++loop) {
        if (x[loop] < eval_affine(nest.lower[loop], x) || x[loop] > eval_affine(nest.upper[loop], x)) {
          out_of_domain = true;
          return;
        }
      }
      ++transformed_instances;
      run_instance(nest, x, actual);
      return;
    }
    if (lowers[k].empty() || uppers[k].empty()) {
      return;  // empty (or unbounded) projection
    }
    const std::int64_t low = bound_value(lowers[k], y, true);
    const std::int64_t high = bound_value(uppers[k], y, false);
    for (std::int64_t value = low; value <= high; ++value) {
      y[k] = value;
      walk(k + 1);
    }
  };
  walk(0);
  std::size_t original_instances = 0;
  std::vector<std::int64_t> point(depth, 0);
  bool stop = false;
  enumerate_domain(nest, 0, point, [&](const std::vector<std::int64_t>&) {
    ++original_instances;
    return true;
  }, stop);
  result.original_instances = original_instances;
  result.transformed_instances = transformed_instances;
  result.verified = !out_of_domain && transformed_instances == original_instances && actual == expected;
  return result;
}

// ---- loop tiling and the cache model -----------------------------------------------------------
namespace {

// Row-major placement of every array touched by the nest, each array starting on a fresh cache line.
struct ArrayLayout {
  std::vector<std::int64_t> low;
  std::vector<std::int64_t> extent;
  std::int64_t base = 0;
};
using Layout = std::map<std::string, ArrayLayout>;

Layout make_layout(const LoopNest& nest, std::size_t line_elements) {
  std::map<std::string, std::pair<std::vector<std::int64_t>, std::vector<std::int64_t>>> ranges;
  auto note = [&](const Access& access, const std::vector<std::int64_t>& point) {
    auto [entry, inserted] = ranges.try_emplace(access.array);
    auto& [low, high] = entry->second;
    if (inserted) {
      low.assign(access.subscripts.size(), std::numeric_limits<std::int64_t>::max());
      high.assign(access.subscripts.size(), std::numeric_limits<std::int64_t>::min());
    }
    for (std::size_t dim = 0; dim < access.subscripts.size() && dim < low.size(); ++dim) {
      const std::int64_t value = eval_affine(access.subscripts[dim], point);
      low[dim] = std::min(low[dim], value);
      high[dim] = std::max(high[dim], value);
    }
  };
  std::vector<std::int64_t> point(nest.indices.size(), 0);
  bool stop = false;
  enumerate_domain(nest, 0, point, [&](const std::vector<std::int64_t>& p) {
    for (const auto& statement : nest.statements) {
      for (const auto& read : statement.reads) {
        note(read, p);
      }
      note(statement.target, p);
    }
    return true;
  }, stop);
  Layout layout;
  const auto line = static_cast<std::int64_t>(std::max<std::size_t>(line_elements, 1));
  std::int64_t next = 0;
  for (const auto& [array, range] : ranges) {
    ArrayLayout placed;
    placed.low = range.first;
    std::int64_t size = 1;
    for (std::size_t dim = 0; dim < range.first.size(); ++dim) {
      placed.extent.push_back(range.second[dim] - range.first[dim] + 1);
      size *= placed.extent.back();
    }
    placed.base = next;
    next += (size + line - 1) / line * line;
    layout[array] = std::move(placed);
  }
  return layout;
}

class LruCache {
 public:
  explicit LruCache(const CacheConfig& config)
      : line_elements_(std::max<std::size_t>(config.line_elements, 1)),
        ways_(std::max<std::size_t>(config.ways, 1)),
        sets_(std::max<std::size_t>(config.sets, 1)) {}

  void access(std::int64_t address, CacheStats& stats) {
    const auto line = static_cast<std::uint64_t>(address) / line_elements_;
    auto& set = sets_[line % sets_.size()];
    ++stats.accesses;
    const auto found = std::find(set.begin(), set.end(), line);
    if (found != set.end()) {
      set.erase(found);
    } else {
      ++stats.misses;
      if (set.size() >= ways_) {
        set.pop_back();
      }
    }
    set.insert(set.begin(), line);  // most recently used first
  }

 private:
  std::size_t line_elements_;
  std::size_t ways_;
  std::vector<std::vector<std::uint64_t>> sets_;
};

std::int64_t address_of(const Layout& layout, const Access& access, const std::vector<std::int64_t>& point) {
  const auto& placed = layout.at(access.array);
  std::int64_t offset = 0;
  for (std::size_t dim = 0; dim < placed.extent.size(); ++dim) {
    offset = offset * placed.extent[dim] + (eval_affine(access.subscripts[dim], point) - placed.low[dim]);
  }
  return placed.base + offset;
}

// The references of one instance in execution order: the reads of each statement, then its write.
void trace_instance(const LoopNest& nest, const std::vector<std::int64_t>& point, const Layout& layout,
                    LruCache& cache, CacheStats& stats) {
  for (const auto& statement : nest.statements) {
    for (const auto& read : statement.reads) {
      cache.access(address_of(layout, read, point), stats);
    }
    cache.access(address_of(layout, statement.target, point), stats);
  }
}

// Range of y_k over the whole transformed domain (every other variable projected out).
std::pair<std::int64_t, std::int64_t> global_range(std::vector<Constraint> system, std::size_t depth, std::size_t k) {
  for (std::size_t j = 0; j < depth; ++j) {
    if (j != k) {
      system = eliminate(system, j).value_or(std::vector<Constraint>{});
    }
  }
  std::int64_t low = std::numeric_limits<std::int64_t>::min();
  std::int64_t high = std::numeric_limits<std::int64_t>::max();
  for (const auto& constraint : system) {
    const std::int64_t c = constraint.a[k];
    if (c > 0) {
      high = std::min(high, floor_div(constraint.b, c));
    } else if (c < 0) {
      low = std::max(low, ceil_div(-constraint.b, -c));
    }
  }
  return {low, high};
}

std::string vector_text(const std::vector<std::int64_t>& vector) {
  std::string text = "(";
  for (std::size_t index = 0; index < vector.size(); ++index) {
    text += (index ? "," : "") + std::to_string(vector[index]);
  }
  return text + ")";
}

}  // namespace

bool fully_permutable(const LoopNest& nest, const DependenceAnalysis& analysis, const Matrix& transform,
                      std::string* reason) {
  const std::size_t depth = nest.indices.size();
  const bool enumerable = domain_points(nest, kEnumerationLimit).size() <= kEnumerationLimit;
  if (enumerable) {
    for (const auto& dependence : analysis.dependences) {
      for (const auto& distance : dependence.distances) {
        const auto moved = transform_vector(transform, distance);
        for (std::size_t k = 0; k < depth; ++k) {
          if (moved[k] < 0) {
            if (reason != nullptr) {
              *reason = "not fully permutable: " + kind_name(dependence.kind) + " dependence on " + dependence.array +
                        " with distance " + vector_text(distance) + " becomes " + vector_text(moved) +
                        ", negative in loop " + std::to_string(k + 1);
            }
            return false;
          }
        }
      }
    }
    return true;
  }
  // Too many instances to enumerate: ask Fourier-Motzkin whether some instance has (T d)_k <= -1.
  std::vector<const Access*> refs;
  for (const auto& statement : nest.statements) {
    refs.push_back(&statement.target);
    for (const auto& read : statement.reads) {
      refs.push_back(&read);
    }
  }
  for (const auto* source : refs) {
    for (const auto* target : refs) {
      if (source->array != target->array || (!source->write && !target->write) ||
          source->subscripts.size() != target->subscripts.size()) {
        continue;
      }
      for (std::size_t level = 1; level <= depth; ++level) {
        const auto base = dependence_system(nest, *source, *target, level);
        for (std::size_t k = 0; k < depth; ++k) {
          auto system = base;
          Constraint negative{std::vector<std::int64_t>(2 * depth, 0), -1};  // sum_j T_kj (t_j - s_j) <= -1
          for (std::size_t j = 0; j < depth; ++j) {
            negative.a[j] = -transform[k][j];
            negative.a[depth + j] = transform[k][j];
          }
          system.push_back(negative);
          if (fm_feasible(system, 2 * depth)) {
            if (reason != nullptr) {
              *reason = "not fully permutable (Fourier-Motzkin): a dependence on " + source->array +
                        " carried by loop " + std::to_string(level) + " may become negative in loop " +
                        std::to_string(k + 1);
            }
            return false;
          }
        }
      }
    }
  }
  return true;
}

std::optional<Matrix> find_tiling_skew(const LoopNest& nest, const DependenceAnalysis& analysis, const Matrix& transform,
                                       std::int64_t max_factor) {
  const std::size_t depth = nest.indices.size();
  std::vector<std::pair<std::size_t, std::size_t>> slots;  // below the diagonal
  for (std::size_t row = 1; row < depth; ++row) {
    for (std::size_t col = 0; col < row; ++col) {
      slots.emplace_back(row, col);
    }
  }
  std::vector<std::vector<std::int64_t>> candidates;
  std::vector<std::int64_t> factors(slots.size(), 0);
  std::function<void(std::size_t)> generate = [&](std::size_t slot) {
    if (slot == slots.size()) {
      candidates.push_back(factors);
      return;
    }
    for (std::int64_t value = 0; value <= max_factor; ++value) {
      factors[slot] = value;
      generate(slot + 1);
    }
  };
  generate(0);
  std::stable_sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
    return std::accumulate(a.begin(), a.end(), std::int64_t{0}) < std::accumulate(b.begin(), b.end(), std::int64_t{0});
  });
  for (const auto& candidate : candidates) {
    Matrix skew = identity(depth);
    for (std::size_t slot = 0; slot < slots.size(); ++slot) {
      skew[slots[slot].first][slots[slot].second] = candidate[slot];
    }
    const Matrix combined = multiply(skew, transform);
    if (fully_permutable(nest, analysis, combined)) {
      return combined;
    }
  }
  return std::nullopt;
}

CacheStats simulate_cache(const LoopNest& nest, const CacheConfig& cache) {
  const Layout layout = make_layout(nest, cache.line_elements);
  LruCache lru(cache);
  CacheStats stats;
  std::vector<std::int64_t> point(nest.indices.size(), 0);
  bool stop = false;
  enumerate_domain(nest, 0, point, [&](const std::vector<std::int64_t>& p) {
    trace_instance(nest, p, layout, lru, stats);
    return true;
  }, stop);
  return stats;
}

TileResult apply_tiling(const LoopNest& nest, const DependenceAnalysis& analysis, const Matrix& transform,
                        const std::vector<std::int64_t>& tile_sizes, const CacheConfig& cache) {
  TileResult result;
  result.tile_sizes = tile_sizes;
  const std::size_t depth = nest.indices.size();
  if (tile_sizes.size() != depth ||
      std::any_of(tile_sizes.begin(), tile_sizes.end(), [](std::int64_t size) { return size < 0; })) {
    result.reason = "need one tile size >= 0 per loop (0 = not tiled)";
    return result;
  }
  const auto inverse = integer_inverse(transform);
  if (!inverse.has_value()) {
    result.reason = "the transformation matrix is not unimodular";
    return result;
  }
  if (!fully_permutable(nest, analysis, transform, &result.reason)) {
    return result;
  }
  result.legal = true;
  result.reason = "legal: the band is fully permutable (T d >= 0 for every dependence), so rectangular tiles keep every dependence";

  const auto system = transformed_domain(nest, *inverse);
  const auto projection = project_bounds(system, depth);
  std::vector<std::pair<std::int64_t, std::int64_t>> ranges;
  for (std::size_t k = 0; k < depth; ++k) {
    ranges.push_back(global_range(system, depth, k));
  }

  std::vector<std::string> point_names;
  std::vector<std::string> tile_names;
  for (std::size_t k = 0; k < depth; ++k) {
    point_names.push_back("c" + std::to_string(k + 1));
    tile_names.push_back("t" + std::to_string(k + 1));
  }
  std::ostringstream code;
  std::size_t indent = 0;
  for (std::size_t k = 0; k < depth; ++k) {
    if (tile_sizes[k] == 0) {
      continue;
    }
    code << std::string(2 * indent++, ' ') << "for " << tile_names[k] << " = " << ranges[k].first << " to "
         << ranges[k].second << " step " << tile_sizes[k] << " {\n";
  }
  for (std::size_t k = 0; k < depth; ++k) {
    const bool tiled = tile_sizes[k] != 0;
    code << std::string(2 * indent++, ' ') << "for " << point_names[k] << " = "
         << bounds_text(projection.lowers[k], true, point_names, tiled ? tile_names[k] : "") << " to "
         << bounds_text(projection.uppers[k], false, point_names,
                        tiled ? tile_names[k] + " + " + std::to_string(tile_sizes[k] - 1) : "")
         << " {\n";
  }
  for (std::size_t loop = 0; loop < depth; ++loop) {
    Affine row((*inverse)[loop]);
    row.push_back(0);
    code << std::string(2 * indent, ' ') << nest.indices[loop] << " = " << affine_text(row, point_names) << '\n';
  }
  for (const auto& statement : nest.statements) {
    code << std::string(2 * indent, ' ') << statement.text << '\n';
  }
  for (std::size_t level = indent; level-- > 0;) {
    code << std::string(2 * level, ' ') << "}\n";
  }
  result.code = code.str();

  // Run the generated nest: tile loops, then point loops clipped to the tile.
  const Memory expected = execute(nest);
  Memory actual;
  const Layout layout = make_layout(nest, cache.line_elements);
  LruCache lru(cache);
  std::vector<std::int64_t> tile(depth, 0);
  std::vector<std::int64_t> y(depth, 0);
  bool out_of_domain = false;
  bool tile_used = false;
  std::function<void(std::size_t)> points = [&](std::size_t k) {
    if (k == depth) {
      const auto x = transform_vector(*inverse, y);
      for (std::size_t loop = 0; loop < depth; ++loop) {
        if (x[loop] < eval_affine(nest.lower[loop], x) || x[loop] > eval_affine(nest.upper[loop], x)) {
          out_of_domain = true;
          return;
        }
      }
      tile_used = true;
      ++result.tiled_instances;
      run_instance(nest, x, actual);
      trace_instance(nest, x, layout, lru, result.tiled_cache);
      return;
    }
    if (projection.lowers[k].empty() || projection.uppers[k].empty()) {
      return;
    }
    std::int64_t low = bound_value(projection.lowers[k], y, true);
    std::int64_t high = bound_value(projection.uppers[k], y, false);
    if (tile_sizes[k] != 0) {
      low = std::max(low, tile[k]);
      high = std::min(high, tile[k] + tile_sizes[k] - 1);
    }
    for (std::int64_t value = low; value <= high; ++value) {
      y[k] = value;
      points(k + 1);
    }
  };
  std::function<void(std::size_t)> tiles = [&](std::size_t k) {
    if (k == depth) {
      tile_used = false;
      points(0);
      result.tiles += tile_used ? 1 : 0;
      return;
    }
    if (tile_sizes[k] == 0) {
      tiles(k + 1);
      return;
    }
    for (std::int64_t start = ranges[k].first; start <= ranges[k].second; start += tile_sizes[k]) {
      tile[k] = start;
      tiles(k + 1);
    }
  };
  if (std::all_of(ranges.begin(), ranges.end(), [](const auto& range) { return range.first <= range.second; })) {
    tiles(0);
  }
  result.original_cache = simulate_cache(nest, cache);
  std::vector<std::int64_t> point(depth, 0);
  bool stop = false;
  enumerate_domain(nest, 0, point, [&](const std::vector<std::int64_t>&) {
    ++result.original_instances;
    return true;
  }, stop);
  result.verified = !out_of_domain && result.tiled_instances == result.original_instances && actual == expected;
  return result;
}

}  // namespace nexus::compiler::polyhedral
