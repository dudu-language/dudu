# shellcheck shell=bash

prepare_scaled_generics_case() {
    local requested_lines="$1"
    local project="$scale_root/generics_$requested_lines"
    local entry="$project/main.dd"
    rm -rf "$project"
    mkdir -p "$project"

    local funcs=$((requested_lines / 9))
    if [[ "$funcs" -lt 1 ]]; then
        funcs=1
    fi
    cat >"$entry" <<'EOF'
class BenchBox[T]:
    value: T

    def get(self) -> T:
        return self.value

def bench_identity[T](value: T) -> T:
    return value

EOF
    for ((i = 0; i < funcs; ++i)); do
        cat >>"$entry" <<EOF
def generic_func_$i(value: i32) -> i32:
    box: BenchBox[i32] = BenchBox[i32](bench_identity[i32](value + $i))
    return box.get()

EOF
    done
    {
        printf 'def main() -> i32:\n'
        printf '    total: i32 = 0\n'
        local limit="$funcs"
        if [[ "$limit" -gt 256 ]]; then
            limit=256
        fi
        for ((i = 0; i < limit; ++i)); do
            printf '    total += generic_func_%d(%d)\n' "$i" "$i"
        done
        printf '    return total\n'
    } >>"$entry"
    printf '%s\n' "$entry"
}

prepare_scaled_matches_case() {
    local requested_lines="$1"
    local project="$scale_root/matches_$requested_lines"
    local entry="$project/main.dd"
    rm -rf "$project"
    mkdir -p "$project"

    local funcs=$((requested_lines / 28))
    if [[ "$funcs" -lt 1 ]]; then
        funcs=1
    fi
    cat >"$entry" <<'EOF'
enum Message:
    Quit

    Move:
        x: i32
        y: i32

    Write(str)

    Scale(i32)


EOF
    for ((i = 0; i < funcs; ++i)); do
        cat >>"$entry" <<EOF
def match_func_$i(msg: Message, seed: i32) -> i32:
    match msg:
        case Message.Quit:
            return seed + $i
        case Message.Move(x, y) if x > y:
            return x - y + seed
        case Message.Move(x, y):
            return x + y + $i
        case Message.Write(text) if len(text) > 3:
            return i32(len(text)) + seed
        case Message.Write(text):
            return i32(len(text)) + $i
        case Message.Scale(value):
            return value * 2 + seed

EOF
    done
    {
        printf 'def main() -> i32:\n'
        printf '    total: i32 = 0\n'
        printf '    moved: Message = Message.Move(x=20, y=19)\n'
        printf '    text: Message = Message.Write("bench")\n'
        printf '    scale: Message = Message.Scale(7)\n'
        local limit="$funcs"
        if [[ "$limit" -gt 128 ]]; then
            limit=128
        fi
        for ((i = 0; i < limit; ++i)); do
            printf '    total += match_func_%d(moved, %d)\n' "$i" "$i"
            printf '    total += match_func_%d(text, %d)\n' "$i" "$i"
            printf '    total += match_func_%d(scale, %d)\n' "$i" "$i"
        done
        printf '    return total\n'
    } >>"$entry"
    printf '%s\n' "$entry"
}

prepare_scaled_operators_case() {
    local requested_lines="$1"
    local project="$scale_root/operators_$requested_lines"
    local entry="$project/main.dd"
    rm -rf "$project"
    mkdir -p "$project"

    local funcs=$((requested_lines / 24))
    if [[ "$funcs" -lt 1 ]]; then
        funcs=1
    fi
    cat >"$entry" <<'EOF'
class Vec2:
    x: i32
    y: i32

    @operator("+")
    def add(self, other: Vec2) -> Vec2:
        return Vec2(self.x + other.x, self.y + other.y)

    @operator("*")
    def mul_vec(self, other: Vec2) -> Vec2:
        return Vec2(self.x * other.x, self.y * other.y)

    @operator("*")
    def mul_scalar(self, value: i32) -> Vec2:
        return Vec2(self.x * value, self.y * value)

    @operator("==")
    def same(self, other: Vec2) -> bool:
        return self.x == other.x and self.y == other.y


class Tensor2:
    values: array[i32][4]

    @operator("[]")
    def at(self, index: i32) -> i32:
        return self.values[index]

    @operator("[]=")
    def put(self, index: i32, value: i32):
        self.values[index] = value


EOF
    for ((i = 0; i < funcs; ++i)); do
        cat >>"$entry" <<EOF
def operator_func_$i(seed: i32) -> i32:
    a: Vec2 = Vec2(seed + 1, seed + 2)
    b: Vec2 = Vec2(seed + 3, seed + 4)
    c = (a + b) * 2
    d = a * b
    values: array[i32] = [seed, seed + 1, seed + 2, seed + 3]
    tensor = Tensor2(values)
    tensor[1] = c.x + d.y + $i
    if c == d:
        return tensor[0]
    return tensor[1] + tensor[2]

EOF
    done
    {
        printf 'def main() -> i32:\n'
        printf '    total: i32 = 0\n'
        local limit="$funcs"
        if [[ "$limit" -gt 128 ]]; then
            limit=128
        fi
        for ((i = 0; i < limit; ++i)); do
            printf '    total += operator_func_%d(%d)\n' "$i" "$i"
        done
        printf '    return total\n'
    } >>"$entry"
    printf '%s\n' "$entry"
}

prepare_scaled_native_case() {
    local requested_lines="$1"
    local project="$scale_root/native_$requested_lines"
    local entry="$project/main.dd"
    rm -rf "$project"
    mkdir -p "$project/native_headers"
    cp "$repo_root/tests/fixtures/native_headers/simple_cpp.hpp" "$project/native_headers/"
    cp "$repo_root/tests/fixtures/native_headers/simple_c.h" "$project/native_headers/"

    local funcs=$((requested_lines / 20))
    if [[ "$funcs" -lt 1 ]]; then
        funcs=1
    fi

    cat >"$entry" <<'EOF'
from cpp.path import ./native_headers/simple_cpp.hpp
from c.path import ./native_headers/simple_c.h as c_native

EOF
    for ((i = 0; i < funcs; ++i)); do
        cat >>"$entry" <<EOF
def native_func_$i(seed: i32) -> i32:
    widget: Widget = Widget(seed)
    alias_widget: DuduWidgetAlias = DuduWidgetAlias(seed + 1)
    derived: dudu_native.DerivedWidget = dudu_native.DerivedWidget(seed + 2)
    nested: dudu_native.Outer.Inner = dudu_native.Outer.Inner(seed + 3)
    total = dudu_native.add(widget.scaled(2), dudu_native.overloaded(seed))
    total += i32(dudu_native.overloaded(f32(seed)))
    total += dudu_native.overloaded_pair(seed, f32(2.0))
    total += dudu_native.overloaded_pair(f32(3.0), seed)
    total += dudu_native.use_base_widget(&derived)
    total += dudu_native.read_const_ref(widget)
    total += use_widget(&alias_widget)
    total += c_native.dudu_native_add(seed, c_native.DUDU_NATIVE_SCALE(2))
    total += nested.doubled()
    return total + $i

EOF
    done
    {
        printf 'def main() -> i32:\n'
        printf '    total: i32 = 0\n'
        local limit="$funcs"
        if [[ "$limit" -gt 128 ]]; then
            limit=128
        fi
        for ((i = 0; i < limit; ++i)); do
            printf '    total += native_func_%d(%d)\n' "$i" "$i"
        done
        printf '    return total\n'
    } >>"$entry"
    printf '%s\n' "$entry"
}

prepare_scaled_stdlib_case() {
    local requested_lines="$1"
    local project="$scale_root/stdlib_$requested_lines"
    local entry="$project/main.dd"
    rm -rf "$project"
    mkdir -p "$project"

    local funcs=$((requested_lines / 26))
    if [[ "$funcs" -lt 1 ]]; then
        funcs=1
    fi

    cat >"$entry" <<'EOF'
from cpp import algorithm
from cpp import numeric
from cpp import string
from cpp import tuple
from cpp import unordered_map
from cpp import utility
from cpp import vector

EOF
    for ((i = 0; i < funcs; ++i)); do
        cat >>"$entry" <<EOF
def stdlib_func_$i(seed: i32) -> i32:
    values: std.vector[i32]
    values.push_back(seed)
    values.push_back(seed + 3)
    values.push_back($i)
    values.push_back(seed + 1)
    std.sort(values.begin(), values.end())
    total: i32 = std.accumulate(values.begin(), values.end(), 0)
    names: std.vector[std.string]
    names.push_back(std.string("alpha"))
    names.push_back(std.string("beta"))
    scores: std.unordered_map[std.string, i32]
    scores[names[0]] = total
    scores[names[1]] = total + $i
    pair: std.pair[std.string, i32] = std.make_pair(std.string("score"), scores[names[1]])
    tup: std.tuple[std.string, i32] = std.make_tuple(pair.first, pair.second)
    if std.get[1](tup) != pair.second:
        return 0
    return scores[std.string("alpha")] + std.get[1](tup)

EOF
    done
    {
        printf 'def main() -> i32:\n'
        printf '    total: i32 = 0\n'
        local limit="$funcs"
        if [[ "$limit" -gt 128 ]]; then
            limit=128
        fi
        for ((i = 0; i < limit; ++i)); do
            printf '    total += stdlib_func_%d(%d)\n' "$i" "$i"
        done
        printf '    return total\n'
    } >>"$entry"
    printf '%s\n' "$entry"
}

prepare_scaled_mixed_case() {
    local requested_lines="$1"
    local project="$scale_root/mixed_$requested_lines"
    local entry="$project/main.dd"
    rm -rf "$project"
    mkdir -p "$project"

    local module_count=8
    local funcs_per_module=$((requested_lines / (module_count * 22)))
    if [[ "$funcs_per_module" -lt 1 ]]; then
        funcs_per_module=1
    fi

    : >"$entry"
    for ((module = 0; module < module_count; ++module)); do
        printf 'import mix%02d\n' "$module" >>"$entry"
    done
    {
        printf '\n'
        printf 'def main() -> i32:\n'
        printf '    total: i32 = 0\n'
    } >>"$entry"
    for ((module = 0; module < module_count; ++module)); do
        local call_limit="$funcs_per_module"
        if [[ "$call_limit" -gt 32 ]]; then
            call_limit=32
        fi
        for ((func = 0; func < call_limit; ++func)); do
            printf '    total += mix%02d.mix%02d_func_%03d(%d)\n' \
                "$module" "$module" "$func" "$func" >>"$entry"
        done
    done
    printf '    return total\n' >>"$entry"

    for ((module = 0; module < module_count; ++module)); do
        local file
        file="$(printf '%s/mix%02d.dd' "$project" "$module")"
        cat >"$file" <<EOF
class MixBox$(printf '%02d' "$module"):
    value: i32
    scale: i32

    def bump(self, amount: i32) -> i32:
        self.value += amount * self.scale
        return self.value

def mix$(printf '%02d' "$module")_identity[T](value: T) -> T:
    return value

EOF
        for ((func = 0; func < funcs_per_module; ++func)); do
            cat >>"$file" <<EOF
def mix$(printf '%02d' "$module")_func_$(printf '%03d' "$func")(seed: i32) -> i32:
    box: MixBox$(printf '%02d' "$module") = MixBox$(printf '%02d' "$module")(seed + $func, $((module + 1)))
    values: array[i32] = [seed, seed + 1, seed + 2, seed + 3]
    total: i32 = 0
    for index: i32 in range(4):
        total += values[index]
        if total % 2 == 0:
            total += box.bump(index)
        else:
            total -= index
    return mix$(printf '%02d' "$module")_identity[i32](total)

EOF
        done
    done
    printf '%s\n' "$entry"
}
