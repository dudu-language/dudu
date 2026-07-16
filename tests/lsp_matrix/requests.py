from protocol import lsp_message, open_message, position, request, text_document


def build_requests(workspace):
    tmp = workspace.tmp
    main = workspace.main
    entities = workspace.entities
    ops = workspace.ops
    native = workspace.native
    native_context = workspace.native_context
    native_cpp = workspace.native_cpp
    native_cpp_same = workspace.native_cpp_same
    native_cpp_other = workspace.native_cpp_other
    native_namespace = workspace.native_namespace
    native_namespace_same = workspace.native_namespace_same
    native_namespace_other = workspace.native_namespace_other
    unresolved = workspace.unresolved
    missing = workspace.missing
    available_symbol = workspace.available_symbol
    missing_symbol = workspace.missing_symbol
    alpha = workspace.alpha
    zeta = workspace.zeta
    disorganized_imports = workspace.disorganized_imports
    lint_quickfix = workspace.lint_quickfix
    containers = workspace.containers

    main_source = workspace.main_source
    containers_source = workspace.containers_source
    entities_source = workspace.entities_source
    ops_source = workspace.ops_source
    native_source = workspace.native_source
    native_context_source = workspace.native_context_source
    native_cpp_source = workspace.native_cpp_source
    native_namespace_source = workspace.native_namespace_source
    missing_symbol_diag_range = workspace.missing_symbol_diag_range
    lint_unused_range = workspace.lint_unused_range
    lint_unused_fix_range = workspace.lint_unused_fix_range

    messages = [
        lsp_message({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"rootUri": tmp.as_uri()}}),
        lsp_message({"jsonrpc": "2.0", "method": "initialized", "params": {}}),
        open_message(main),
        open_message(tmp / "math_utils.dd"),
        open_message(containers),
        open_message(entities),
        open_message(ops),
        open_message(native),
        open_message(native_context),
        open_message(native_cpp),
        open_message(native_cpp_same),
        open_message(native_cpp_other),
        open_message(native_namespace),
        open_message(native_namespace_same),
        open_message(native_namespace_other),
        open_message(unresolved),
        open_message(missing),
        open_message(available_symbol),
        open_message(missing_symbol),
        open_message(alpha),
        open_message(zeta),
        open_message(disorganized_imports),
        open_message(lint_quickfix),
        lsp_message({"jsonrpc": "2.0", "method": "textDocument/didSave", "params": {"textDocument": text_document(missing)}}),
        request(10, "textDocument/documentSymbol", {"textDocument": text_document(main)}),
        request(18, "textDocument/documentSymbol", {"textDocument": text_document(entities)}),
        request(11, "workspace/symbol", {"query": "Player"}),
        request(12, "workspace/symbol", {"query": "Mode"}),
        request(13, "workspace/symbol", {"query": "Token"}),
        request(14, "workspace/symbol", {"query": "Box"}),
        request(15, "workspace/symbol", {"query": "MAX_HP"}),
        request(16, "workspace/symbol", {"query": "mix"}),
        request(17, "workspace/symbol", {"query": "add"}),
        request(78, "workspace/symbol", {"query": "Counter.count"}),
        request(20, "textDocument/definition", {"textDocument": text_document(main), "position": position(main_source, "math.mix", add=len("math."))}),
        request(21, "textDocument/definition", {"textDocument": text_document(main), "position": position(main_source, "Player(MAX_HP)", add=1)}),
        request(22, "textDocument/definition", {"textDocument": text_document(main), "position": position(main_source, "player.move", add=len("player."))}),
        request(23, "textDocument/definition", {"textDocument": text_document(main), "position": position(main_source, "player.hp", add=len("player."))}),
        request(24, "textDocument/definition", {"textDocument": text_document(main), "position": position(main_source, "MAX_HP", occurrence=1)}),
        request(25, "textDocument/definition", {"textDocument": text_document(main), "position": position(main_source, "Mode.Play", add=len("Mode."))}),
        request(26, "textDocument/definition", {"textDocument": text_document(main), "position": position(main_source, "Token.IntLit", add=len("Token."))}),
        request(27, "textDocument/definition", {"textDocument": text_document(main), "position": position(main_source, "identity[i32]", add=1)}),
        request(28, "textDocument/definition", {"textDocument": text_document(main), "position": position(main_source, "Box[i32]", add=1)}),
        request(29, "textDocument/definition", {"textDocument": text_document(main), "position": position(main_source, "transitive.transitive_value", add=len("transitive."))}),
        request(49, "textDocument/definition", {"textDocument": text_document(main), "position": position(main_source, "player_id: PlayerId", add=len("player_id: "))}),
        request(125, "textDocument/definition", {"textDocument": text_document(main), "position": position(main_source, "from math_utils import mix", add=len("from "))}),
        request(126, "textDocument/definition", {"textDocument": text_document(main), "position": position(main_source, "from math_utils import mix", add=len("from math_utils import "))}),
        request(68, "textDocument/definition", {"textDocument": text_document(main), "position": position(main_source, "Counter.count", add=len("Counter."))}),
        request(69, "textDocument/definition", {"textDocument": text_document(main), "position": position(main_source, "Counter.LIMIT", add=len("Counter."))}),
        request(30, "textDocument/hover", {"textDocument": text_document(main), "position": position(main_source, "current", occurrence=1)}),
        request(31, "textDocument/hover", {"textDocument": text_document(main), "position": position(main_source, "player: Player", add=1)}),
        request(145, "textDocument/hover", {"textDocument": text_document(main), "position": position(main_source, "player: Player", add=len("player: "))}),
        request(71, "textDocument/hover", {"textDocument": text_document(main), "position": position(main_source, "Counter.count", add=len("Counter."))}),
        request(72, "textDocument/hover", {"textDocument": text_document(main), "position": position(main_source, "Counter.bump", add=len("Counter."))}),
        request(32, "textDocument/references", {"textDocument": text_document(main), "position": position(main_source, "current =")}),
        request(66, "textDocument/references", {"textDocument": text_document(main), "position": position(main_source, "math.mix", add=len("math."))}),
        request(67, "textDocument/references", {"textDocument": text_document(main), "position": position(main_source, "Player(MAX_HP)", add=len("Player("))}),
        request(73, "textDocument/references", {"textDocument": text_document(main), "position": position(main_source, "Counter.count", add=len("Counter."))}),
        request(74, "textDocument/references", {"textDocument": text_document(main), "position": position(main_source, "Counter.bump", add=len("Counter."))}),
        request(111, "textDocument/prepareRename", {"textDocument": text_document(main), "position": position(main_source, "math.mix", add=len("math."))}),
        request(79, "textDocument/rename", {"textDocument": text_document(main), "position": position(main_source, "math.mix", add=len("math.")), "newName": "blend"}),
        request(80, "textDocument/rename", {"textDocument": text_document(main), "position": position(main_source, "Player(MAX_HP)", add=len("Player(")), "newName": "START_HP"}),
        request(33, "textDocument/completion", {"textDocument": text_document(main), "position": position(main_source, "return math.MAGIC", add=len("return math."))}),
        request(34, "textDocument/completion", {"textDocument": text_document(main), "position": position(main_source, "transitive.transitive_value", add=len("transitive."))}),
        request(35, "textDocument/completion", {"textDocument": text_document(main), "position": position(main_source, "player.move", add=len("player."))}),
        request(75, "textDocument/completion", {"textDocument": text_document(main), "position": position(main_source, "Counter.bump", add=len("Counter."))}),
        request(119, "textDocument/completion", {"textDocument": text_document(main), "position": position(main_source, "Mode.Play", add=len("Mode."))}),
        request(120, "textDocument/completion", {"textDocument": text_document(main), "position": position(main_source, "Token.IntLit", add=len("Token."))}),
        request(76, "textDocument/signatureHelp", {"textDocument": text_document(main), "position": position(main_source, "Counter.bump()", add=len("Counter.bump("))}),
        request(77, "textDocument/signatureHelp", {"textDocument": text_document(main), "position": position(main_source, "Player(MAX_HP)", add=len("Player("))}),
        request(36, "textDocument/signatureHelp", {"textDocument": text_document(main), "position": position(main_source, "math.mix(current", add=len("math.mix(current"))}),
        request(37, "textDocument/hover", {"textDocument": text_document(main), "position": position(main_source, "import entities", add=len("import "))}),
        request(38, "textDocument/references", {"textDocument": text_document(entities), "position": position(entities_source, "hp: i32", add=1)}),
        request(39, "textDocument/references", {"textDocument": text_document(entities), "position": position(entities_source, "    Play", add=5)}),
        request(42, "textDocument/references", {"textDocument": text_document(main), "position": position(main_source, "player.hp", add=len("player."))}),
        request(43, "textDocument/references", {"textDocument": text_document(main), "position": position(main_source, "player.move", add=len("player."))}),
        request(44, "textDocument/references", {"textDocument": text_document(entities), "position": position(entities_source, "move(self")}),
        request(45, "textDocument/hover", {"textDocument": text_document(main), "position": position(main_source, "player.hp", add=len("player."))}),
        request(46, "textDocument/semanticTokens/full", {"textDocument": text_document(main)}),
        request(84, "textDocument/semanticTokens/full", {"textDocument": text_document(native)}),
        request(85, "textDocument/semanticTokens/full", {"textDocument": text_document(entities)}),
        request(47, "textDocument/hover", {"textDocument": text_document(main), "position": position(main_source, "Mode.Play", add=len("Mode."))}),
        request(48, "textDocument/hover", {"textDocument": text_document(main), "position": position(main_source, "player_id: PlayerId", add=len("player_id: "))}),
        request(62, "textDocument/hover", {"textDocument": text_document(main), "position": position(main_source, "Token.IntLit", add=len("Token."))}),
        request(130, "textDocument/hover", {"textDocument": text_document(main), "position": position(main_source, "def main() -> i32", add=len("def main() -> "))}),
        request(132, "textDocument/hover", {"textDocument": text_document(containers), "position": position(containers_source, "list[i32]", add=1)}),
        request(63, "textDocument/references", {"textDocument": text_document(main), "position": position(main_source, "Token.IntLit", add=len("Token."))}),
        request(40, "textDocument/documentSymbol", {"textDocument": text_document(ops)}),
        request(142, "textDocument/semanticTokens/full", {"textDocument": text_document(ops)}),
        request(131, "textDocument/definition", {"textDocument": text_document(tmp / "math_utils.dd"), "position": position((tmp / "math_utils.dd").read_text(), "mix(left", add=1)}),
        request(41, "textDocument/definition", {"textDocument": text_document(ops), "position": position(ops_source, "add(self", add=1)}),
        request(143, "textDocument/hover", {"textDocument": text_document(ops), "position": position(ops_source, "@operator", add=1)}),
        request(144, "textDocument/definition", {"textDocument": text_document(ops), "position": position(ops_source, "@operator", add=1)}),
        request(146, "textDocument/definition", {"textDocument": text_document(ops), "position": position(ops_source, "self.x + other.x", add=1)}),
        request(147, "textDocument/definition", {"textDocument": text_document(ops), "position": position(ops_source, "self.x + other.x", add=len("self."))}),
        request(148, "textDocument/definition", {"textDocument": text_document(ops), "position": position(ops_source, "other.x", add=1)}),
        request(149, "textDocument/definition", {"textDocument": text_document(ops), "position": position(ops_source, "other.x", add=len("other."))}),
        request(150, "textDocument/hover", {"textDocument": text_document(ops), "position": position(ops_source, "self.x + other.x", add=1)}),
        request(151, "textDocument/hover", {"textDocument": text_document(ops), "position": position(ops_source, "other.x", add=1)}),
        request(152, "textDocument/hover", {"textDocument": text_document(ops), "position": position(ops_source, "for i in range", add=len("for "))}),
        request(153, "textDocument/hover", {"textDocument": text_document(ops), "position": position(ops_source, "range(2)", add=1)}),
        request(154, "textDocument/hover", {"textDocument": text_document(ops), "position": position(ops_source, "numbers.append(1)", add=len("numbers."))}),
        request(155, "textDocument/hover", {"textDocument": text_document(ops), "position": position(ops_source, "if len", add=1)}),
        request(156, "textDocument/hover", {"textDocument": text_document(ops), "position": position(ops_source, "while j", add=1)}),
        request(157, "textDocument/hover", {"textDocument": text_document(ops), "position": position(ops_source, "len(numbers)", add=1)}),
        request(158, "textDocument/hover", {"textDocument": text_document(ops), "position": position(ops_source, "numbers.back()", add=len("numbers."))}),
        request(94, "textDocument/definition", {"textDocument": text_document(ops), "position": position(ops_source, "left + right", add=len("left "))}),
        request(95, "textDocument/hover", {"textDocument": text_document(ops), "position": position(ops_source, "left + right", add=len("left "))}),
        request(96, "textDocument/references", {"textDocument": text_document(ops), "position": position(ops_source, "left + right", add=len("left "))}),
        request(97, "textDocument/hover", {"textDocument": text_document(main), "position": position(main_source, "identity[i32]", add=1)}),
        request(98, "textDocument/references", {"textDocument": text_document(main), "position": position(main_source, "identity[i32]", add=1)}),
        request(99, "textDocument/definition", {"textDocument": text_document(main), "position": position(main_source, "box.echo", add=len("box."))}),
        request(100, "textDocument/hover", {"textDocument": text_document(main), "position": position(main_source, "box.echo", add=len("box."))}),
        request(101, "textDocument/references", {"textDocument": text_document(main), "position": position(main_source, "box.echo", add=len("box."))}),
        request(102, "textDocument/hover", {"textDocument": text_document(main), "position": position(main_source, "Box[i32]", add=1)}),
        request(50, "textDocument/completion", {"textDocument": text_document(native), "position": position(native_source, "nb.matrix_native_add", add=len("nb."))}),
        request(51, "textDocument/hover", {"textDocument": text_document(native), "position": position(native_source, "nb.MatrixNativePoint", add=len("nb."))}),
        request(52, "textDocument/definition", {"textDocument": text_document(native), "position": position(native_source, "nb.matrix_native_add", add=len("nb."))}),
        request(53, "textDocument/hover", {"textDocument": text_document(native), "position": position(native_source, "nb.DUDU_MATRIX_NATIVE_SCALE", add=len("nb."))}),
        request(54, "textDocument/references", {"textDocument": text_document(native), "position": position(native_source, "nb.matrix_native_add", add=len("nb."))}),
        request(128, "textDocument/definition", {"textDocument": text_document(native), "position": position(native_source, "native_bridge.h")}),
        request(129, "textDocument/definition", {"textDocument": text_document(native), "position": position(native_source, " as nb", add=len(" as "))}),
        request(121, "textDocument/definition", {"textDocument": text_document(native), "position": position(native_source, "nb.DUDU_MATRIX_NATIVE_SCALE", add=len("nb."))}),
        request(122, "textDocument/references", {"textDocument": text_document(native), "position": position(native_source, "nb.DUDU_MATRIX_NATIVE_SCALE", add=len("nb."))}),
        request(123, "textDocument/signatureHelp", {"textDocument": text_document(native), "position": position(native_source, "nb.DUDU_MATRIX_NATIVE_SCALE(2)", add=len("nb.DUDU_MATRIX_NATIVE_SCALE("))}),
        request(124, "textDocument/documentSymbol", {"textDocument": text_document(native)}),
        request(55, "textDocument/signatureHelp", {"textDocument": text_document(native), "position": position(native_source, "nb.matrix_native_add(point.x", add=len("nb.matrix_native_add(point.x"))}),
        request(112, "textDocument/prepareRename", {"textDocument": text_document(native), "position": position(native_source, "nb.matrix_native_add", add=len("nb."))}),
        request(91, "textDocument/hover", {"textDocument": text_document(native), "position": position(native_source, "nb.MATRIX_MODE_FAST", add=len("nb."))}),
        request(92, "textDocument/definition", {"textDocument": text_document(native), "position": position(native_source, "nb.MATRIX_MODE_FAST", add=len("nb."))}),
        request(93, "textDocument/references", {"textDocument": text_document(native), "position": position(native_source, "nb.MATRIX_MODE_FAST", add=len("nb."))}),
        request(127, "textDocument/definition", {"textDocument": text_document(native), "position": position(native_source, "wrapped: *const[nb.MatrixNativePoint]", add=len("wrapped: *const[nb."))}),
        request(116, "textDocument/hover", {"textDocument": text_document(native), "position": position(native_source, "point.x", add=len("point."))}),
        request(117, "textDocument/definition", {"textDocument": text_document(native), "position": position(native_source, "point.x", add=len("point."))}),
        request(118, "textDocument/references", {"textDocument": text_document(native), "position": position(native_source, "point.x", add=len("point."))}),
        request(113, "textDocument/definition", {"textDocument": text_document(native_context), "position": position(native_context_source, "value.count", add=len("value."))}),
        request(114, "textDocument/hover", {"textDocument": text_document(native_context), "position": position(native_context_source, "value.count", add=len("value."))}),
        request(115, "textDocument/references", {"textDocument": text_document(native_context), "position": position(native_context_source, "value.count", add=len("value."))}),
        request(56, "textDocument/completion", {"textDocument": text_document(native_cpp), "position": position(native_cpp_source, "widget.value", add=len("widget."))}),
        request(57, "textDocument/hover", {"textDocument": text_document(native_cpp), "position": position(native_cpp_source, "widget.value", add=len("widget."))}),
        request(58, "textDocument/signatureHelp", {"textDocument": text_document(native_cpp), "position": position(native_cpp_source, "widget.scaled(2", add=len("widget.scaled(2"))}),
        request(59, "textDocument/definition", {"textDocument": text_document(native_cpp), "position": position(native_cpp_source, "widget.value", add=len("widget."))}),
        request(60, "textDocument/references", {"textDocument": text_document(native_cpp), "position": position(native_cpp_source, "widget.value", add=len("widget."))}),
        request(61, "textDocument/hover", {"textDocument": text_document(native_cpp), "position": position(native_cpp_source, "widget: MatrixWidget", add=len("widget: "))}),
        request(64, "textDocument/definition", {"textDocument": text_document(native_cpp), "position": position(native_cpp_source, "widget.scaled", add=len("widget."))}),
        request(65, "textDocument/references", {"textDocument": text_document(native_cpp), "position": position(native_cpp_source, "widget.scaled", add=len("widget."))}),
        request(81, "textDocument/signatureHelp", {"textDocument": text_document(native_cpp), "position": position(native_cpp_source, "MatrixWidget(5)", add=len("MatrixWidget("))}),
        request(82, "textDocument/definition", {"textDocument": text_document(native_cpp), "position": position(native_cpp_source, "MatrixWidget(5)", add=1)}),
        request(83, "textDocument/hover", {"textDocument": text_document(native_cpp), "position": position(native_cpp_source, "MatrixWidget(5)", add=1)}),
        request(86, "textDocument/hover", {"textDocument": text_document(native_namespace), "position": position(native_namespace_source, "matrix_space.namespaced_add", add=1)}),
        request(87, "textDocument/definition", {"textDocument": text_document(native_namespace), "position": position(native_namespace_source, "matrix_space.namespaced_add", add=1)}),
        request(88, "textDocument/completion", {"textDocument": text_document(native_namespace), "position": position(native_namespace_source, "matrix_space.namespaced_add", add=len("matrix_space."))}),
        request(89, "textDocument/semanticTokens/full", {"textDocument": text_document(native_namespace)}),
        request(90, "textDocument/references", {"textDocument": text_document(native_namespace), "position": position(native_namespace_source, "matrix_space.namespaced_add", add=1)}),
        request(103, "textDocument/hover", {"textDocument": text_document(native_namespace), "position": position(native_namespace_source, "matrix_space.identity", add=len("matrix_space."))}),
        request(104, "textDocument/definition", {"textDocument": text_document(native_namespace), "position": position(native_namespace_source, "matrix_space.identity", add=len("matrix_space."))}),
        request(105, "textDocument/references", {"textDocument": text_document(native_namespace), "position": position(native_namespace_source, "matrix_space.identity", add=len("matrix_space."))}),
        request(106, "textDocument/signatureHelp", {"textDocument": text_document(native_namespace), "position": position(native_namespace_source, "matrix_space.identity[i32](4)", add=len("matrix_space.identity[i32]("))}),
        request(107, "textDocument/codeAction", {
            "textDocument": text_document(missing_symbol),
            "range": missing_symbol_diag_range,
            "context": {
                "diagnostics": [{
                    "range": missing_symbol_diag_range,
                    "severity": 1,
                    "source": "dudu/sema",
                    "code": "dudu.sema.unknown_identifier",
                    "message": "unknown identifier: MissingThing",
                    "data": {"name": "MissingThing"},
                }]
            },
        }),
        request(108, "textDocument/codeAction", {
            "textDocument": text_document(disorganized_imports),
            "range": {"start": {"line": 0, "character": 0}, "end": {"line": 1, "character": len("import alpha")}},
            "context": {"diagnostics": []},
        }),
        request(109, "textDocument/codeAction", {
            "textDocument": text_document(lint_quickfix),
            "range": lint_unused_range,
            "context": {
                "diagnostics": [{
                    "range": lint_unused_range,
                    "severity": 2,
                    "source": "dudu/lint",
                    "code": "dudu.lint.unused",
                    "message": "unused local: unused",
                    "data": {"fixRange": lint_unused_fix_range},
                }]
            },
        }),
        request(110, "completionItem/resolve", {
            "label": "mix",
            "kind": 3,
            "detail": "def mix(left: i32, right: i32) -> i32",
            "documentation": {
                "kind": "markdown",
                "value": "Mixes two numbers for signature docs.",
            },
        }),
        request(70, "textDocument/semanticTokens/full", {"textDocument": text_document(unresolved)}),
        request(900, "shutdown", None),
        lsp_message({"jsonrpc": "2.0", "method": "exit", "params": None}),
    ]

    return messages
