# Project Goals

Dudu is a statically typed Python subset that compiles to readable C++.

The goal:

```text
99% Python-shaped syntax.
C/C++ capability.
Readable generated .hpp/.cpp files.
Direct access to C and C++ libraries.
No CPython runtime dependency.
```

Authoritative docs:

- [Appearance Spec](appearance-spec.md)
- [Dudu Tests](tests.md)
- [Le Plan](le_plan.md)
- [Python Subset Compiler Plan](python-subset-compiler-plan.md)

The compiler should support practical systems programming: native values,
pointers, references, fixed-width integers, fixed arrays, dynamic containers,
manual allocation, C/C++ imports, generated headers, hardware layout controls,
atomics, volatile memory, target attributes, and build modes for hosted,
freestanding, embedded, CUDA, and shader-style targets.

## Concurrency Philosophy

Dudu should not grow language-level `async`/`await` by default. Async colors
function signatures, splits ecosystems into sync and async variants, and makes
ordinary control flow harder to read. Dudu should prefer the C-style systems
model: normal blocking functions, explicit concurrency at the boundary, and
small library abstractions over OS primitives.

Useful alternatives to async are ordinary and enough for most systems code:

```python
def handle_client(client: Socket):
    request = read_request(client)      # blocking call
    response = handle_request(request)
    write_response(client, response)


def serve(server: Socket):
    while True:
        client = server.accept()        # blocking call
        thread.spawn(handle_client, client)
```

For many sockets, use explicit readiness mechanisms instead of coloring every
function:

```python
def serve(server: Socket):
    poller = Poller()
    poller.add(server, Event.Read)

    while True:
        events = poller.wait()
        for event in events:
            if event.fd == server.fd:
                client = server.accept_nonblocking()
                poller.add(client, Event.Read)
            elif event.readable:
                request = try_read_request(event.fd)
                if request.ready:
                    response = handle_request(request)
                    write_response(event.fd, response)
                    poller.remove(event.fd)
```

This is similar to C's long-standing model: blocking calls, threads, queues,
`select`/`poll`/platform event APIs, and explicit state machines when needed.
The language should make those patterns clean before considering async syntax.
