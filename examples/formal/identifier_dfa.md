# Identifier DFA

Phase 2 lexical analysis can be explained as a DFA.

## Identifier and Integer States

| State | Input class | Next state | Meaning |
| --- | --- | --- | --- |
| `start` | letter or `_` | `identifier` | begin identifier |
| `start` | digit | `integer` | begin integer literal |
| `identifier` | letter, digit, `_` | `identifier` | continue identifier |
| `integer` | digit | `integer` | continue integer literal |
| `identifier` | other | accept | emit identifier or keyword |
| `integer` | other | accept | emit integer literal |

## Worked Trace

Input: `sum4`

1. `start` sees `s` -> `identifier`
2. `identifier` sees `u` -> `identifier`
3. `identifier` sees `m` -> `identifier`
4. `identifier` sees `4` -> `identifier`
5. next input is delimiter -> accept `sum4`

Input: `123`

1. `start` sees `1` -> `integer`
2. `integer` sees `2` -> `integer`
3. `integer` sees `3` -> `integer`
4. end of token -> accept `123`
