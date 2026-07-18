from types import SimpleNamespace


def create_workspace(root):
    dudu_source = """'''Rich documentation fixture module.'''

enum Status:
    '''Result status returned by documented operations.'''
    Ready
    Failed:
        reason: str

class Packet[T]:
    '''A packet carrying one value.

    Args:
        value: Initial packet value.
    '''
    value: T

    def read(self) -> T:
        '''Read the packet value.

        Returns:
            The current packet value.
        '''
        return self.value

def blend(left: i32, right: i32) -> i32:
    '''Blend two signed values.

    Args:
        left: First value to blend.
        right: Second value to blend.

    Returns:
        The blended integer.
    '''
    return left + right

def main() -> i32:
    packet: Packet[i32] = Packet[i32](blend(20, 22))
    return packet.read()
"""
    native_header = """#pragma once

/** Base type for rich native records. */
struct RichBase {};

/**
 * Record imported by the rich documentation fixture.
 *
 * The second paragraph must survive Clang metadata and the native cache.
 */
struct RichRecord : RichBase {
    /** Stable identifier alias. */
    using Id = unsigned long long;

    /** Record state values. */
    enum class State {
        /** The record is ready. */
        Ready,
        /** The record failed. */
        Failed,
    };

    /** Current record value. */
    int value = 0;

    /**
     * Scale the current value.
     * @param factor Multiplication factor.
     * @return The scaled value.
     */
    int scaled(int factor = 2) const noexcept { return value * factor; }
};

/**
 * Convert a value using a compile-time policy.
 * @tparam T Value type.
 * @tparam Policy Conversion policy.
 * @param value Value to convert.
 * @param amount Conversion amount.
 * @return The converted value.
 * @deprecated Use rich_convert_new instead.
 */
template <typename T, typename Policy = void>
[[deprecated("use rich_convert_new")]]
T rich_convert(T value, int amount = 3) noexcept { return value + T(amount); }

/** Choose an integer value. */
inline int rich_choose(int value) noexcept { return value; }

/** Choose a floating-point value. */
inline double rich_choose(double value) noexcept { return value; }

/** Multiply two values in a native macro. */
#define RICH_MULTIPLY(value, factor) ((value) * (factor))
"""
    native_source = """from cpp.path import ./rich_native.hpp as rich

def main() -> i32:
    record: rich.RichRecord
    record.value = 7
    converted = rich.rich_convert[i32](record.scaled(), 4)
    chosen = rich.rich_choose(converted)
    return rich.RICH_MULTIPLY(chosen, 2)
"""
    c_header = """#pragma once

/** A point declared by an ordinary C header. */
typedef struct RichCPoint {
    /** Horizontal coordinate. */
    int x;
    /** Vertical coordinate. */
    int y;
} RichCPoint;

/**
 * Add two signed values.
 * @param left Left operand.
 * @param right Right operand.
 * @return Their signed sum.
 */
static inline int rich_c_add(int left, int right) { return left + right; }

int rich_c_undocumented(int value);
"""
    c_source = """from c.path import ./rich_c.h as c_api

def use_c(point: c_api.RichCPoint) -> i32:
    return c_api.rich_c_add(point.x, point.y)

def undocumented(value: i32) -> i32:
    return c_api.rich_c_undocumented(value)
"""
    generated_header = """#pragma once

namespace generated {

/** Message emitted by an external schema compiler. */
class UserMessage final {
public:
    /** Return the generated numeric identifier. */
    int id() const noexcept;

    /**
     * Set the generated numeric identifier.
     * @param value New identifier value.
     */
    void set_id(int value) noexcept;
};

} // namespace generated
"""
    generated_source = """from cpp.path import ./user_message.pb.hpp as schema

def update_message(message: &schema.generated.UserMessage) -> i32:
    message.set_id(42)
    return message.id()
"""
    stdlib_source = """from cpp import vector

def vector_size(values: &const[std.vector[i32]]) -> usize:
    return values.size()
"""
    dudu = root / "docs.dd"
    native = root / "native.dd"
    c_api = root / "c_api.dd"
    generated = root / "generated.dd"
    stdlib = root / "stdlib.dd"
    header = root / "rich_native.hpp"
    c_header_path = root / "rich_c.h"
    generated_header_path = root / "user_message.pb.hpp"
    config = root / "dudu.toml"
    dudu.write_text(dudu_source)
    native.write_text(native_source)
    c_api.write_text(c_source)
    generated.write_text(generated_source)
    stdlib.write_text(stdlib_source)
    header.write_text(native_header)
    c_header_path.write_text(c_header)
    generated_header_path.write_text(generated_header)
    config.write_text('name = "lsp-rich-docs"\nmain = "docs.dd"\nbuild_dir = "build"\n')
    return SimpleNamespace(
        root=root,
        dudu=dudu,
        native=native,
        c_api=c_api,
        generated=generated,
        stdlib=stdlib,
        header=header,
        c_header_path=c_header_path,
        generated_header_path=generated_header_path,
        dudu_source=dudu_source,
        native_source=native_source,
        c_source=c_source,
        generated_source=generated_source,
        stdlib_source=stdlib_source,
        native_header=native_header,
        c_header=c_header,
        generated_header=generated_header,
    )
