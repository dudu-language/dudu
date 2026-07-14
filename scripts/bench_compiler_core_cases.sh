# shellcheck shell=bash

prepare_synthetic_project() {
    rm -rf "$synthetic_project"
    mkdir -p "$synthetic_project"
    : >"$synthetic_entry"
    for module in $(seq 0 11); do
        printf 'import mod%02d\n' "$module" >>"$synthetic_entry"
    done
    {
        printf '\n'
        printf 'def main() -> i32:\n'
        printf '    total: i32 = 0\n'
    } >>"$synthetic_entry"
    for module in $(seq 0 11); do
        for func in $(seq 0 15); do
            printf '    total += mod%02d.mod%02d_func%02d(%d)\n' \
                "$module" "$module" "$func" "$func" >>"$synthetic_entry"
        done
    done
    printf '    return total\n' >>"$synthetic_entry"

    for module in $(seq 0 11); do
        local file
        file="$(printf '%s/mod%02d.dd' "$synthetic_project" "$module")"
        : >"$file"
        for func in $(seq 0 15); do
            cat >>"$file" <<EOF
def mod$(printf '%02d' "$module")_func$(printf '%02d' "$func")(value: i32) -> i32:
    running: i32 = value
    running += $(($module + $func))
    if running % 2 == 0:
        return running / 2
    return running * 3 + 1

EOF
        done
    done
}

prepare_scaled_functions_case() {
    local requested_lines="$1"
    local project="$scale_root/functions_$requested_lines"
    local entry="$project/main.dd"
    rm -rf "$project"
    mkdir -p "$project"

    # Each generated helper contributes five lines. The main function keeps
    # enough references alive to exercise name lookup without making C++ compile
    # time part of this frontend benchmark.
    local helpers=$((requested_lines / 5))
    if [[ "$helpers" -lt 1 ]]; then
        helpers=1
    fi
    : >"$entry"
    for ((i = 0; i < helpers; ++i)); do
        cat >>"$entry" <<EOF
def generated_func_$i(value: i32) -> i32:
    total: i32 = value
    total += $i
    return total

EOF
    done
    {
        printf 'def main() -> i32:\n'
        printf '    total: i32 = 0\n'
        local limit="$helpers"
        if [[ "$limit" -gt 128 ]]; then
            limit=128
        fi
        for ((i = 0; i < limit; ++i)); do
            printf '    total += generated_func_%d(%d)\n' "$i" "$i"
        done
        printf '    return total\n'
    } >>"$entry"
    printf '%s\n' "$entry"
}

prepare_scaled_classes_case() {
    local requested_lines="$1"
    local project="$scale_root/classes_$requested_lines"
    local entry="$project/main.dd"
    rm -rf "$project"
    mkdir -p "$project"

    local classes=$((requested_lines / 9))
    if [[ "$classes" -lt 1 ]]; then
        classes=1
    fi
    : >"$entry"
    for ((i = 0; i < classes; ++i)); do
        cat >>"$entry" <<EOF
class BenchType$i:
    value: i32

    def bump(self, delta: i32) -> i32:
        self.value += delta
        return self.value

EOF
    done
    {
        printf 'def main() -> i32:\n'
        printf '    total: i32 = 0\n'
        local limit="$classes"
        if [[ "$limit" -gt 128 ]]; then
            limit=128
        fi
        for ((i = 0; i < limit; ++i)); do
            printf '    item_%d = BenchType%d(%d)\n' "$i" "$i" "$i"
            printf '    total += item_%d.bump(%d)\n' "$i" "$i"
        done
        printf '    return total\n'
    } >>"$entry"
    printf '%s\n' "$entry"
}

prepare_scaled_expressions_case() {
    local requested_lines="$1"
    local project="$scale_root/expressions_$requested_lines"
    local entry="$project/main.dd"
    rm -rf "$project"
    mkdir -p "$project"

    local statements=$((requested_lines - 4))
    if [[ "$statements" -lt 1 ]]; then
        statements=1
    fi
    {
        printf 'def main() -> i32:\n'
        printf '    total: i32 = 1\n'
        for ((i = 0; i < statements; ++i)); do
            printf '    total = (((total + %d) * 3) - ((%d + total) / 2)) + (%d %% 17)\n' "$i" "$i" "$i"
        done
        printf '    return total\n'
    } >"$entry"
    printf '%s\n' "$entry"
}

prepare_scaled_modules_case() {
    local requested_lines="$1"
    local project="$scale_root/modules_$requested_lines"
    local entry="$project/main.dd"
    rm -rf "$project"
    mkdir -p "$project"

    local module_count=16
    local funcs_per_module=$((requested_lines / (module_count * 7)))
    if [[ "$funcs_per_module" -lt 1 ]]; then
        funcs_per_module=1
    fi

    : >"$entry"
    for ((module = 0; module < module_count; ++module)); do
        printf 'import mod%02d\n' "$module" >>"$entry"
    done
    {
        printf '\n'
        printf 'def main() -> i32:\n'
        printf '    total: i32 = 0\n'
    } >>"$entry"
    for ((module = 0; module < module_count; ++module)); do
        local call_limit="$funcs_per_module"
        if [[ "$call_limit" -gt 16 ]]; then
            call_limit=16
        fi
        for ((func = 0; func < call_limit; ++func)); do
            printf '    total += mod%02d.mod%02d_func%03d(%d)\n' \
                "$module" "$module" "$func" "$func" >>"$entry"
        done
    done
    printf '    return total\n' >>"$entry"

    for ((module = 0; module < module_count; ++module)); do
        local file
        file="$(printf '%s/mod%02d.dd' "$project" "$module")"
        : >"$file"
        for ((func = 0; func < funcs_per_module; ++func)); do
            cat >>"$file" <<EOF
def mod$(printf '%02d' "$module")_func$(printf '%03d' "$func")(value: i32) -> i32:
    running: i32 = value
    running += $module
    running += $func
    return running

EOF
        done
    done
    printf '%s\n' "$entry"
}

prepare_scaled_calls_case() {
    local requested_lines="$1"
    local project="$scale_root/calls_$requested_lines"
    local entry="$project/main.dd"
    rm -rf "$project"
    mkdir -p "$project"

    local helpers=$((requested_lines / 6))
    if [[ "$helpers" -lt 1 ]]; then
        helpers=1
    fi
    : >"$entry"
    for ((i = 0; i < helpers; ++i)); do
        cat >>"$entry" <<EOF
def call_leaf_$i(left: i32, right: i32) -> i32:
    value: i32 = left + right
    value += $i
    return value

EOF
    done
    {
        printf 'def main() -> i32:\n'
        printf '    total: i32 = 0\n'
        local limit="$helpers"
        if [[ "$limit" -gt 512 ]]; then
            limit=512
        fi
        for ((i = 0; i < limit; ++i)); do
            printf '    total = call_leaf_%d(call_leaf_%d(total, %d), total)\n' "$i" "$i" "$i"
        done
        printf '    return total\n'
    } >>"$entry"
    printf '%s\n' "$entry"
}

prepare_scaled_control_case() {
    local requested_lines="$1"
    local project="$scale_root/control_$requested_lines"
    local entry="$project/main.dd"
    rm -rf "$project"
    mkdir -p "$project"

    local funcs=$((requested_lines / 12))
    if [[ "$funcs" -lt 1 ]]; then
        funcs=1
    fi
    : >"$entry"
    for ((i = 0; i < funcs; ++i)); do
        cat >>"$entry" <<EOF
def control_func_$i(value: i32) -> i32:
    total: i32 = value
    for index: i32 in range(4):
        if total % 2 == 0:
            total += index + $i
        else:
            total -= index
    while total < $((i + 16)):
        total += 1
    return total

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
            printf '    total += control_func_%d(%d)\n' "$i" "$i"
        done
        printf '    return total\n'
    } >>"$entry"
    printf '%s\n' "$entry"
}

prepare_scaled_arrays_case() {
    local requested_lines="$1"
    local project="$scale_root/arrays_$requested_lines"
    local entry="$project/main.dd"
    rm -rf "$project"
    mkdir -p "$project"

    local funcs=$((requested_lines / 12))
    if [[ "$funcs" -lt 1 ]]; then
        funcs=1
    fi
    : >"$entry"
    for ((i = 0; i < funcs; ++i)); do
        cat >>"$entry" <<EOF
def array_func_$i(values: &array[i32][4]) -> i32:
    total: i32 = 0
    for index: i32 in range(4):
        total += values[index]
    values[0] = total + $i
    return values[0] + values[1]

EOF
    done
    {
        printf 'def main() -> i32:\n'
        printf '    values: array[i32] = [1, 2, 3, 4]\n'
        printf '    total: i32 = 0\n'
        local limit="$funcs"
        if [[ "$limit" -gt 256 ]]; then
            limit=256
        fi
        for ((i = 0; i < limit; ++i)); do
            printf '    total += array_func_%d(values)\n' "$i"
        done
        printf '    return total\n'
    } >>"$entry"
    printf '%s\n' "$entry"
}

prepare_scaled_indexing_case() {
    local requested_lines="$1"
    local project="$scale_root/indexing_$requested_lines"
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


class Color4:
    r: i32
    g: i32
    b: i32
    a: i32


def sum_view(values: array_view[i32]) -> i32:
    total: i32 = 0
    for value: i32 in values:
        total += value
    return total

EOF
    for ((i = 0; i < funcs; ++i)); do
        cat >>"$entry" <<EOF
def indexing_func_$i(seed: i32) -> i32:
    matrix: array[i32] = [
        [seed, seed + 1, seed + 2],
        [seed + 3, seed + 4, seed + 5],
    ]
    image: array[i32] = [
        [[seed, seed + 1], [seed + 2, seed + 3]],
        [[seed + 4, seed + 5], [seed + 6, seed + 7]],
    ]
    row: array[i32][3] = matrix[1]
    column: array_view[i32][2] = matrix[:, 1]
    channel: array_view[i32][2, 2] = image[:, :, 0]
    pos: Vec2 = Vec2(seed, seed + 1)
    pos.yx = Vec2(seed + 2, seed + 3)
    color: Color4 = Color4(seed, seed + 1, seed + 2, seed + 3)
    color.bgra = Color4(seed + 6, seed + 5, seed + 4, seed + 7)
    return row[0] + row[2] + sum_view(column) + sum_view(channel) + pos.x + pos.y + color.r + color.a + $i

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
            printf '    total += indexing_func_%d(%d)\n' "$i" "$i"
        done
        printf '    return total\n'
    } >>"$entry"
    printf '%s\n' "$entry"
}
