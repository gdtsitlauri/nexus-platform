# Type Inference (Hindley-Milner)

`src/compiler/types/` implements Damas-Milner Algorithm W for a small ML-like language:

```text
e ::= n | true | false | x | fun x -> e | e e | let x = e in e | let rec f = e in e
    | if e then e else e | e + e | e - e | e * e | e < e | e = e | (e, e)
```

with built-in polymorphic constants `fst`, `snd`, `nil`, `cons`, `head`, `tail`, `isnil`.
`let f x y = e` is sugar for `let f = fun x -> fun y -> e`.

The implementation uses union-find type variables, unification with the occurs check,
generalisation of let-bound definitions over the variables not free in the environment, and
instantiation with fresh variables.

```bash
./build/bin/nexusc infer "fun f -> fun g -> fun x -> f (g x)"
('a -> 'b) -> ('c -> 'a) -> 'c -> 'b
./build/bin/nexusc infer "let rec map f l = if isnil l then nil else cons (f (head l)) (map f (tail l)) in map"
('a -> 'b) -> 'a list -> 'b list
./build/bin/nexusc infer "let id = fun x -> x in (id 1, id true)"
(int * bool)
./build/bin/nexusc infer "(fun id -> (id 1, id true)) (fun x -> x)"
type error: type mismatch: int vs bool            # lambda-bound variables are monomorphic
./build/bin/nexusc infer "fun x -> x x"
type error: occurs check failed: cannot construct the infinite type 'a = 'a -> 'b
```

`--trace` prints every unification step. `nexus_type_inference_test` checks ten principal types and
five rejections.
