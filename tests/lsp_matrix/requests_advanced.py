from protocol import lsp_message, open_message, position, request, text_document


def full_range(source):
    return {
        "start": {"line": 0, "character": 0},
        "end": {"line": source.count("\n") + 1, "character": 0},
    }


def build_advanced_requests(workspace):
    main = workspace.main
    model = workspace.model
    inheritance = workspace.inheritance
    facade = workspace.facade
    other = workspace.other
    main_source = workspace.main_source
    model_source = workspace.model_source
    inheritance_source = workspace.inheritance_source

    messages = [
        lsp_message(
            {
                "jsonrpc": "2.0",
                "id": 1,
                "method": "initialize",
                "params": {"rootUri": workspace.tmp.as_uri()},
            }
        ),
        lsp_message({"jsonrpc": "2.0", "method": "initialized", "params": {}}),
        open_message(main),
        open_message(model),
        open_message(inheritance),
        open_message(facade),
        open_message(other),
    ]

    queries = [
        (10, "textDocument/definition", main, main_source, "total += item", len("total += ")),
        (11, "textDocument/definition", main, main_source, "total + payload", len("total + ")),
        (12, "textDocument/references", main, main_source, "for item", len("for ")),
        (13, "textDocument/references", main, main_source, "Data(payload)", len("Data(")),
        (14, "textDocument/hover", main, main_source, "total += item", len("total += ")),
        (15, "textDocument/hover", main, main_source, "total + payload", len("total + ")),
        (20, "textDocument/definition", model, model_source, "values: array[T][N]", len("values: array[")),
        (21, "textDocument/definition", model, model_source, "values: array[T][N]", len("values: array[T][")),
        (22, "textDocument/references", model, model_source, "class Buffer[T", len("class Buffer[")),
        (23, "textDocument/references", model, model_source, "class Buffer[T, N", len("class Buffer[T, ")),
        (24, "textDocument/definition", model, model_source, "range(N)", len("range(")),
        (25, "textDocument/hover", model, model_source, "range(N)", len("range(")),
        (26, "textDocument/definition", main, main_source, "api.copy_values", len("api.")),
        (27, "textDocument/definition", main, main_source, "api.FourInts", len("api.")),
        (28, "textDocument/definition", main, main_source, "api.Buffer", len("api.")),
        (29, "textDocument/hover", main, main_source, "copied =", 2),
        (30, "textDocument/hover", main, main_source, "middle =", 2),
        (31, "textDocument/definition", main, main_source, "copied[1:3]", len("copied")),
        (32, "textDocument/signatureHelp", main, main_source, "copy_values(values)", len("copy_values(")),
        (40, "textDocument/definition", inheritance, inheritance_source, "super.transform", len("super.")),
        (41, "textDocument/definition", main, main_source, "processor.transform", len("processor.")),
        (42, "textDocument/definition", main, main_source, "other_processor.transform", len("other_processor.")),
        (43, "textDocument/definition", main, main_source, "source.read", len("source.")),
        (44, "textDocument/references", inheritance, inheritance_source, "def transform", len("def ")),
        (45, "textDocument/references", inheritance, inheritance_source, "def transform", len("def "), 1),
        (46, "textDocument/hover", main, main_source, "processor.transform", len("processor.")),
        (47, "textDocument/completion", main, main_source, "processor.transform", len("processor.")),
        (48, "textDocument/signatureHelp", main, main_source, "processor.transform(buffer", len("processor.transform(")),
        (49, "textDocument/definition", facade, workspace.facade_source, "from model import Buffer", len("from ")),
        (50, "textDocument/definition", facade, workspace.facade_source, "from model import Buffer", len("from model import ")),
    ]
    for query in queries:
        request_id, method, path, source, needle, add, *rest = query
        occurrence = rest[0] if rest else 0
        params = {
            "textDocument": text_document(path),
            "position": position(source, needle, add=add, occurrence=occurrence),
        }
        messages.append(request(request_id, method, params))

    messages.extend(
        [
            request(
                60,
                "textDocument/semanticTokens/full",
                {"textDocument": text_document(main)},
            ),
            request(
                61,
                "textDocument/semanticTokens/full",
                {"textDocument": text_document(model)},
            ),
            request(
                62,
                "textDocument/inlayHint",
                {"textDocument": text_document(main), "range": full_range(main_source)},
            ),
            request(
                63,
                "textDocument/documentSymbol",
                {"textDocument": text_document(inheritance)},
            ),
            request(64, "shutdown", None),
            lsp_message({"jsonrpc": "2.0", "method": "exit", "params": None}),
        ]
    )
    return messages
