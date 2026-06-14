const vscode = require("vscode");
const childProcess = require("child_process");
const path = require("path");

let terminal;
let diagnostics;
let lsp;

function shellQuote(value) {
  return `'${value.replace(/'/g, "'\\''")}'`;
}

function workspaceDirectory() {
  const folder = vscode.workspace.workspaceFolders?.[0];
  return folder ? folder.uri.fsPath : process.cwd();
}

function ducPath() {
  return vscode.workspace.getConfiguration("dudu").get("ducPath", "duc");
}

function activeDuduFile() {
  const editor = vscode.window.activeTextEditor;
  if (!editor) {
    vscode.window.showErrorMessage("Open a .dd file first.");
    return undefined;
  }
  if (editor.document.languageId !== "dudu" && !editor.document.fileName.endsWith(".dd")) {
    vscode.window.showErrorMessage("The active file is not a Dudu .dd file.");
    return undefined;
  }
  return editor.document.fileName;
}

function runCommand(command) {
  if (!terminal || terminal.exitStatus) {
    terminal = vscode.window.createTerminal("Dudu");
  }
  terminal.show();
  terminal.sendText(`cd ${shellQuote(workspaceDirectory())} && ${command}`);
}

function isDuduDocument(document) {
  return document.languageId === "dudu" || document.fileName.endsWith(".dd");
}

function diagnosticSeverity(value) {
  if (value === 2) {
    return vscode.DiagnosticSeverity.Warning;
  }
  if (value === 3) {
    return vscode.DiagnosticSeverity.Information;
  }
  if (value === 4) {
    return vscode.DiagnosticSeverity.Hint;
  }
  return vscode.DiagnosticSeverity.Error;
}

function toVscodeDiagnostic(item) {
  const start = item.range?.start ?? { line: 0, character: 0 };
  const end = item.range?.end ?? start;
  const diagnostic = new vscode.Diagnostic(
    new vscode.Range(start.line, start.character, end.line, end.character),
    item.message ?? "Dudu diagnostic",
    diagnosticSeverity(item.severity),
  );
  diagnostic.source = item.source ?? "dudu";
  return diagnostic;
}

class DuduLspClient {
  constructor(context) {
    this.context = context;
    this.process = undefined;
    this.nextId = 1;
    this.pending = new Map();
    this.buffer = Buffer.alloc(0);
  }

  start() {
    if (this.process) {
      return;
    }
    this.process = childProcess.spawn(ducPath(), ["lsp"], {
      cwd: workspaceDirectory(),
      stdio: ["pipe", "pipe", "pipe"],
    });
    this.process.stdout.on("data", (chunk) => this.onData(chunk));
    this.process.stderr.on("data", (chunk) => {
      const text = chunk.toString().trim();
      if (text) {
        console.error(`dudu lsp: ${text}`);
      }
    });
    this.process.on("error", (error) => {
      vscode.window.showErrorMessage(`Could not start duc lsp: ${error.message}`);
      for (const { reject } of this.pending.values()) {
        reject(error);
      }
      this.pending.clear();
    });
    this.process.on("exit", () => {
      this.process = undefined;
      for (const { reject } of this.pending.values()) {
        reject(new Error("duc lsp exited"));
      }
      this.pending.clear();
    });
    this.request("initialize", {
      processId: process.pid,
      rootUri: vscode.workspace.workspaceFolders?.[0]?.uri.toString(),
      capabilities: {},
    }).catch((error) => vscode.window.showErrorMessage(`Dudu LSP failed: ${error.message}`));
  }

  stop() {
    if (!this.process) {
      return;
    }
    this.request("shutdown", null)
      .catch(() => undefined)
      .finally(() => {
        this.notify("exit", null);
        this.process?.kill();
        this.process = undefined;
      });
  }

  send(payload) {
    this.start();
    const body = JSON.stringify(payload);
    this.process.stdin.write(`Content-Length: ${Buffer.byteLength(body, "utf8")}\r\n\r\n${body}`);
  }

  request(method, params) {
    const id = this.nextId++;
    const promise = new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
    });
    this.send({ jsonrpc: "2.0", id, method, params });
    return promise;
  }

  notify(method, params) {
    this.send({ jsonrpc: "2.0", method, params });
  }

  didOpen(document) {
    if (!isDuduDocument(document)) {
      return;
    }
    this.notify("textDocument/didOpen", {
      textDocument: {
        uri: document.uri.toString(),
        languageId: "dudu",
        version: document.version,
        text: document.getText(),
      },
    });
  }

  didChange(document) {
    if (!isDuduDocument(document)) {
      return;
    }
    this.notify("textDocument/didChange", {
      textDocument: { uri: document.uri.toString(), version: document.version },
      contentChanges: [{ text: document.getText() }],
    });
  }

  didSave(document) {
    if (!isDuduDocument(document)) {
      return;
    }
    this.notify("textDocument/didSave", {
      textDocument: { uri: document.uri.toString() },
    });
  }

  async format(document) {
    const edits = await this.request("textDocument/formatting", {
      textDocument: { uri: document.uri.toString() },
      options: { tabSize: 4, insertSpaces: true },
    });
    return edits.map(
      (edit) =>
        new vscode.TextEdit(
          new vscode.Range(
            edit.range.start.line,
            edit.range.start.character,
            edit.range.end.line,
            edit.range.end.character,
          ),
          edit.newText,
        ),
    );
  }

  async documentSymbols(document) {
    const symbols = await this.request("textDocument/documentSymbol", {
      textDocument: { uri: document.uri.toString() },
    });
    return symbols.map((symbol) => {
      const range = toRange(symbol.location.range);
      const item = new vscode.SymbolInformation(
        symbol.name,
        symbolKind(symbol.kind),
        symbol.detail ?? "",
        new vscode.Location(vscode.Uri.parse(symbol.location.uri), range),
      );
      return item;
    });
  }

  async definition(document, position) {
    const location = await this.request("textDocument/definition", {
      textDocument: { uri: document.uri.toString() },
      position: { line: position.line, character: position.character },
    });
    if (!location) {
      return undefined;
    }
    return new vscode.Location(vscode.Uri.parse(location.uri), toRange(location.range));
  }

  async hover(document, position) {
    const hover = await this.request("textDocument/hover", {
      textDocument: { uri: document.uri.toString() },
      position: { line: position.line, character: position.character },
    });
    if (!hover) {
      return undefined;
    }
    return new vscode.Hover(new vscode.MarkdownString(hover.contents.value), toRange(hover.range));
  }

  async completion(document, position) {
    const items = await this.request("textDocument/completion", {
      textDocument: { uri: document.uri.toString() },
      position: { line: position.line, character: position.character },
    });
    return items.map((item) => {
      const completion = new vscode.CompletionItem(item.label, completionKind(item.kind));
      completion.detail = item.detail;
      return completion;
    });
  }

  onData(chunk) {
    this.buffer = Buffer.concat([this.buffer, chunk]);
    while (true) {
      const headerEnd = this.buffer.indexOf("\r\n\r\n");
      if (headerEnd < 0) {
        return;
      }
      const header = this.buffer.slice(0, headerEnd).toString();
      const lengthLine = header
        .split("\r\n")
        .find((line) => line.toLowerCase().startsWith("content-length:"));
      if (!lengthLine) {
        this.buffer = this.buffer.slice(headerEnd + 4);
        continue;
      }
      const length = Number(lengthLine.split(":", 2)[1].trim());
      const bodyStart = headerEnd + 4;
      const bodyEnd = bodyStart + length;
      if (this.buffer.length < bodyEnd) {
        return;
      }
      const body = this.buffer.slice(bodyStart, bodyEnd).toString();
      this.buffer = this.buffer.slice(bodyEnd);
      this.handleMessage(JSON.parse(body));
    }
  }

  handleMessage(message) {
    if (message.id !== undefined) {
      const pending = this.pending.get(message.id);
      if (pending) {
        this.pending.delete(message.id);
        if (message.error) {
          pending.reject(new Error(message.error.message ?? "Dudu LSP request failed"));
        } else {
          pending.resolve(message.result);
        }
      }
      return;
    }
    if (message.method === "textDocument/publishDiagnostics") {
      const uri = vscode.Uri.parse(message.params.uri);
      diagnostics.set(uri, (message.params.diagnostics ?? []).map(toVscodeDiagnostic));
    }
  }
}

function toRange(range) {
  return new vscode.Range(
    range.start.line,
    range.start.character,
    range.end.line,
    range.end.character,
  );
}

function symbolKind(lspKind) {
  return Math.max(0, Number(lspKind ?? 13) - 1);
}

function completionKind(lspKind) {
  return Math.max(0, Number(lspKind ?? 1) - 1);
}

function activate(context) {
  diagnostics = vscode.languages.createDiagnosticCollection("dudu");
  lsp = new DuduLspClient(context);
  lsp.start();

  for (const document of vscode.workspace.textDocuments) {
    lsp.didOpen(document);
  }

  context.subscriptions.push(
    diagnostics,
    vscode.languages.registerDocumentFormattingEditProvider("dudu", {
      provideDocumentFormattingEdits(document) {
        return lsp.format(document);
      },
    }),
    vscode.languages.registerDocumentSymbolProvider("dudu", {
      provideDocumentSymbols(document) {
        return lsp.documentSymbols(document);
      },
    }),
    vscode.languages.registerDefinitionProvider("dudu", {
      provideDefinition(document, position) {
        return lsp.definition(document, position);
      },
    }),
    vscode.languages.registerHoverProvider("dudu", {
      provideHover(document, position) {
        return lsp.hover(document, position);
      },
    }),
    vscode.languages.registerCompletionItemProvider("dudu", {
      provideCompletionItems(document, position) {
        return lsp.completion(document, position);
      },
    }, "."),
    vscode.commands.registerCommand("dudu.fmtFile", async () => {
      const editor = vscode.window.activeTextEditor;
      if (editor && isDuduDocument(editor.document)) {
        await vscode.commands.executeCommand("editor.action.formatDocument");
      }
    }),
    vscode.commands.registerCommand("dudu.checkFile", () => {
      const editor = vscode.window.activeTextEditor;
      if (editor && isDuduDocument(editor.document)) {
        lsp.didSave(editor.document);
      }
    }),
    vscode.commands.registerCommand("dudu.buildProject", () => {
      runCommand(`${shellQuote(ducPath())} build`);
    }),
    vscode.commands.registerCommand("dudu.runFile", () => {
      const file = activeDuduFile();
      if (file) {
        runCommand(`${shellQuote(ducPath())} run ${shellQuote(file)}`);
      }
    }),
    vscode.workspace.onDidOpenTextDocument((document) => lsp.didOpen(document)),
    vscode.workspace.onDidChangeTextDocument((event) => lsp.didChange(event.document)),
    vscode.workspace.onDidSaveTextDocument((document) => lsp.didSave(document)),
  );
}

function deactivate() {
  lsp?.stop();
  diagnostics?.dispose();
}

module.exports = { activate, deactivate };
