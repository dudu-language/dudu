# Import Semantics

Dudu imports should look Python-shaped while preserving C and C++ include
behavior.

## Dudu Modules

Dudu modules use normal Python-style imports:

```python
import camera
import renderer.camera as cam
from vec3 import Vec3
from raytrace import shade_pixel as shade
```

Non-string module names are Dudu modules. Selective imports bind the imported
name directly and must not collide with local declarations.

## Native Headers

Native imports use `from c`, `from cxx`, and `from cpp` so the source makes the
language boundary explicit:

```python
from c import math.h
from cxx import libxml/parser.h
from cpp import thread
from cpp import string
from cpp import vector
```

These are system/header-search imports and emit angle includes:

```cpp
#include <math.h>
#include <libxml/parser.h>
#include <thread>
```

`c` emits an `extern "C"` include block for C headers that need C linkage from
generated C++. `cxx` scans C-style globals but emits a plain C++ include for C
API headers that already manage C++ linkage. `cpp` scans C++ namespaces,
classes, methods, overloads, operators, and templates.

Quoted include behavior uses `.path`:

```python
from c.path import vendor/foo.h
from cpp.path import local/foo.hpp
```

These emit quoted includes:

```cpp
#include "vendor/foo.h"
#include "local/foo.hpp"
```

The parser treats the imported target after `from c/cxx/cpp import` as a native
header path token, not as a Dudu module name. Quotes are not needed for normal
header paths. If a generated or vendor path contains spaces or other unusual
characters, a quoted target may be accepted as path include syntax.

The complete matrix is:

| Language boundary | Header search | Source-relative path |
| --- | --- | --- |
| C | `from c import SDL3/SDL.h` | `from c.path import vendor/foo.h` |
| C with alias | `from c import sqlite3.h as sqlite` | `from c.path import vendor/foo.h as foo` |
| C API with C++ linkage | `from cxx import libxml/parser.h` | `from cxx.path import vendor/c_api.h` |
| C API with C++ linkage and alias | `from cxx import libxml/parser.h as xml` | `from cxx.path import vendor/c_api.h as c_api` |
| C++ | `from cpp import thread` | `from cpp.path import vendor/math.hpp` |
| C++ with alias | `from cpp import thread as threading` | `from cpp.path import vendor/math.hpp as math` |

Aliases are optional in every row. `c`, `cxx`, and `cpp` control scanner and
linkage semantics; `.path` controls include resolution independently.

## Native Names

Unaliased native imports expose declarations according to their real C/C++
namespaces:

```python
from cpp import thread
from cpp import string

worker: std.thread
name: std.string
```

This mirrors C++:

```cpp
#include <thread>
#include <string>

std::thread worker;
std::string name;
```

C headers generally expose globals:

```python
from c import math.h

root = sqrt(4.0)
```

Prefixed C APIs are normally clearest without aliases because their own names
already provide a boundary:

```python
from c import SDL3/SDL.h
from c import sqlite3.h
from c import curl/curl.h
from c import libavcodec/avcodec.h
from c import vulkan/vulkan.h
from c import zstd.h

window = SDL_CreateWindow("demo", 800, 600, 0)
database: *sqlite3
curl = curl_easy_init()
packet = av_packet_alloc()
version = vkEnumerateInstanceVersion(&api_version)
compressed = ZSTD_compress(dst, dst_capacity, src, src_size, 3)
```

These names remain the native names from their headers. Dudu does not add an
extra synthetic namespace.

Multiple C++ headers may contribute to the same namespace. This is normal and
must merge when the declarations are compatible:

```python
from cpp import thread
from cpp import vector
from cpp import string

threads: list[std.thread] = []
names: std.vector[std.string]
```

## Aliased Native Imports

Use `as` when a native header would collide with local Dudu names or when a C
library's global namespace is too broad:

```python
from c import raylib.h as ray
from c.path import vendor/foo.h as vendor
```

For example, a broad local C API can be contained without changing its emitted
C names:

```python
from c.path import vendor/legacy_api.h as legacy

handle = legacy.open_context()
legacy.process_context(handle)
```

Aliased native imports are hygienic. They expose scanned names through the alias
and do not also leak the unaliased C/C++ globals into Dudu lookup:

```python
color: ray.Color
ray.InitWindow(800, 600, "demo")
```

Generated C++ still emits the original native spelling:

```cpp
Color color;
InitWindow(800, 600, "demo");
```

C tag types keep their required native tag spelling when reached through an
alias. For example:

```python
from c import sys/stat.h as stat

info: stat.stat
```

emits the native C type as:

```cpp
struct stat info;
```

Typedef aliases preserve the typedef spelling instead. If a header exposes
`typedef std::array<unsigned char, 16> DuduFixedBytes`, then an aliased Dudu
type such as `native.DuduFixedBytes` emits `DuduFixedBytes`, not the expanded
implementation type. The scanner/codegen boundary is responsible for knowing
which imported names are C records and which are typedef aliases.

C++ namespaces under an aliased header are also reached through the alias if the
alias is used intentionally as a collision boundary:

```python
from cpp.path import vendor/math.hpp as vendor_math

value: vendor_math.vendor.Vec3
```

Prefer unaliased imports for normal C++ standard-library headers so `std` stays
the real C++ namespace:

```python
from cpp import thread
from cpp import string

worker: std.thread
```

Do not write fake aliases such as `from cpp import thread as std`.

## Collision Policy

Dudu should reject ambiguous bindings instead of silently shadowing:

- A native namespace such as `std` may merge with the same native namespace from
  another compatible C++ header.
- A native namespace or global that collides with a Dudu local/module binding is
  an error.
- A C global such as `Vec3` that collides with a Dudu `Vec3` is an error.
- Two native declarations with the same canonical identity are deduped.
- Two native declarations with the same visible name but different identities
  are an error.

Diagnostics should suggest the alias escape hatch:

```text
native name 'Vec3' collides with Dudu class 'Vec3'
hint: use `from c import vendor/foo.h as vendor` and refer to `vendor.Vec3`
```

## Editor Ranges

Native imports carry separate source ranges for the native language token, path
mode token, imported header target, and alias:

```python
from cpp.path import vendor/foo.hpp as foo
     ^^^ ^^^^        ^^^^^^^^^^^^^^    ^^^
```

The language server should use those ranges so Ctrl-click can distinguish:

- `cpp`: native C++ import semantics in hover; it has no source definition
- `path`: source-relative quoted-include semantics in hover; it has no source
  definition
- every segment of `vendor/foo.hpp`: opens the resolved header
- `foo`: opens the aliased header; find-references reports uses of the alias

This behavior applies equally to `c`, `cxx`, and `cpp` imports. Header-search
imports resolve through configured include directories, `pkg-config`, and the
compiler's system include paths. Path imports resolve from the importing source
file before configured include paths.

## Related Reference

- [C and C++ interop](interop.md)
- [Generics, native templates, and macros](native-templates-and-macros.md)
- [Native compatibility matrix](native-compatibility-matrix.md)
