# Macro Syntax Plan

Dudu should have macros only when they make real code nicer. The first design
question is what macros should look like in user code, not how the macro engine
works internally.

The target feel is Python-shaped decorators and ordinary calls. The target power
is enough to support serde-like derives, bindings, tests, reflection metadata,
and small code-generation helpers without turning normal code into a macro
language.

Serde, third-party adapters, and protocol-shaped abstract classes are discussed
in [Protocols And Serde Design Notes](protocols-serde-design-notes.md). That
document records candidate syntax and tradeoffs without making them language
features.

## Principles

- Normal code should look like normal Dudu.
- Macros should be visible at the call site.
- Attribute/decorator macros should be preferred for declaration transforms.
- Expression macros should look like calls.
- C/C++ macros remain imported native macros, not Dudu macro syntax.
- Macro expansion diagnostics must point at both the macro use and generated
  code when useful.
- Macros should operate on AST nodes where possible.

Status: decorators keep a parsed expression node alongside their original text.
Compiler-recognized decorators use a shared helper over that parsed expression
for name matching and first-argument extraction. The parser also has regression
coverage for decorator string arguments containing operator characters, such as
`@operator("+")`, so macro/decorator work has a structured input shape instead
of relying on raw decorator text slicing. String-valued compiler decorators
such as `@operator(...)`, `@section(...)`, and
`@test.should_panic(...)` require exactly one parsed string literal argument,
so raw identifier arguments such as `@operator(add)` are rejected in Dudu source
instead of being treated like operator names. Expression-valued decorators such
as `@align(16)` and `@workgroup_size(8, 8, 1)` remain parsed expression
arguments, not strings.

## Decorator Macros

Declaration macros use Python-style decorators.

```python
@derive(Debug, Json)
class Player:
    id: u64
    name: str
    hp: i32
```

This should be able to generate methods or helper functions.

Serde-like target:

```python
@derive(Json)
class Vec2:
    x: f32
    y: f32

@derive(Json)
class Player:
    id: u64
    name: str
    pos: Vec2
```

Possible generated surface:

```python
text: str = json.to_string(player)
loaded: Result[Player, json.Error] = json.from_string[Player](text)
```

## Field Attributes

Fields need lightweight metadata.

```python
@derive(Json)
class Player:
    id: u64

    @json.name("displayName")
    name: str

    @json.skip
    cached_score: i32
```

Target uses:

- JSON field rename
- skip serialization
- default value
- range validation
- editor/UI metadata
- binary layout metadata

## Enum Derives

```python
@derive(Debug, StringEnum)
enum Direction:
    North
    South
    East
    West
```

Target generated helpers:

```python
name: str = Direction.to_string(Direction.North)
dir: Option[Direction] = Direction.from_string("North")
```

## Test Macros

Built-in decorators already cover the normal test path:

```python
@test
def add_works():
    assert add(2, 3) == 5
```

Parameterized test generation is not a priority. Normal Dudu code can already
express table-driven tests clearly:

```python
@test
def add_cases():
    cases: list[tuple[i32, i32, i32]] = [
        (0, 0, 0),
        (2, 3, 5),
        (-1, 1, 0),
    ]
    for case: tuple[i32, i32, i32] in cases:
        assert add(case._0, case._1) == case._2
```

Macros should only enter test generation if a real need appears that ordinary
test helpers cannot handle, such as generating cross-product fixtures with
stable test names.

## C/C++ Export Surface

C and C++ export shape should mostly be normal Dudu language design, not macro
magic.

C ABI exports are already a decorator-shaped language feature:

```python
@extern_c
def dudu_player_update(player: *Player, dt: f32):
    player.hp += i32(dt)
```

Bare `@extern_c` should export the Dudu function name. If the ABI needs a
different symbol, use an explicit export name:

```python
@extern_c("dudu_player_update")
def update_player(player: *Player, dt: f32):
    player.hp += i32(dt)
```

That should lower to a stable exported C symbol while keeping the Dudu name nice
inside the module. Export-name overrides should be restricted to ABI/export
decorators, not become a general macro pattern.

C++ namespace exports should come from modules or an explicit namespace form,
not from a macro pretending to be binding glue. If Dudu needs explicit
namespaces, the syntax should be direct and indented:

```python
namespace game:
    class PlayerApi:
        def spawn(name: str) -> Player:
            ...
```

Macro involvement should be limited to cases where repetitive glue is genuinely
being generated from declarations, such as reflecting a class into a plugin API
table or exporting a batch of related functions with a naming convention.

## Reflection Metadata

Dudu should not require runtime reflection for everything, but compile-time
metadata is useful.

```python
@reflect
class Transform:
    position: Vec3[f32]
    rotation: Quat[f32]
    scale: Vec3[f32]
```

Target uses:

- editor inspectors
- debug UI
- save/load
- network replication metadata
- schema export

## Binary Serialization

Systems code needs explicit layout.

```python
@binary.little_endian
@binary.packed
class PacketHeader:
    magic: u32
    version: u16
    flags: u16
```

Target helpers:

```python
header: Result[PacketHeader, binary.Error] = binary.read[PacketHeader](bytes)
binary.write(out, header)
```

## Command And CLI Derives

```python
@derive(Args)
class Options:
    input: str

    @arg.short("o")
    output: str

    @arg.default(False)
    verbose: bool
```

Target use:

```python
opts: Result[Options, args.Error] = args.parse[Options](argc, argv)
```

## GPU And Kernel Metadata

Dudu already has target decorators. Macro syntax should be compatible with this
style.

```python
@cuda.global
@workgroup_size(16, 16, 1)
def blur_kernel(input: *device[f32], output: *device[f32], width: i32, height: i32):
    ...
```

```python
@shader.compute
@workgroup_size(8, 8, 1)
def main():
    ...
```

## Expression Macros

Expression macros should look like calls, not new punctuation.

```python
log.debug("player hp", player.hp)
defer(file.close())
using(lock_guard(mutex)):
    update_shared_state()
```

These need careful design because they alter control flow or lifetime.
Declaration macros are safer and more important.

## Macro Definitions

Macro definition syntax is not final. This section is a candidate design to
evaluate once the AST exists.

The most Dudu-shaped option is that macros are compile-time Dudu functions over
compiler AST types. They should look like normal Dudu, but run in the
compiler's expansion environment instead of the runtime program.

```python
import dudu.ast

@macro
def derive_json(item: ast.ClassDecl, args: ast.DecoratorArgs) -> ast.Items:
    out: ast.Items = []
    out.append(make_to_json(item))
    out.append(make_from_json(item))
    return out
```

Macro names come from imported macro modules:

```python
from macros.json import derive_json

@derive_json
class Player:
    id: u64
    name: str
```

Built-in macro groups can provide nicer aliases:

```python
from dudu.derive import derive

@derive(Json, Debug)
class Player:
    id: u64
    name: str
```

That grouped form is sugar for invoking registered derive macros separately.

## Macro Modules

Macro definitions should live in ordinary `.dd` modules that are compiled for
the compile-time environment:

```text
src/
    main.dd
macros/
    json.dd
    debug.dd
```

Example `macros/debug.dd`:

```python
import dudu.ast

@macro
def Debug(item: ast.ClassDecl) -> ast.Items:
    method: ast.FunctionDecl = ast.method(
        name="debug_string",
        params=[],
        return_type=ast.named_type("str"),
        body=debug_body(item),
    )
    return [method]
```

Macro modules can import normal Dudu helper modules if those helpers are
compile-time-safe:

```python
import dudu.ast
from macros.naming import snake_to_camel

@macro
def Json(item: ast.ClassDecl) -> ast.Items:
    fields: list[ast.FieldDecl] = item.fields
    return [
        make_json_to_value(item.name, fields),
        make_json_from_value(item.name, fields),
    ]
```

Macro modules should not silently depend on runtime global state. Compile-time
I/O, environment access, clock reads, random numbers, and subprocesses should be
explicit capabilities so expansion remains deterministic by default.

## Macro Categories

Candidate categories:

- declaration macro: declaration in, declarations out
- decorator macro: declaration plus decorator args in, declaration/items out
- field attribute macro: field metadata in, enclosing declaration metadata out
- expression macro: expression in, expression out

Initial implementation should prefer declaration and decorator macros.
Expression macros are more likely to create surprising control flow and type
checking problems.

## AST Builder Style

The lowest-risk API is explicit AST builders:

```python
@macro
def derive_debug(item: ast.ClassDecl) -> ast.Items:
    body: ast.Block = ast.block()
    body.add(ast.var("out", ast.type("str"), ast.string(item.name + "(")))

    first: bool = True
    for field: ast.FieldDecl in item.fields:
        if not first:
            body.add(ast.assign("out", ast.call("str.concat", [ast.name("out"), ast.string(", ")])))
        first = False
        body.add(ast.assign(
            "out",
            ast.call("str.concat", [
                ast.name("out"),
                ast.string(field.name + "="),
                ast.call("str", [ast.member(ast.name("self"), field.name)]),
            ]),
        ))

    body.add(ast.assign("out", ast.call("str.concat", [ast.name("out"), ast.string(")")]))
    body.add(ast.return_(ast.name("out")))

    return [
        ast.method(
            name="debug_string",
            params=[],
            return_type=ast.type("str"),
            body=body,
        )
    ]
```

A higher-level quote API can make common macros less painful:

```python
@macro
def Debug(item: ast.ClassDecl) -> ast.Items:
    fields: list[ast.FieldDecl] = item.fields

    return ast.items:
        def debug_string(self) -> str:
            out: str = "{item.name}("
            ast.splice(debug_fields(fields))
            out += ")"
            return out
```

The exact quote syntax is undecided. The important idea is that quote blocks
produce AST nodes, and `ast.splice(...)` inserts generated AST nodes. If quote
syntax is too magical, builders are enough for the baseline design.

## Example: Debug Derive

User code:

```python
from dudu.derive import derive

@derive(Debug)
class Player:
    id: u64
    name: str
    hp: i32
```

Generated Dudu-shaped surface:

```python
class Player:
    id: u64
    name: str
    hp: i32

    def debug_string(self) -> str:
        return "Player(id=" + str(self.id) + ", name=" + self.name + ", hp=" + str(self.hp) + ")"
```

Macro definition sketch:

```python
import dudu.ast

@macro
def Debug(item: ast.ClassDecl) -> ast.Items:
    if not item.is_class():
        ast.error(item.range, "Debug can only derive on classes")

    parts: list[ast.Expr] = []
    parts.append(ast.string(item.name + "("))
    for i: usize, field: ast.FieldDecl in enumerate(item.fields):
        if i != 0:
            parts.append(ast.string(", "))
        parts.append(ast.string(field.name + "="))
        parts.append(ast.call("str", [ast.member(ast.name("self"), field.name)]))
    parts.append(ast.string(")"))

    return [
        ast.method(
            name="debug_string",
            params=[],
            return_type=ast.type("str"),
            body=ast.block([
                ast.return_(ast.concat(parts)),
            ]),
        )
    ]
```

## Example: Json Derive

User code:

```python
from dudu.derive import derive

@derive(Json)
class Player:
    id: u64

    @json.name("displayName")
    name: str

    @json.skip
    cached_score: i32
```

Target generated surface:

```python
namespace json:
    def to_value(value: Player) -> Value:
        out: Object = Object()
        out.set("id", json.to_value(value.id))
        out.set("displayName", json.to_value(value.name))
        return out
```

Macro definition sketch:

```python
import dudu.ast

@macro
def Json(item: ast.ClassDecl) -> ast.Items:
    to_body: ast.Block = ast.block()
    to_body.add(ast.var("out", ast.type("json.Object"), ast.call("json.Object", [])))

    for field: ast.FieldDecl in item.fields:
        if field.has_attr("json.skip"):
            continue
        json_name: str = field.attr_string("json.name", field.name)
        to_body.add(ast.expr(ast.call("out.set", [
            ast.string(json_name),
            ast.call("json.to_value", [ast.member(ast.name("value"), field.name)]),
        ])))

    to_body.add(ast.return_(ast.name("out")))

    return [
        ast.function(
            namespace="json",
            name="to_value",
            params=[ast.param("value", ast.type(item.name))],
            return_type=ast.type("json.Value"),
            body=to_body,
        ),
        make_json_from_value(item),
    ]
```

Required diagnostics:

- `Json` on a type with unsupported field type
- duplicate `@json.name`
- `@json.skip` on a method
- generated `from_value` missing a required field

## Example: Binary Layout Macro

User code:

```python
@binary.packed
@binary.little_endian
class PacketHeader:
    magic: u32
    version: u16
    flags: u16
```

Target generated helpers:

```python
def read_packet_header(bytes: span[u8]) -> Result[PacketHeader, binary.Error]:
    if len(bytes) < sizeof[PacketHeader]():
        return Err(binary.Error.ShortRead)
    header: PacketHeader = PacketHeader()
    header.magic = binary.read_u32_le(bytes, 0)
    header.version = binary.read_u16_le(bytes, 4)
    header.flags = binary.read_u16_le(bytes, 6)
    return Ok(header)

def write_packet_header(out: &list[u8], value: PacketHeader):
    binary.write_u32_le(out, value.magic)
    binary.write_u16_le(out, value.version)
    binary.write_u16_le(out, value.flags)
```

Macro definition sketch:

```python
@macro
def packed(item: ast.ClassDecl) -> ast.Items:
    for field: ast.FieldDecl in item.fields:
        if not binary.is_fixed_width(field.type):
            ast.error(field.range, "packed binary fields must be fixed-width values")
    return [
        ast.layout_attr(item.name, packed=True),
        binary.make_reader(item),
        binary.make_writer(item),
    ]
```

This is a real systems use case where macros are better than handwritten
boilerplate, but the generated code is still normal Dudu/C++.

## Example: CLI Args Derive

User code:

```python
@derive(Args)
class Options:
    input: str

    @arg.short("o")
    output: str

    @arg.default(False)
    verbose: bool
```

Target generated use:

```python
opts: Result[Options, args.Error] = args.parse[Options](argc, argv)
```

Macro definition sketch:

```python
@macro
def Args(item: ast.ClassDecl) -> ast.Items:
    parser_body: ast.Block = ast.block()
    parser_body.add(ast.var("opts", ast.type(item.name), ast.call(item.name, [])))
    parser_body.add(ast.var("i", ast.type("i32"), ast.int(1)))
    parser_body.add(ast.while_(ast.binary(ast.name("i"), "<", ast.name("argc")),
                               args_parse_loop(item)))
    parser_body.add(ast.return_(ast.call("Ok", [ast.name("opts")])))

    return [
        ast.function(
            namespace="args",
            name="parse",
            params=[ast.param("argc", ast.type("i32")), ast.param("argv", ast.type("**cstr"))],
            return_type=ast.type("Result[" + item.name + ", args.Error]"),
            body=parser_body,
        )
    ]
```

This stresses decorator args, field metadata, generated control flow, and
diagnostics for unsupported field types.

## Example: Reflection Metadata

User code:

```python
@reflect
class Transform:
    position: Vec3[f32]
    rotation: Quat[f32]
    scale: Vec3[f32]
```

Target generated metadata:

```python
namespace reflect:
    TRANSFORM: TypeInfo = TypeInfo(
        name="Transform",
        fields=[
            FieldInfo("position", type_id[Vec3[f32]](), offsetof[Transform]("position")),
            FieldInfo("rotation", type_id[Quat[f32]](), offsetof[Transform]("rotation")),
            FieldInfo("scale", type_id[Vec3[f32]](), offsetof[Transform]("scale")),
        ],
    )
```

Macro definition sketch:

```python
@macro
def reflect(item: ast.ClassDecl) -> ast.Items:
    fields: list[ast.Expr] = []
    for field: ast.FieldDecl in item.fields:
        fields.append(ast.call("reflect.FieldInfo", [
            ast.string(field.name),
            ast.call_template("reflect.type_id", [field.type], []),
            ast.call_template("offsetof", [ast.type(item.name)], [ast.string(field.name)]),
        ]))

    return [
        ast.const(
            namespace="reflect",
            name=item.name.upper(),
            type=ast.type("reflect.TypeInfo"),
            value=ast.call("reflect.TypeInfo", [
                ast.named_arg("name", ast.string(item.name)),
                ast.named_arg("fields", ast.list(fields)),
            ]),
        )
    ]
```

## Example: C Export Table

This is the kind of native interop glue where a macro can help without hiding
basic ABI rules.

User code:

```python
@plugin.exports(prefix="dudu")
class PluginApi:
    def init(app: *App) -> i32:
        return 0

    def tick(app: *App, dt: f32):
        update(app, dt)
```

Target generated exports:

```python
@extern_c("dudu_init")
def plugin_api_init(app: *App) -> i32:
    return PluginApi.init(app)

@extern_c("dudu_tick")
def plugin_api_tick(app: *App, dt: f32):
    PluginApi.tick(app, dt)
```

Macro definition sketch:

```python
@macro
def exports(item: ast.ClassDecl, args: ast.DecoratorArgs) -> ast.Items:
    prefix: str = args.string("prefix", "")
    out: ast.Items = []
    for method: ast.FunctionDecl in item.methods:
        if method.has_self_param():
            ast.error(method.range, "plugin exports require class-scoped methods with no self")
        symbol: str = prefix + "_" + method.name
        out.append(ast.function(
            name=item.name.lower() + "_" + method.name,
            decorators=[ast.decorator("extern_c", [ast.string(symbol)])],
            params=method.params,
            return_type=method.return_type,
            body=ast.block([
                ast.return_or_expr(ast.call(item.name + "." + method.name,
                                            ast.param_names(method.params))),
            ]),
        ))
    return out
```

This keeps `@extern_c("name")` as the ABI primitive and uses the macro only to
generate repetitive wrappers.

## Expansion Order

Candidate order:

1. Parse modules into AST.
2. Resolve imports enough to locate macro definitions.
3. Compile or interpret macro modules in the compile-time environment.
4. Expand declaration macros.
5. Re-run name resolution on the expanded module.
6. Type-check normal Dudu code.
7. Expand expression macros when the language supports them.
8. Type-check generated expression output.
9. Emit C++.

Declaration macros should run before full type checking because they can add
members, functions, constants, and namespaces. Macro definitions themselves
should be type-checked before they can run.

## Macro Diagnostics

Macro errors should look like normal compiler errors with expansion context:

```text
src/player.dd:4:1: error[dudu/macro]: Json cannot derive field `socket`
@derive(Json)
^^^^^^^^^^^^^
note: field declared here with unsupported type TcpSocket
    socket: TcpSocket
    ^^^^^^
note: error came from macro `Json`
macros/json.dd:18:13:
        ast.error(field.range, "Json cannot derive field `" + field.name + "`")
        ^^^^^^^^^
```

Generated-code diagnostics should include the macro expansion chain:

```text
src/player.dd:3:1: error[dudu/type-mismatch]: generated `to_value` returns json.Object, expected json.Value
@derive(Json)
^^^^^^^^^^^^^
note: generated function `json.to_value(Player)` came from this derive
```

## Macro Caching

Macro expansion cache keys should include:

- macro definition source hash
- macro module imports
- Dudu compiler version
- build configuration values visible to macros
- source hash of the item being expanded
- native header scan hashes if the macro inspects native symbols

Macro output should be inspectable. The compiler should support commands like:

```text
duc expand src/main.dd
duc expand src/main.dd --macro Json
```

Those commands should show generated Dudu-shaped code, not only generated C++.

## Compile-Time Environment

Macro code should have a smaller environment than runtime Dudu:

- AST API
- string/list/dict/set helpers
- deterministic formatting helpers
- diagnostic helpers
- optional access to resolved symbol/type metadata

Restricted by default:

- filesystem writes
- arbitrary subprocesses
- clock/time
- random numbers
- network access

If those capabilities are added, they should be explicit in project config so
macro expansion remains understandable and cacheable.

Open questions:

- Whether macro definitions must live in separate compile-time modules.
- Whether macros can call arbitrary Dudu code at compile time.
- Whether macros can use imported C++ libraries while expanding.
- Whether macro expansion is interpreted by the compiler or compiled and loaded
  as a native plugin.
- How macro expansion output is cached and invalidated.
- How generated items are shown in LSP hovers, go-to-definition, and diagnostics.

Constraints:

- Macro inputs and outputs should be typed AST values.
- Macro expansion should be deterministic for the same source and build config.
- Macro diagnostics must point at the macro use and the macro definition.
- Macro-generated names must participate in normal name resolution.
- Macro expansion should not require runtime Python objects.

The surface examples above should drive the final implementation design.

## Required AST Support

This feature depends on [AST Plan](ast-plan.md).

Macros need structured access to:

- declarations
- decorators
- fields
- methods
- enums
- function params
- types
- source ranges
- generated item ranges

String macros should not be the default. C/C++ preprocessor macros already cover
the string/token world badly enough.

## Acceptance

- The macro syntax examples in this document parse as reserved or recognized
  forms.
- `@derive(Json)` target examples have a clear expansion model.
- Field attributes compose with class decorators.
- Macro diagnostics can report both macro use and generated code context.
- C/C++ imported macros remain usable without becoming Dudu macro definitions.
- The design does not require runtime Python objects or runtime type metadata.
