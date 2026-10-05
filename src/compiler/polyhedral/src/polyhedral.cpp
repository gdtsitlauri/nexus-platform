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
        // Variables: s_0..s_{d-1}, t_0..t_{d-1}
        const std::size_t width = 2 * depth;
        std::vector<Constraint> system;
        add_domain(nest, 0, width, system);
        add_domain(nest, depth, width, system);
        for (std::size_t dim = 0; dim < source.access->subscripts.size(); ++dim) {
          Constraint equal{std::vector<std::int64_t>(width, 0), 0};
          const auto& f = source.access->subscripts[dim];
          const auto& g = target.access->subscripts[dim];
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

  // Domain in new coordinates y = T x  (x = T^-1 y): constraints a.x <= b become (a T^-1).y <= b.
  std::vector<Constraint> original;
  add_domain(nest, 0, depth, original);
  std::vector<Constraint> system;
  for (const auto& constraint : original) {
    Constraint transformed{std::vector<std::int64_t>(depth, 0), constraint.b};
    for (std::size_t col = 0; col < depth; ++col) {
      for (std::size_t k = 0; k < depth; ++k) {
        transformed.a[col] += constraint.a[k] * (*inverse)[k][col];
      }
    }
    if (normalize(transformed)) {
      system.push_back(transformed);
    }
  }
  // Bounds of y_k: project out y_{k+1..d-1} by Fourier-Motzkin.
  std::vector<std::vector<Bound>> lowers(depth);
  std::vector<std::vector<Bound>> uppers(depth);
  std::vector<Constraint> projected = system;
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
      (c > 0 ? uppers : lowers)[k].push_back(bound);
    }
    if (k > 0) {
      auto next = eliminate(projected, k);
      projected = next.value_or(std::vector<Constraint>{});
    }
  }

  std::vector<std::string> new_names;
  for (std::size_t k = 0; k < depth; ++k) {
    new_names.push_back("c" + std::to_string(k + 1));
  }
  auto bound_text = [&](const Bound& bound, bool lower) {
    Affine affine(bound.coefficients);
    affine.push_back(bound.constant);
    const std::string body = affine_text(affine, new_names);
    if (bound.divisor == 1) {
      return body;
    }
    return std::string(lower ? "ceil(" : "floor(") + "(" + body + ")/" + std::to_string(bound.divisor) + ")";
  };
  std::ostringstream code;
  for (std::size_t k = 0; k < depth; ++k) {
    code << std::string(2 * k, ' ') << "for " << new_names[k] << " = ";
    auto list = [&](const std::vector<Bound>& bounds, bool lower) {
      if (bounds.size() == 1) {
        return bound_text(bounds.front(), lower);
      }
      std::string text = lower ? "max(" : "min(";
      for (std::size_t index = 0; index < bounds.size(); ++index) {
        text += (index ? ", " : "") + bound_text(bounds[index], lower);
      }
      return text + ")";
    };
    code << list(lowers[k], true) << " to " << list(uppers[k], false) << " {\n";
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
    std::int64_t low = std::numeric_limits<std::int64_t>::min();
    std::int64_t high = std::numeric_limits<std::int64_t>::max();
    for (const auto& bound : lowers[k]) {
      std::int64_t value = bound.constant;
      for (std::size_t j = 0; j < k; ++j) {
        value += bound.coefficients[j] * y[j];
      }
      low = std::max(low, ceil_div(value, bound.divisor));
    }
    for (const auto& bound : uppers[k]) {
      std::int64_t value = bound.constant;
      for (std::size_t j = 0; j < k; ++j) {
        value += bound.coefficients[j] * y[j];
      }
      high = std::min(high, floor_div(value, bound.divisor));
    }
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

}  // namespace nexus::compiler::polyhedral
