; Baseline query file for a Dudu Tree-sitter grammar.
; Dudu is Python-shaped, so these node names intentionally mirror Python grammar
; names where possible.

[
  "def"
  "class"
  "if"
  "elif"
  "else"
  "while"
  "for"
  "in"
  "return"
  "break"
  "continue"
] @keyword

[
  "and"
  "or"
  "not"
] @keyword.operator

(identifier) @variable
(comment) @comment
(string) @string
(integer) @number
(float) @number.float
