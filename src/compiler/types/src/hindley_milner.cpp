#include "nexus/compiler/types/hindley_milner.hpp"

#include <cctype>
#include <map>
#include <memory>
#include <set>
#include <sstream>

namespace nexus::compiler::types {

namespace {

// ---- types ----------------------------------------------------------------------------------
struct Type;
using TypePtr = std::shared_ptr<Type>;

struct Type {
  enum class Kind { Var, Con } kind = Kind::Var;
  int id = 0;                    // type variable id
  std::string name;              // constructor: int, bool, ->, *, list
  std::vector<TypePtr> args;
  TypePtr instance;              // union-find link for bound variables
};

TypePtr var(int id) {
  auto type = std::make_shared<Type>();
  type->kind = Type::Kind::Var;
  type->id = id;
  return type;
}

TypePtr con(std::string name, std::vector<TypePtr> args = {}) {
  auto type = std::make_shared<Type>();
  type->kind = Type::Kind::Con;
  type->name = std::move(name);
  type->args = std::move(args);
  return type;
}

TypePtr arrow(TypePtr from, TypePtr to) { return con("->", {std::move(from), std::move(to)}); }

TypePtr prune(TypePtr type) {
  while (type->kind == Type::Kind::Var && type->instance) {
    type = type->instance;
  }
  return type;
}

struct Scheme {
  std::set<int> quantified;
  TypePtr type;
};

// ---- syntax ---------------------------------------------------------------------------------
struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

struct Expr {
  enum class Kind { Int, Bool, Var, Fun, App, Let, LetRec, If, BinOp, Pair } kind = Kind::Int;
  std::string name;  // variable / parameter / binder / operator
  std::vector<ExprPtr> children;
};

class Parser {
 public:
  explicit Parser(const std::string& text) {
    for (std::size_t position = 0; position < text.size();) {
      const char ch = text[position];
      if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
        ++position;
      } else if (std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_' || ch == '\'') {
        std::size_t end = position;
        while (end < text.size() && (std::isalnum(static_cast<unsigned char>(text[end])) != 0 || text[end] == '_' ||
                                     text[end] == '\'')) {
          ++end;
        }
        tokens_.push_back(text.substr(position, end - position));
        position = end;
      } else if (text.compare(position, 2, "->") == 0) {
        tokens_.emplace_back("->");
        position += 2;
      } else {
        tokens_.emplace_back(1, ch);
        ++position;
      }
    }
  }

  ExprPtr parse() {
    ExprPtr expr = expression();
    if (position_ != tokens_.size()) {
      throw std::string("unexpected '" + tokens_[position_] + "'");
    }
    return expr;
  }

 private:
  [[nodiscard]] bool peek(const std::string& token) const {
    return position_ < tokens_.size() && tokens_[position_] == token;
  }

  void expect(const std::string& token) {
    if (!peek(token)) {
      throw std::string("expected '" + token + "'" + (position_ < tokens_.size() ? " before '" + tokens_[position_] + "'" : " at end"));
    }
    ++position_;
  }

  std::string identifier() {
    if (position_ >= tokens_.size() || std::isalpha(static_cast<unsigned char>(tokens_[position_][0])) == 0 ||
        keyword(tokens_[position_])) {
      throw std::string("expected an identifier");
    }
    return tokens_[position_++];
  }

  static bool keyword(const std::string& token) {
    static const std::set<std::string> kKeywords = {"fun", "let", "rec", "in", "if", "then", "else", "true", "false"};
    return kKeywords.contains(token);
  }

  static ExprPtr node(Expr::Kind kind, std::string name = {}) {
    auto expr = std::make_unique<Expr>();
    expr->kind = kind;
    expr->name = std::move(name);
    return expr;
  }

  ExprPtr expression() {
    if (peek("fun")) {
      ++position_;
      auto expr = node(Expr::Kind::Fun, identifier());
      expect("->");
      expr->children.push_back(expression());
      return expr;
    }
    if (peek("let")) {
      ++position_;
      const bool recursive = peek("rec");
      if (recursive) {
        ++position_;
      }
      auto expr = node(recursive ? Expr::Kind::LetRec : Expr::Kind::Let, identifier());
      // let f x y = e  is sugar for  let f = fun x -> fun y -> e
      std::vector<std::string> parameters;
      while (!peek("=")) {
        parameters.push_back(identifier());
      }
      expect("=");
      ExprPtr bound = expression();
      for (auto it = parameters.rbegin(); it != parameters.rend(); ++it) {
        auto fun = node(Expr::Kind::Fun, *it);
        fun->children.push_back(std::move(bound));
        bound = std::move(fun);
      }
      expect("in");
      expr->children.push_back(std::move(bound));
      expr->children.push_back(expression());
      return expr;
    }
    if (peek("if")) {
      ++position_;
      auto expr = node(Expr::Kind::If);
      expr->children.push_back(expression());
      expect("then");
      expr->children.push_back(expression());
      expect("else");
      expr->children.push_back(expression());
      return expr;
    }
    return comparison();
  }

  ExprPtr binary(ExprPtr lhs, const std::string& op, ExprPtr rhs) {
    auto expr = node(Expr::Kind::BinOp, op);
    expr->children.push_back(std::move(lhs));
    expr->children.push_back(std::move(rhs));
    return expr;
  }

  ExprPtr comparison() {
    ExprPtr lhs = additive();
    while (peek("<") || peek("=")) {
      const std::string op = tokens_[position_++];
      lhs = binary(std::move(lhs), op, additive());
    }
    return lhs;
  }

  ExprPtr additive() {
    ExprPtr lhs = multiplicative();
    while (peek("+") || peek("-")) {
      const std::string op = tokens_[position_++];
      lhs = binary(std::move(lhs), op, multiplicative());
    }
    return lhs;
  }

  ExprPtr multiplicative() {
    ExprPtr lhs = application();
    while (peek("*")) {
      ++position_;
      lhs = binary(std::move(lhs), "*", application());
    }
    return lhs;
  }

  [[nodiscard]] bool starts_atom() const {
    if (position_ >= tokens_.size()) {
      return false;
    }
    const std::string& token = tokens_[position_];
    return token == "(" || token == "true" || token == "false" ||
           ((std::isalnum(static_cast<unsigned char>(token[0])) != 0 || token[0] == '_') && !keyword(token));
  }

  ExprPtr application() {
    ExprPtr expr = atom();
    while (starts_atom()) {
      auto app = node(Expr::Kind::App);
      app->children.push_back(std::move(expr));
      app->children.push_back(atom());
      expr = std::move(app);
    }
    return expr;
  }

  ExprPtr atom() {
    if (position_ >= tokens_.size()) {
      throw std::string("unexpected end of input");
    }
    const std::string token = tokens_[position_];
    if (token == "(") {
      ++position_;
      ExprPtr first = expression();
      if (peek(",")) {
        ++position_;
        auto pair = node(Expr::Kind::Pair);
        pair->children.push_back(std::move(first));
        pair->children.push_back(expression());
        expect(")");
        return pair;
      }
      expect(")");
      return first;
    }
    if (token == "true" || token == "false") {
      ++position_;
      return node(Expr::Kind::Bool, token);
    }
    if (std::isdigit(static_cast<unsigned char>(token[0])) != 0) {
      ++position_;
      return node(Expr::Kind::Int, token);
    }
    return node(Expr::Kind::Var, identifier());
  }

  std::vector<std::string> tokens_;
  std::size_t position_ = 0;
};

// ---- Algorithm W ----------------------------------------------------------------------------
class Inferencer {
 public:
  explicit Inferencer(std::vector<std::string>& steps) : steps_(steps) {
    const auto a = fresh();
    const auto b = fresh();
    builtin("fst", arrow(con("*", {a, b}), a));
    builtin("snd", arrow(con("*", {a, b}), b));
    builtin("nil", con("list", {a}));
    builtin("cons", arrow(a, arrow(con("list", {a}), con("list", {a}))));
    builtin("head", arrow(con("list", {a}), a));
    builtin("tail", arrow(con("list", {a}), con("list", {a})));
    builtin("isnil", arrow(con("list", {a}), con("bool")));
  }

  TypePtr infer(const Expr& expr, std::map<std::string, Scheme>& env) {
    switch (expr.kind) {
      case Expr::Kind::Int:
        return con("int");
      case Expr::Kind::Bool:
        return con("bool");
      case Expr::Kind::Var: {
        const auto found = env.find(expr.name);
        if (found != env.end()) {
          return instantiate(found->second);
        }
        const auto builtin_found = builtins_.find(expr.name);
        if (builtin_found != builtins_.end()) {
          return instantiate(builtin_found->second);
        }
        throw std::string("unbound variable '" + expr.name + "'");
      }
      case Expr::Kind::Fun: {
        const auto parameter = fresh();
        auto inner = env;
        inner[expr.name] = Scheme{{}, parameter};
        const auto body = infer(*expr.children[0], inner);
        return arrow(parameter, body);
      }
      case Expr::Kind::App: {
        const auto function = infer(*expr.children[0], env);
        const auto argument = infer(*expr.children[1], env);
        const auto result = fresh();
        unify(function, arrow(argument, result));
        return result;
      }
      case Expr::Kind::Let: {
        const auto bound = infer(*expr.children[0], env);
        auto inner = env;
        inner[expr.name] = generalize(bound, env);
        return infer(*expr.children[1], inner);
      }
      case Expr::Kind::LetRec: {
        const auto self = fresh();
        auto recursive = env;
        recursive[expr.name] = Scheme{{}, self};
        const auto bound = infer(*expr.children[0], recursive);
        unify(self, bound);
        auto inner = env;
        inner[expr.name] = generalize(bound, env);
        return infer(*expr.children[1], inner);
      }
      case Expr::Kind::If: {
        unify(infer(*expr.children[0], env), con("bool"));
        const auto then_type = infer(*expr.children[1], env);
        unify(then_type, infer(*expr.children[2], env));
        return then_type;
      }
      case Expr::Kind::BinOp: {
        const auto lhs = infer(*expr.children[0], env);
        const auto rhs = infer(*expr.children[1], env);
        if (expr.name == "=") {
          unify(lhs, rhs);
          return con("bool");
        }
        unify(lhs, con("int"));
        unify(rhs, con("int"));
        return expr.name == "<" ? con("bool") : con("int");
      }
      case Expr::Kind::Pair:
        return con("*", {infer(*expr.children[0], env), infer(*expr.children[1], env)});
    }
    throw std::string("unknown expression");
  }

  std::string show(const TypePtr& type) {
    std::map<int, std::string> names;
    return show(type, names, false);
  }

 private:
  TypePtr fresh() { return var(next_id_++); }

  void builtin(const std::string& name, const TypePtr& type) {
    builtins_[name] = generalize(type, {});
  }

  void free_variables(const TypePtr& raw, std::set<int>& out) {
    const auto type = prune(raw);
    if (type->kind == Type::Kind::Var) {
      out.insert(type->id);
      return;
    }
    for (const auto& arg : type->args) {
      free_variables(arg, out);
    }
  }

  Scheme generalize(const TypePtr& type, const std::map<std::string, Scheme>& env) {
    std::set<int> in_type;
    free_variables(type, in_type);
    std::set<int> in_env;
    for (const auto& [name, scheme] : env) {
      std::set<int> scheme_vars;
      free_variables(scheme.type, scheme_vars);
      for (const int id : scheme_vars) {
        if (!scheme.quantified.contains(id)) {
          in_env.insert(id);
        }
      }
    }
    Scheme scheme{{}, type};
    for (const int id : in_type) {
      if (!in_env.contains(id)) {
        scheme.quantified.insert(id);
      }
    }
    return scheme;
  }

  TypePtr instantiate(const Scheme& scheme) {
    std::map<int, TypePtr> mapping;
    for (const int id : scheme.quantified) {
      mapping[id] = fresh();
    }
    return copy(scheme.type, mapping);
  }

  TypePtr copy(const TypePtr& raw, std::map<int, TypePtr>& mapping) {
    const auto type = prune(raw);
    if (type->kind == Type::Kind::Var) {
      const auto found = mapping.find(type->id);
      return found == mapping.end() ? type : found->second;
    }
    std::vector<TypePtr> args;
    for (const auto& arg : type->args) {
      args.push_back(copy(arg, mapping));
    }
    return con(type->name, std::move(args));
  }

  bool occurs(int id, const TypePtr& raw) {
    const auto type = prune(raw);
    if (type->kind == Type::Kind::Var) {
      return type->id == id;
    }
    for (const auto& arg : type->args) {
      if (occurs(id, arg)) {
        return true;
      }
    }
    return false;
  }

  void unify(const TypePtr& lhs_raw, const TypePtr& rhs_raw) {
    const auto lhs = prune(lhs_raw);
    const auto rhs = prune(rhs_raw);
    if (steps_.size() < 200) {
      steps_.push_back("unify " + show(lhs) + "  ~  " + show(rhs));
    }
    if (lhs->kind == Type::Kind::Var) {
      if (rhs->kind == Type::Kind::Var && rhs->id == lhs->id) {
        return;
      }
      if (occurs(lhs->id, rhs)) {
        throw std::string("occurs check failed: cannot construct the infinite type " + show(lhs) + " = " + show(rhs));
      }
      lhs->instance = rhs;
      return;
    }
    if (rhs->kind == Type::Kind::Var) {
      unify(rhs, lhs);
      return;
    }
    if (lhs->name != rhs->name || lhs->args.size() != rhs->args.size()) {
      throw std::string("type mismatch: " + show(lhs) + " vs " + show(rhs));
    }
    for (std::size_t index = 0; index < lhs->args.size(); ++index) {
      unify(lhs->args[index], rhs->args[index]);
    }
  }

  std::string show(const TypePtr& raw, std::map<int, std::string>& names, bool parenthesize_arrow) {
    const auto type = prune(raw);
    if (type->kind == Type::Kind::Var) {
      auto [found, inserted] = names.try_emplace(type->id, "");
      if (inserted) {
        const std::size_t index = names.size() - 1;
        found->second = "'" + std::string(1, static_cast<char>('a' + static_cast<int>(index % 26))) +
                        (index >= 26 ? std::to_string(index / 26) : "");
      }
      return found->second;
    }
    if (type->name == "->") {
      const std::string text = show(type->args[0], names, true) + " -> " + show(type->args[1], names, false);
      return parenthesize_arrow ? "(" + text + ")" : text;
    }
    if (type->name == "*") {
      return "(" + show(type->args[0], names, true) + " * " + show(type->args[1], names, true) + ")";
    }
    if (type->name == "list") {
      return show(type->args[0], names, true) + " list";
    }
    return type->name;
  }

  std::vector<std::string>& steps_;
  std::map<std::string, Scheme> builtins_;
  int next_id_ = 0;
};

}  // namespace

InferenceResult infer_type(const std::string& source) {
  InferenceResult result;
  try {
    const ExprPtr expr = Parser(source).parse();
    Inferencer inferencer(result.steps);
    std::map<std::string, Scheme> env;
    const TypePtr type = inferencer.infer(*expr, env);
    result.type = inferencer.show(type);
    result.ok = true;
  } catch (const std::string& error) {
    result.error = error;
  }
  return result;
}

}  // namespace nexus::compiler::types
