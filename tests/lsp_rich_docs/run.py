#!/usr/bin/env python3

import pathlib
import shutil
import subprocess
import sys
import tempfile

from protocol import message, notification, open_document, position, read_messages, request, response, text_document
from workspace import create_workspace


def require(text, *parts):
    for part in parts:
        if part not in text:
            raise AssertionError(f"missing {part!r} in:\n{text}")


def session_requests(ws):
    return [
            request(1, "initialize", {"rootUri": ws.root.as_uri()}),
            notification("initialized", {}),
            open_document(ws.dudu),
            open_document(ws.native),
            open_document(ws.c_api),
            open_document(ws.generated),
            open_document(ws.stdlib),
            request(10, "textDocument/hover", {"textDocument": text_document(ws.dudu), "position": position(ws.dudu_source, "blend(20", add=1)}),
            request(11, "textDocument/signatureHelp", {"textDocument": text_document(ws.dudu), "position": position(ws.dudu_source, "blend(20, 22", add=len("blend(20,"))}),
            request(12, "textDocument/hover", {"textDocument": text_document(ws.dudu), "position": position(ws.dudu_source, "Packet[i32]", add=1)}),
            request(13, "textDocument/hover", {"textDocument": text_document(ws.dudu), "position": position(ws.dudu_source, "Status:", add=1)}),
            request(20, "textDocument/hover", {"textDocument": text_document(ws.native), "position": position(ws.native_source, "rich.rich_convert", add=len("rich."))}),
            request(21, "textDocument/signatureHelp", {"textDocument": text_document(ws.native), "position": position(ws.native_source, "record.scaled()", add=len("record.scaled("))}),
            request(22, "textDocument/hover", {"textDocument": text_document(ws.native), "position": position(ws.native_source, "rich.RichRecord", add=len("rich."))}),
            request(23, "textDocument/hover", {"textDocument": text_document(ws.native), "position": position(ws.native_source, "rich.RICH_MULTIPLY", add=len("rich."))}),
            request(24, "textDocument/signatureHelp", {"textDocument": text_document(ws.native), "position": position(ws.native_source, "rich.RICH_MULTIPLY(chosen", add=len("rich.RICH_MULTIPLY(chosen"))}),
            request(29, "textDocument/hover", {"textDocument": text_document(ws.native), "position": position(ws.native_source, "rich.rich_choose", add=len("rich."))}),
            request(30, "textDocument/signatureHelp", {"textDocument": text_document(ws.native), "position": position(ws.native_source, "rich.rich_choose(converted", add=len("rich.rich_choose("))}),
            request(31, "textDocument/definition", {"textDocument": text_document(ws.native), "position": position(ws.native_source, "rich.rich_choose", add=len("rich."))}),
            request(40, "textDocument/hover", {"textDocument": text_document(ws.c_api), "position": position(ws.c_source, "c_api.rich_c_add", add=len("c_api."))}),
            request(41, "textDocument/signatureHelp", {"textDocument": text_document(ws.c_api), "position": position(ws.c_source, "c_api.rich_c_add(point.x", add=len("c_api.rich_c_add("))}),
            request(42, "textDocument/hover", {"textDocument": text_document(ws.c_api), "position": position(ws.c_source, "c_api.RichCPoint", add=len("c_api."))}),
            request(43, "textDocument/hover", {"textDocument": text_document(ws.c_api), "position": position(ws.c_source, "c_api.rich_c_undocumented", add=len("c_api."))}),
            request(44, "textDocument/definition", {"textDocument": text_document(ws.c_api), "position": position(ws.c_source, "c_api.rich_c_add", add=len("c_api."))}),
            request(50, "textDocument/hover", {"textDocument": text_document(ws.generated), "position": position(ws.generated_source, "schema.generated.UserMessage", add=len("schema.generated."))}),
            request(51, "textDocument/signatureHelp", {"textDocument": text_document(ws.generated), "position": position(ws.generated_source, "message.set_id(42", add=len("message.set_id("))}),
            request(52, "textDocument/hover", {"textDocument": text_document(ws.generated), "position": position(ws.generated_source, "message.id()", add=len("message."))}),
            request(53, "textDocument/definition", {"textDocument": text_document(ws.generated), "position": position(ws.generated_source, "message.set_id", add=len("message."))}),
            request(60, "textDocument/hover", {"textDocument": text_document(ws.stdlib), "position": position(ws.stdlib_source, "std.vector", add=len("std."))}),
            request(61, "textDocument/signatureHelp", {"textDocument": text_document(ws.stdlib), "position": position(ws.stdlib_source, "values.size()", add=len("values.size("))}),
            request(62, "textDocument/definition", {"textDocument": text_document(ws.stdlib), "position": position(ws.stdlib_source, "std.vector", add=len("std."))}),
            request(25, "workspace/executeCommand", {
                "command": "dudu.showGeneratedCpp",
                "arguments": [{
                    "textDocument": text_document(ws.dudu),
                    "range": {
                        "start": position(ws.dudu_source, "def blend"),
                        "end": position(ws.dudu_source, "def main"),
                    },
                }],
            }),
            request(26, "workspace/executeCommand", {
                "command": "dudu.showGeneratedCpp",
                "arguments": [{
                    "textDocument": text_document(ws.dudu),
                    "range": {
                        "start": position(ws.dudu_source, "class Packet"),
                        "end": position(ws.dudu_source, "def blend"),
                    },
                }],
            }),
            request(27, "workspace/executeCommand", {
                "command": "dudu.showGeneratedCpp",
                "arguments": [{
                    "textDocument": text_document(ws.dudu),
                    "range": {
                        "start": position(ws.dudu_source, "left + right", add=len("left ")),
                        "end": position(ws.dudu_source, "left + right", add=len("left ")),
                    },
                }],
            }),
            request(28, "workspace/executeCommand", {
                "command": "dudu.showGeneratedCpp",
                "arguments": [{
                    "textDocument": text_document(ws.dudu),
                    "range": {
                        "start": position(ws.dudu_source, "def read"),
                        "end": position(ws.dudu_source, "def read"),
                    },
                }],
            }),
            request(99, "shutdown", None),
            notification("exit", None),
        ]


def run_session(repo, ws):
    proc = subprocess.run(
        [str(repo / "build" / "dudu-lsp")],
        input="".join(session_requests(ws)).encode(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=15,
        check=False,
    )
    if proc.returncode != 0:
        raise AssertionError(proc.stderr.decode(errors="replace"))
    return read_messages(proc.stdout)


def check_messages(messages):
    dudu_hover = response(messages, 10)["contents"]["value"]
    require(dudu_hover, "def blend(left: i32, right: i32) -> i32", "Blend two signed values.", "left", "First value to blend.", "Returns", "The blended integer.")
    dudu_signature = response(messages, 11)["signatures"][0]
    require(dudu_signature["documentation"]["value"], "Blend two signed values.", "The blended integer.")
    require(dudu_signature["parameters"][0]["documentation"]["value"], "First value to blend.")
    require(dudu_signature["parameters"][1]["documentation"]["value"], "Second value to blend.")

    class_hover = response(messages, 12)["contents"]["value"]
    require(class_hover, "class Packet[T]", "value: T", "def read", "A packet carrying one value.")
    enum_hover = response(messages, 13)["contents"]["value"]
    require(enum_hover, "enum Status", "Ready", "Failed", "reason: str")

    native_hover = response(messages, 20)["contents"]["value"]
    require(native_hover, "rich_convert", "template <typename T, typename Policy = void>", "int amount = 3", "noexcept", "Value to convert.", "Conversion amount.", "The converted value.", "Deprecated", "use rich_convert_new", "Imported by")
    if native_hover.index("value: T") >= native_hover.index("amount: i32 = 3"):
        raise AssertionError(f"native parameters are not in declaration order:\n{native_hover}")
    template_docs = native_hover.index("**Type parameters**")
    if native_hover.index("`T`", template_docs) >= native_hover.index("Policy = void", template_docs):
        raise AssertionError(f"native template parameters are not in declaration order:\n{native_hover}")
    native_signature = response(messages, 21)["signatures"][0]
    require(native_signature["label"], "factor", "2")
    require(native_signature["parameters"][0]["documentation"]["value"], "Multiplication factor.")
    require(native_signature["documentation"]["value"], "The scaled value.")

    native_class = response(messages, 22)["contents"]["value"]
    require(native_class, "RichRecord", "RichBase", "using Id", "enum class State", "value", "struct RichRecord", "Native identity", "size =", "align =", "Declared in", "Imported by", "second paragraph")
    macro_hover = response(messages, 23)["contents"]["value"]
    require(macro_hover, "RICH_MULTIPLY(value, factor)", "Multiply two values", "function-like", "rich_native.hpp")
    macro_signature = response(messages, 24)["signatures"][0]["label"]
    require(macro_signature, "value", "factor")

    overload_hover = response(messages, 29)["contents"]["value"]
    require(overload_hover, "inline int rich_choose(int value) noexcept", "Choose an integer value.")
    if "double rich_choose" in overload_hover:
        raise AssertionError(f"integer call selected the floating overload:\n{overload_hover}")
    overload_signatures = response(messages, 30)
    if len(overload_signatures["signatures"]) != 2 or overload_signatures["activeSignature"] != 0:
        raise AssertionError(f"unexpected overload signature set:\n{overload_signatures}")
    require(overload_signatures["signatures"][0]["label"], "i32")
    require(response(messages, 31)["uri"], "rich_native.hpp")

    c_hover = response(messages, 40)["contents"]["value"]
    require(c_hover, "static inline int rich_c_add", "Add two signed values.", "Left operand.", "Right operand.", "Their signed sum.", "Imported by")
    c_signature = response(messages, 41)["signatures"][0]
    require(c_signature["label"], "left: i32", "right: i32")
    require(c_signature["documentation"]["value"], "Add two signed values.", "Their signed sum.")
    c_class = response(messages, 42)["contents"]["value"]
    require(c_class, "native class c_api.RichCPoint", "struct RichCPoint", "Native identity", "x: i32", "y: i32", "size = 8 bytes", "Declared in", "Imported by", "ordinary C header")
    c_undocumented = response(messages, 43)["contents"]["value"]
    require(c_undocumented, "int rich_c_undocumented(int value)", "Native identity", "Imported by")
    require(response(messages, 44)["uri"], "rich_c.h")

    generated_class = response(messages, 50)["contents"]["value"]
    require(generated_class, "schema.generated.UserMessage", "class UserMessage final", "Native identity", "def id()", "def set_id", "Declared in", "Imported by", "external schema compiler")
    generated_signature = response(messages, 51)["signatures"][0]
    require(generated_signature["label"], "set_id", "value: i32")
    require(generated_signature["documentation"]["value"], "Set the generated numeric identifier.", "New identifier value.")
    generated_method = response(messages, 52)["contents"]["value"]
    require(generated_method, "int id() const noexcept", "generated numeric identifier", "user_message.pb.hpp")
    require(response(messages, 53)["uri"], "user_message.pb.hpp")

    stdlib_class = response(messages, 60)["contents"]["value"]
    require(stdlib_class, "native class std.vector", "using value_type")
    stdlib_signatures = response(messages, 61)["signatures"]
    if len(stdlib_signatures) != 1:
        raise AssertionError(f"std.vector specialization leaked duplicate signatures:\n{stdlib_signatures}")
    require(stdlib_signatures[0]["label"], "def size() -> usize")
    require(response(messages, 62)["uri"], "stl_vector.h")

    generated = response(messages, 25)
    require(generated["language"], "cpp")
    require(generated["source"], "blend")
    require(generated["content"], "blend", "int32_t")
    generated_class = response(messages, 26)
    require(generated_class["source"], "Packet")
    require(generated_class["content"], "struct Packet", "value", "read")
    generated_expression = response(messages, 27)
    require(generated_expression["source"], "blend expression")
    require(generated_expression["content"], "left + right")
    generated_method = response(messages, 28)
    require(generated_method["source"], "Packet.read")
    require(generated_method["content"], "struct Packet", "read")


def run(repo):
    root = pathlib.Path(tempfile.mkdtemp(prefix="dudu_lsp_rich_docs_"))
    try:
        ws = create_workspace(root)
        check_messages(run_session(repo, ws))
        scan_cache = root / "build" / "dudu-header-cache"
        if not list(scan_cache.glob("*.scan")):
            raise AssertionError(f"native scan cache was not written under {scan_cache}")
        check_messages(run_session(repo, ws))
        print("rich documentation LSP scan and disk-cache checks passed")
    finally:
        shutil.rmtree(root, ignore_errors=True)


if __name__ == "__main__":
    run(pathlib.Path(sys.argv[1]).resolve())
