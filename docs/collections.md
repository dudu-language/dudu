<a id="collections-and-literal-inference"></a>
# Collections And Literal Inference

[Dudu manual](https://dudulang.org/docs.html#containers) | Previous: [Appearance spec](appearance-spec.md) | Next: [Fixed arrays and numeric literals](fixed-arrays-and-numeric-literals.md)

Dudu's built-in dynamic collections lower to standard C++ containers:

| Dudu | C++ lowering | Meaning |
| --- | --- | --- |
| `list[T]` | `std::vector<T>` | Ordered, contiguous, owning sequence |
| `dict[K, V]` | `std::unordered_map<K, V>` | Owning hash map |
| `set[T]` | `std::unordered_set<T>` | Owning hash set |

## Literal Inference

A non-empty homogeneous literal supplies all generic arguments:

```python
numbers = [1, 2, 3]                 # list[i32]
scores = {"ada": 20, "bob": 22}    # dict[str, i32]
names = {"ada", "bob"}             # set[str]
groups = {"a": {"x": 1}}           # dict[str, dict[str, i32]]
```

Inference is recursive. A bare nested list remains a dynamic nested list:

```python
matrix = [[1, 2], [3, 4]]           # list[list[i32]]
```

It does not silently become fixed contiguous array storage. Use `array[T]`
when that storage choice is required.

## Expected Types

An annotation, parameter, or return type supplies context to every literal
element. This allows widths to be selected once:

```python
def fractions() -> list[f32]:
    return [1.0, 2.0, 3.0]

counts: dict[str, u64] = {"ready": 4, "done": 9}
offsets: set[usize] = {0, 8, 16}
```

Without context, integer literals default to `i32` and decimal literals
default to `f64`.

## Empty Collections

An empty literal has no element type. Give it an annotation or other expected
type:

```python
values: list[i32] = []
lookup: dict[str, i32] = {}
visited: set[str] = set[str]()
```

An unannotated empty binding is rejected at its declaration:

```python
values = []
# error: cannot infer type of empty collection literal; add an explicit
# list[T], dict[K, V], or set[T] annotation
```

Because `{}` is the empty mapping spelling in Python-shaped source, use an
explicit `set[T]()` to construct an empty set.

## Heterogeneous Values

Collections are homogeneous by default. A conflicting element is diagnosed at
that element:

```python
values = [1, "two"]
# error: list element type mismatch: expected i32, got str
```

Use `variant` when alternatives are intentional:

```python
values: list[variant[i32, str]] = [1, "two"]
```

## Inferred Types In Use

The inferred generic type is the normal local type. Indexing, iteration,
methods, function calls, and return checking use it directly:

```python
def first(values: list[i32]) -> i32:
    return values[0]

numbers = [1, 2, 3]
numbers.append(4)

total = 0
for number in numbers:
    total += number

answer = first(numbers)
```

As in Python, iterating a dictionary yields its keys:

```python
scores = {"ada": 20, "bob": 22}
for name in scores:                 # name: str
    print(name, scores[name])
```

The generated C++ iterates the underlying map entries directly. It does not
materialize a separate key collection.

Hover and inlay hints report complete types such as `list[i32]` and
`dict[str, list[f32]]`. Generated C++ declares the corresponding concrete
standard container rather than relying on C++ initializer-list inference.

## Native Containers

Built-in collection names are conveniences, not a boundary around the C++
ecosystem. Native containers can be imported and used directly:

```python
from cpp import deque
from cpp import map

queue: std.deque[i32]
ordered: std.map[str, i32]
```

Their APIs and ownership behavior remain the APIs and behavior of the imported
C++ types.

## Limits

- Built-in collections are homogeneous. Mixed values require an explicit
  `variant` element or value type.
- An empty literal cannot infer missing generic arguments.
- A list literal selects dynamic `list` storage. It never silently selects a
  fixed `array`.
- Native containers retain their own iterator, invalidation, allocation, and
  exception behavior.

## Tested Examples

- [`containers.dd`](../tests/fixtures/containers.dd) executes inferred list,
  dictionary, and set operations.
- [`nested_containers.dd`](../tests/fixtures/nested_containers.dd) checks
  recursive generic inference.
- [`bad_list_literal_type.dd`](../tests/fixtures/bad_list_literal_type.dd),
  [`bad_dict_literal_key_type.dd`](../tests/fixtures/bad_dict_literal_key_type.dd),
  and [`bad_set_literal_type.dd`](../tests/fixtures/bad_set_literal_type.dd)
  verify conflicting-element diagnostics.
