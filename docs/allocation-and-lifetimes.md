<a id="allocation-and-lifetimes"></a>
# Allocation And Lifetimes

[Dudu manual](https://dudulang.org/docs.html#memory) | Previous: [Native templates and macros](native-templates-and-macros.md) | Next: [Arrays, views, and indexing](arrays-views-and-indexing.md)

Dudu keeps C++'s value and RAII model while exposing raw allocation for systems
code. It does not impose ownership checking, borrow checking, a garbage
collector, or one allocator interface.

## Value Construction

Calling a type constructs a value. It does not allocate that value separately
on the heap:

```python
class Player:
    hp: i32
    name: str


def make_player() -> Player:
    return Player(100, "ada")


player = make_player()
```

The returned `Player` owns its fields. Normal C++ copy elision, moves, and
destruction apply to the generated value.

## Heap Construction

`new[T](...)` is function-shaped language syntax. The compiler validates `T`,
checks constructor arguments, emits `new T(...)`, and infers `*T`:

```python
enemy: *Enemy = new[Enemy](100, 10.0, 20.0)
enemy.hp -= 10
delete enemy
```

`delete` destroys the object and releases storage allocated by `new[T]`.
Pairing `new[T]` with `free`, or `malloc[T]` with `delete`, is invalid even
though both APIs use raw pointers.

## Raw Storage

`malloc[T](count)` is also compiler-defined function-shaped syntax. It emits a
typed cast around enough raw storage for `count` values of `T`:

```python
bytes: *u8 = malloc[u8](4096)
free(bytes)
```

`malloc[T]` does not construct `T`. For nontrivial objects, construct and
destroy them explicitly before freeing the storage:

```python
from cpp import memory

storage: *Enemy = malloc[Enemy](1)
enemy: *Enemy = std.construct_at(storage, Enemy(40, 2.0))

use_enemy(enemy)

std.destroy_at(enemy)
free(storage)
```

This is ordinary C++20 placement construction through the imported standard
library. Native allocator libraries may expose equivalent APIs.

## Value-Owning Containers

`list[T]` owns its elements and lowers to a dynamic C++ value container. A
returned list carries its storage with it:

```python
def make_players() -> list[Player]:
    players = [Player(100, "ada"), Player(80, "lin")]
    return players
```

Imported RAII containers work normally too:

```python
from cpp import memory

player: std.unique_ptr[Player] = std.make_unique[Player](100, "ada")
```

Destructors run at lexical scope exit. Dudu does not require manual cleanup for
objects that own resources through their value type.

## Pointer Containers

A container of pointers owns the pointer values, not the pointees:

```python
borrowed: list[*Player] = []
borrowed.append(player_ptr)
```

Destroying `borrowed` does not delete `player_ptr`. The producer and consumer
must agree on pointee lifetime and cleanup.

## Fixed Arrays And Dynamic Lists

Fixed arrays have inline, compile-time storage:

```python
matrix: array[f32][4, 4]
```

Dynamic lists own resizable storage:

```python
samples: list[f32] = []
samples.append(1.0)
```

Use a fixed array when rank and extents are part of the value's static layout.
Use a list when the count changes at runtime. Specialized device, arena, or
tensor storage belongs in a library type whose API states its ownership model.

## Custom Allocators And Arenas

Allocator methods are normal library methods:

```python
enemy = arena.make[Enemy](100, 10.0, 20.0)
bytes = arena.alloc[u8](4096)
arena.reset()
```

The compiler does not assign ownership meaning to `arena.make`, `arena.alloc`,
or `arena.reset`. Their library contract determines whether a result is owned,
borrowed, relocatable, or invalidated by reset.

An allocator can return an owning RAII value instead of a raw pointer:

```python
class OwnedBytes:
    data: *u8
    count: usize

    def drop(self):
        free(self.data)


class HeapAllocator:
    def make_bytes(self, count: usize) -> OwnedBytes:
        return OwnedBytes(malloc[u8](count), count)
```

`OwnedBytes.drop` defines destruction for the library type. The allocator name
itself has no compiler privilege.

An arena reset is an explicit lifetime boundary:

```python
from cpp import memory

raw = arena.alloc(sizeof[Enemy](), alignof[Enemy]())
enemy: *Enemy = *Enemy(raw)
std.construct_at(enemy, Enemy(50, 4.0, 8.0))

use_enemy(enemy)
std.destroy_at(enemy)
arena.reset()

# enemy is invalid after reset
```

## Native Allocation Pairs

Native APIs keep their own allocation contracts. Use the matching cleanup
function from the same API:

```python
from cxx import libxml/parser.h as xml

doc = xml.xmlReadMemory(source, source_len, "input.xml", None, 0)
root = xml.xmlDocGetRootElement(doc)
text = xml.xmlNodeGetContent(root)

consume(text)

xml.xmlFree(text)
xml.xmlFreeDoc(doc)
```

Dudu's `free` is not a replacement for `xmlFree`, `SDL_free`, Vulkan destroy
functions, or another library's documented release operation.

## Diagnosed Local Escapes

Dudu rejects direct, statically visible escapes of local addresses:

```python
def bad() -> *i32:
    value = 1
    return &value  # error: cannot let local address escape: value
```

It also rejects placing a local address into a returned pointer container:

```python
def bad() -> list[*i32]:
    value = 1
    out: list[*i32] = []
    out.append(&value)  # error
    return out
```

This catches direct mistakes; it is not Rust-style lifetime proof. Raw pointers,
native calls, casts, custom allocators, and complex aliasing remain the
programmer's responsibility.

## Tested Examples

- [`tests/fixtures/allocation.dd`](../tests/fixtures/allocation.dd): `new`,
  `delete`, `malloc`, and `free`
- [`tests/fixtures/allocation_native_interop.dd`](../tests/fixtures/allocation_native_interop.dd):
  placement construction and destruction
- [`tests/fixtures/custom_allocator_raii.dd`](../tests/fixtures/custom_allocator_raii.dd):
  allocator returning an owning RAII value
- [`tests/fixtures/arena_allocator.dd`](../tests/fixtures/arena_allocator.dd): raw arena storage and reset
- [`tests/fixtures/bad_return_local_address.dd`](../tests/fixtures/bad_return_local_address.dd):
  rejected local pointer escape

## Limits

- Dudu diagnoses direct local-address escapes but does not prove arbitrary
  pointer lifetimes or aliasing safety.
- Raw allocation pairs, placement construction, custom arenas, and imported
  native handles remain the programmer's responsibility.
- Pointer containers do not acquire ownership of their pointees.
- Dudu does not add garbage collection, borrow checking, or a mandatory
  allocator protocol.
