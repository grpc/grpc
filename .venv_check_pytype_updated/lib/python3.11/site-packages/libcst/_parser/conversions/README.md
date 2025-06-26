# Parser Conversions Developer Guide

Parser conversions take grammar productions and convert them to CST nodes, or to some
"partial" value that will later be converted to a CST node.

The grammar production that parser conversions are associated with is co-located
alongside the conversion function using our `@with_production` decorator. This is
similar to the API that [rply](https://github.com/alex/rply/) uses.

Grammar productions are collected when the parser is first called, and converted into a
state machine by Parso's pgen2 fork.

Unlike rply's API, productions are not automatically gathered, because that would be
dependent on implicit import-time side-effects. Instead all conversion functions must be
listed in `_grammar.py`.

# What's a production?

A production is a line in our BNF-like grammar definition. A production has a name (the
first argument of `@with_production`), and a sequence of children (the second argument
of `@with_production`).

Python's full grammar is here: https://docs.python.org/3/reference/grammar.html

We use Parso's fork of pgen2, and therefore support the same BNF-like syntax that
Python's documentation uses.

# Why is everything `Any`-typed? Isn't that bad?

Yes, `Any` types indicate a gap in static type coverage. Unfortunately, this isn't
easily solved.

The value of `children` given to a conversion function is dependent on textual grammar
representation and pgen2's implementation, which the type system is unaware of. Unless
we extend the type system to support pgen2 (unlikely) or add a layer of
machine-generated code (possible, but we're not there), there's no way for the type
system to validate any annotations on `children`.

We could add annotations to `children`, but they're usually complicated types (so they
wouldn't be very human-readable), and they wouldn't actually provide any type safety
because the type checker doesn't know about them.

Similarly, we could annotate return type annotations, but that's just duplicating the
type we're already expressing in our return statement (so it doesn't improve readability
much), and it's not providing any static type safety.

We do perform runtime type checks inside tests, and we hope that this test coverage will
help compensate for the lack of static type safety.

# Where's the whitespace?

The most important differentiation between an Abstract Syntax Tree and a Concrete Syntax
Tree (for our purposes) is that the CST contains enough information to exactly reproduce
the original program. This means that we must somehow capture and store whitespace.

The grammar does not contain whitespace information, and there are no explicit tokens
for whitespace. If the grammar did contain whitespace information, the grammar likely
wouldn't be LL(1), and while we could use another context free grammar parsing
algorithm, it would add complexity and likely wouldn't be as efficient.

Instead, we have a hand-written re-entrant recursive-descent parser for whitespace. It's
the responsibility of conversion functions to call into this parser given whitespace
states before and after a token.

# Token and WhitespaceState Data Structures

A token is defined as:

```
class Token:
    type: TokenType
    string: str
    # The start of where `string` is in the source, not including leading whitespace.
    start_pos: Tuple[int, int]
    # The end of where `string` is in the source, not including trailing whitespace.
    end_pos: Tuple[int, int]
    whitespace_before: WhitespaceState
    whitespace_after: WhitespaceState
```

Or, in the order that these pieces appear lexically in a parsed program:

```
+-------------------+--------+-------------------+
| whitespace_before | string | whitespace_after  |
| (WhitespaceState) | (str)  | (WhitespaceState) |
+-------------------+--------+-------------------+
```

Tokens are immutable, but only shallowly, because their whitespace fields are mutable
WhitespaceState objects.

WhitespaceStates are opaque objects that the whitespace parser consumes and mutates.
WhitespaceState nodes are shared across multiple tokens, so `whitespace_after` is the
same object as `whitespace_before` in the next token.

# Parser Execution Order

The parser generator we use (`pgen2`) is bottom-up, meaning that children productions
are called before their parents. In contrast, our hand written whitespace parser is
top-down.

Inside each production, child conversion functions are called from left to right.

As an example, assume we're given the following simple grammar and program:

```
add_expr: NUMBER ['+' add_expr]
```

```
1 + 2 + 3
```

which forms the parse tree:

```
     [H] add_expr
    /      |      \
[A] 1   [B] '+'   [G] add_expr
                 /      |      \
             [C] 2   [D] '+'   [F] add_expr
                                    |
                                  [E] 3
```

The conversion functions would be called in the labeled alphabetical order, with `A`
converted first, and `H` converted last.

# Who owns whitespace?

There's a lot of holes between you and a correct whitespace representation, but these
can be divided into a few categories of potential mistakes:

## Forgetting to Parse Whitespace

Fortunately, the inverse (parsing the same whitespace twice) should not be possible,
because whitespace is "consumed" by the whitespace parser.

This kind of mistake is easily caught with tests.

## Assigning Whitespace to the Wrong Owner

This is probably the easiest mistake to make. The general convention is that the
top-most possible node owns whitespace, but in a bottom-up parser like ours, the
children are parsed before their parents.

In contrast, the best owner for whitespace in our tree when there's multiple possible
owners is usually the top-most node.

As an example, assume we have the following grammar and program:

```
simple_stmt: (pass_stmt ';')* NEWLINE
```

```
pass; # comment
```

Since both `cst.Semicolon` and `cst.SimpleStatement` can both store some whitespace
after themselves, there's some ambiguity about who should own the space character before
the comment. However, since `cst.SimpleStatement` is the parent, the convention is that
it should own it.

Unfortunately, since nodes are processed bottom-to-top and left-to-right, the semicolon
under `simple_stmt` will get processed before `simple_stmt` is. This means that in a
naive implementation, the semicolon's conversion function would have a chance to consume
the whitespace before `simple_stmt` can.

To solve this problem, you must "fix" the whitespace in the parent node's conversion
function or grammar. This can be done in a number of ways. In order of preference:

1. Split the child's grammar production into two separate productions, one that consumes
   it's leading or trailing whitespace, and one that doesn't. Depending on the parent,
   use the appropriate version of the child.
2. Construct a "partial" node in the child that doesn't consume the whitespace, and then
   consume the correct whitespace in the parent. Be careful about what whitespace a
   node's siblings consume.
3. "Steal" the whitespace from the child by replacing the child with a new version that
   doesn't have the whitespace.

This mistake is probably hard to catch with tests, because the CST will still reprint
correctly, but it creates ergonomic issues for tools consuming the CST.

## Consuming Whitespace in the Wrong Order

This mistake is probably is the hardest to make by accident, but it could still happen,
and may be hard to catch with tests.

Given the following piece of code:

```
pass # trailing
# empty line
pass
```

The first statement should own `# trailing` (parsed using `parse_trailing_whitespace`).
The second statement then should `# empty line` (parsed using `parse_empty_lines`).

However, it's possible that if you somehow called `parse_empty_lines` on the second
statement before calling `parse_trailing_whitespace` on the first statement,
`parse_empty_lines` could accidentally end up consuming the `# trailing` comment,
because `parse_trailing_whitespace` hasn't yet consumed it.

However, this circumstance is unlikely, because you'd explicitly have to handle the
children out-of-order, and we have assertions inside the whitespace parser to prevent
some possible mistakes, like the one described above.
