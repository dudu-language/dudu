const vscode = require("vscode");
const childProcess = require("child_process");
const fs = require("fs");
const path = require("path");

let terminal;
let diagnostics;
let lsp;
let statusItem;

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

function updateStatus() {
  if (!statusItem) {
    return;
  }
  const state = lsp?.process ? "LSP" : "LSP stopped";
  const nativeState = lsp?.hasNativeHeaderProblem() ? "native headers failing" : "native headers ok";
  const target = targetStatus();
  statusItem.text = `$(symbol-method) Dudu: ${state}`;
  statusItem.tooltip = `duc: ${ducPath()}\ntarget: ${target}\n${nativeState}`;
  statusItem.show();
}

function targetStatus() {
  const config = findProjectConfig(workspaceDirectory());
  if (!config) {
    return "default executable/hosted";
  }
  const text = fs.readFileSync(config, "utf8");
  let section = "";
  let kind = "executable";
  let mode = "hosted";
  for (const rawLine of text.split(/\r?\n/)) {
    const line = rawLine.replace(/#.*/, "").trim();
    if (!line) {
      continue;
    }
    if (line.startsWith("[") && line.endsWith("]")) {
      section = line.slice(1, -1).trim();
      continue;
    }
    if (section !== "target") {
      continue;
    }
    const match = line.match(/^([A-Za-z_][A-Za-z0-9_]*)\s*=\s*"?([^"]+)"?\s*$/);
    if (!match) {
      continue;
    }
    if (match[1] === "kind") {
      kind = match[2];
    } else if (match[1] === "mode") {
      mode = match[2];
    }
  }
  return `${kind}/${mode}`;
}

function findProjectConfig(start) {
  let current = path.resolve(start);
  while (true) {
    const candidate = path.join(current, "dudu.toml");
    if (fs.existsSync(candidate)) {
      return candidate;
    }
    const parent = path.dirname(current);
    if (parent === current) {
      return undefined;
    }
    current = parent;
  }
}

class DuduLspClient {
  constructor(context) {
    this.context = context;
    this.process = undefined;
    this.nextId = 1;
    this.pending = new Map();
    this.buffer = Buffer.alloc(0);
    this.nativeHeaderProblems = new Map();
    this.changeTimers = new Map();
  }

  start() {
    if (this.process) {
      return;
    }
    this.process = childProcess.spawn(ducPath(), ["lsp"], {
      cwd: workspaceDirectory(),
      stdio: ["pipe", "pipe", "pipe"],
    });
    updateStatus();
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
      for (const { timer } of this.pending.values()) {
        clearTimeout(timer);
      }
      this.pending.clear();
      updateStatus();
    });
    this.process.on("exit", () => {
      this.process = undefined;
      for (const { reject } of this.pending.values()) {
        reject(new Error("duc lsp exited"));
      }
      for (const { timer } of this.pending.values()) {
        clearTimeout(timer);
      }
      this.pending.clear();
      updateStatus();
    });
    this.request("initialize", {
      processId: process.pid,
      rootUri: vscode.workspace.workspaceFolders?.[0]?.uri.toString(),
      capabilities: {},
    }).catch((error) => vscode.window.showErrorMessage(`Dudu LSP failed: ${error.message}`));
  }

  stop() {
    for (const timer of this.changeTimers.values()) {
      clearTimeout(timer);
    }
    this.changeTimers.clear();
    if (!this.process) {
      return;
    }
    this.request("shutdown", null)
      .catch(() => undefined)
      .finally(() => {
        this.notify("exit", null);
        this.process?.kill();
        this.process = undefined;
        updateStatus();
      });
  }

  hasNativeHeaderProblem() {
    return [...this.nativeHeaderProblems.values()].some(Boolean);
  }

  send(payload) {
    this.start();
    const body = JSON.stringify(payload);
    this.process.stdin.write(`Content-Length: ${Buffer.byteLength(body, "utf8")}\r\n\r\n${body}`);
  }

  request(method, params) {
    const id = this.nextId++;
    const promise = new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error(`Dudu LSP request timed out: ${method}`));
      }, 10000);
      this.pending.set(id, { resolve, reject, timer });
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
    const uri = document.uri.toString();
    const existing = this.changeTimers.get(uri);
    if (existing) {
      clearTimeout(existing);
    }
    this.changeTimers.set(
      uri,
      setTimeout(() => {
        this.changeTimers.delete(uri);
        this.sendDidChange(document);
      }, 250),
    );
  }

  sendDidChange(document) {
    this.notify("textDocument/didChange", {
      textDocument: { uri: document.uri.toString(), version: document.version },
      contentChanges: [{ text: document.getText() }],
    });
  }

  didSave(document) {
    if (!isDuduDocument(document)) {
      return;
    }
    const uri = document.uri.toString();
    const existing = this.changeTimers.get(uri);
    if (existing) {
      clearTimeout(existing);
      this.changeTimers.delete(uri);
      this.sendDidChange(document);
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

  async references(document, position) {
    const locations = await this.request("textDocument/references", {
      textDocument: { uri: document.uri.toString() },
      position: { line: position.line, character: position.character },
      context: { includeDeclaration: true },
    });
    return locations.map(
      (location) => new vscode.Location(vscode.Uri.parse(location.uri), toRange(location.range)),
    );
  }

  async rename(document, position, newName) {
    const edit = await this.request("textDocument/rename", {
      textDocument: { uri: document.uri.toString() },
      position: { line: position.line, character: position.character },
      newName,
    });
    if (!edit) {
      return undefined;
    }
    const workspaceEdit = new vscode.WorkspaceEdit();
    for (const [uriText, edits] of Object.entries(edit.changes ?? {})) {
      const uri = vscode.Uri.parse(uriText);
      for (const item of edits) {
        workspaceEdit.replace(uri, toRange(item.range), item.newText);
      }
    }
    return workspaceEdit;
  }

  async codeActions(document, range, context) {
    const actions = await this.request("textDocument/codeAction", {
      textDocument: { uri: document.uri.toString() },
      range: {
        start: { line: range.start.line, character: range.start.character },
        end: { line: range.end.line, character: range.end.character },
      },
      context: {
        diagnostics: context.diagnostics.map((diagnostic) => ({
          range: {
            start: {
              line: diagnostic.range.start.line,
              character: diagnostic.range.start.character,
            },
            end: {
              line: diagnostic.range.end.line,
              character: diagnostic.range.end.character,
            },
          },
          message: diagnostic.message,
          source: diagnostic.source,
        })),
      },
    });
    return actions.map((item) => {
      const action = new vscode.CodeAction(item.title, codeActionKind(item.kind));
      if (item.edit) {
        action.edit = workspaceEditFromLsp(item.edit);
      }
      if (item.command) {
        action.command = {
          title: item.command.title,
          command: item.command.command,
          arguments: item.command.arguments ?? [],
        };
      }
      return action;
    });
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
      completion.insertText =
        item.insertTextFormat === 2 ? new vscode.SnippetString(item.insertText) : item.insertText;
      completion.data = item;
      return completion;
    });
  }

  async resolveCompletion(item) {
    const resolved = await this.request("completionItem/resolve", item.data ?? item);
    item.detail = resolved.detail ?? item.detail;
    if (resolved.documentation?.value) {
      item.documentation = new vscode.MarkdownString(resolved.documentation.value);
    }
    return item;
  }

  async signatureHelp(document, position) {
    const help = await this.request("textDocument/signatureHelp", {
      textDocument: { uri: document.uri.toString() },
      position: { line: position.line, character: position.character },
    });
    const result = new vscode.SignatureHelp();
    result.activeSignature = help.activeSignature ?? 0;
    result.activeParameter = help.activeParameter ?? 0;
    result.signatures = (help.signatures ?? []).map(
      (signature) => new vscode.SignatureInformation(signature.label),
    );
    return result;
  }

  async workspaceSymbols(query) {
    const symbols = await this.request("workspace/symbol", { query });
    return symbols.map((symbol) => {
      const range = toRange(symbol.location.range);
      return new vscode.SymbolInformation(
        symbol.name,
        symbolKind(symbol.kind),
        symbol.detail ?? "",
        new vscode.Location(vscode.Uri.parse(symbol.location.uri), range),
      );
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
      try {
        this.handleMessage(JSON.parse(body));
      } catch (error) {
        this.failPending(error);
      }
    }
  }

  failPending(error) {
    for (const { reject, timer } of this.pending.values()) {
      clearTimeout(timer);
      reject(error);
    }
    this.pending.clear();
  }

  handleMessage(message) {
    if (message.id !== undefined) {
      const pending = this.pending.get(message.id);
      if (pending) {
        this.pending.delete(message.id);
        clearTimeout(pending.timer);
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
      const items = message.params.diagnostics ?? [];
      this.nativeHeaderProblems.set(
        uri.toString(),
        items.some((item) => item.source === "dudu/native-header"),
      );
      diagnostics.set(uri, items.map(toVscodeDiagnostic));
      updateStatus();
    }
  }
}

function workspaceEditFromLsp(edit) {
  const workspaceEdit = new vscode.WorkspaceEdit();
  for (const [uriText, edits] of Object.entries(edit.changes ?? {})) {
    const uri = vscode.Uri.parse(uriText);
    for (const item of edits) {
      workspaceEdit.replace(uri, toRange(item.range), item.newText);
    }
  }
  return workspaceEdit;
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

function codeActionKind(kind) {
  if (kind === "quickfix") {
    return vscode.CodeActionKind.QuickFix;
  }
  if (kind === "source.format") {
    return vscode.CodeActionKind.Source.append("format");
  }
  return kind ? new vscode.CodeActionKind(kind) : vscode.CodeActionKind.Empty;
}

function activate(context) {
  diagnostics = vscode.languages.createDiagnosticCollection("dudu");
  statusItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 90);
  lsp = new DuduLspClient(context);
  lsp.start();

  for (const document of vscode.workspace.textDocuments) {
    lsp.didOpen(document);
  }

  context.subscriptions.push(
    diagnostics,
    statusItem,
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
    vscode.languages.registerReferenceProvider("dudu", {
      provideReferences(document, position) {
        return lsp.references(document, position);
      },
    }),
    vscode.languages.registerRenameProvider("dudu", {
      provideRenameEdits(document, position, newName) {
        return lsp.rename(document, position, newName);
      },
    }),
    vscode.languages.registerCodeActionsProvider(
      "dudu",
      {
        provideCodeActions(document, range, context) {
          return lsp.codeActions(document, range, context);
        },
      },
      {
        providedCodeActionKinds: [
          vscode.CodeActionKind.QuickFix,
          vscode.CodeActionKind.Source.append("format"),
          vscode.CodeActionKind.SourceOrganizeImports,
        ],
      },
    ),
    vscode.languages.registerHoverProvider("dudu", {
      provideHover(document, position) {
        return lsp.hover(document, position);
      },
    }),
    vscode.languages.registerCompletionItemProvider("dudu", {
      provideCompletionItems(document, position) {
        return lsp.completion(document, position);
      },
      resolveCompletionItem(item) {
        return lsp.resolveCompletion(item);
      },
    }, "."),
    vscode.languages.registerSignatureHelpProvider(
      "dudu",
      {
        provideSignatureHelp(document, position) {
          return lsp.signatureHelp(document, position);
        },
      },
      "(",
      ",",
    ),
    vscode.languages.registerWorkspaceSymbolProvider({
      provideWorkspaceSymbols(query) {
        return lsp.workspaceSymbols(query);
      },
    }),
    vscode.commands.registerCommand("dudu.fmtFile", async () => {
      const editor = vscode.window.activeTextEditor;
      if (editor && isDuduDocument(editor.document)) {
        await vscode.commands.executeCommand("editor.action.formatDocument");
      }
    }),
    vscode.commands.registerCommand("dudu.checkFile", () => {
      const file = activeDuduFile();
      if (file) {
        runCommand(`${shellQuote(ducPath())} check ${shellQuote(file)}`);
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
    vscode.commands.registerCommand("dudu.testProject", () => {
      runCommand(`${shellQuote(ducPath())} test`);
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
