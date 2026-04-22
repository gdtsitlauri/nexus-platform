# Scope Graph and Hash Tables

Phase 2 semantic analysis uses one hash-table-like map per scope and walks outward when resolving a
name.

## Worked Example

```nexus
fn main() -> int {
  var x: int = 1;
  {
    var y: int = x + 1;
    {
      var x: int = y + 2;
      return x;
    }
  }
}
```

## Scope Tree

```text
function scope
└── block scope #1
    └── block scope #2
```

## Symbol Tables

- function scope: `x -> int`
- block scope #1: `y -> int`
- block scope #2: `x -> int`

## Traversal and Closure

Lookup of `y` inside block scope #2:

1. current scope does not contain `y`
2. parent scope does contain `y`
3. lookup succeeds through transitive parent closure

Lookup of `x` inside block scope #2:

1. current scope contains `x`
2. lookup stops immediately
3. outer `x` is shadowed
