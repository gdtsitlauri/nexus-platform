# Block PDA

Nested blocks and parenthesized expressions require stack discipline.

## Informal PDA View

- push a marker when the parser enters `(` or `{`
- parse nested content
- pop the marker when `)` or `}` is consumed
- reject if the wrong closing delimiter appears or the stack is empty too early

## Worked Example

Input:

```nexus
if (flag) {
  while (x < 4) {
    x = x + 1;
  }
}
```

Stack trace:

1. read `if (` -> push `(`
2. read `)` -> pop `(`
3. read `{` -> push `{`
4. read `while (` -> push `(`
5. read `)` -> pop `(`
6. read inner `{` -> push `{`
7. read inner `}` -> pop `{`
8. read outer `}` -> pop `{`

This is why nested language structure cannot be modeled by a finite automaton alone.
