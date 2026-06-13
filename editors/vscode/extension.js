const vscode = require("vscode");
const childProcess = require("child_process");
const os = require("os");
const path = require("path");

let terminal;
let diagnostics;

function shellQuote(value) {
  return `'${value.replace(/'/g, "'\\''")}'`;
}

function workspaceDirectory() {
  const folder = vscode.workspace.workspaceFolders?.[0];
  return folder ? folder.uri.fsPath : process.cwd();
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

function activeDuduDocument() {
  const editor = vscode.window.activeTextEditor;
  if (!editor) {
    vscode.window.showErrorMessage("Open a .dd file first.");
    return undefined;
  }
  if (editor.document.languageId !== "dudu" && !editor.document.fileName.endsWith(".dd")) {
    vscode.window.showErrorMessage("The active file is not a Dudu .dd file.");
    return undefined;
  }
  return editor.document;
}

function parseDiagnostic(line) {
  const match = /^dudu: (.*):([0-9]+):([0-9]+): (.*)$/.exec(line.trim());
  if (!match) {
    return undefined;
  }
  const file = match[1];
  const row = Math.max(Number(match[2]) - 1, 0);
  const col = Math.max(Number(match[3]) - 1, 0);
  return {
    file,
    diagnostic: new vscode.Diagnostic(
      new vscode.Range(row, col, row, col + 1),
      match[4],
      vscode.DiagnosticSeverity.Error,
    ),
  };
}

function checkDocument(document) {
  const output = path.join(os.tmpdir(), `dudu-vscode-${process.pid}.cpp`);
  childProcess.execFile(
    "duc",
    ["emit", document.fileName, "-o", output],
    { cwd: workspaceDirectory() },
    (error, _stdout, stderr) => {
      const byFile = new Map();
      for (const line of stderr.split(/\r?\n/)) {
        const parsed = parseDiagnostic(line);
        if (!parsed) {
          continue;
        }
        const uri = vscode.Uri.file(path.resolve(workspaceDirectory(), parsed.file));
        const key = uri.toString();
        const list = byFile.get(key) ?? [];
        list.push(parsed.diagnostic);
        byFile.set(key, list);
      }
      diagnostics.clear();
      for (const [key, list] of byFile) {
        diagnostics.set(vscode.Uri.parse(key), list);
      }
      if (error && byFile.size === 0) {
        vscode.window.showErrorMessage(stderr.trim() || String(error));
      }
    },
  );
}

function completion(label, kind) {
  return new vscode.CompletionItem(label, kind);
}

function collectDocumentCompletions(document) {
  const items = [];
  const text = document.getText();
  const keywordKind = vscode.CompletionItemKind.Keyword;
  for (const keyword of ["class", "def", "enum", "import", "return", "for", "if", "else"]) {
    items.push(completion(keyword, keywordKind));
  }
  for (const type of ["bool", "i32", "i64", "u32", "u64", "f32", "f64", "str", "cstr"]) {
    items.push(completion(type, vscode.CompletionItemKind.TypeParameter));
  }
  for (const line of text.split(/\r?\n/)) {
    let match = /^(?:public |private )?class\s+([A-Z][A-Za-z0-9]*)\s*:/.exec(line);
    if (match) {
      items.push(completion(match[1], vscode.CompletionItemKind.Class));
      continue;
    }
    match = /^enum\s+([A-Z][A-Za-z0-9]*)/.exec(line);
    if (match) {
      items.push(completion(match[1], vscode.CompletionItemKind.Enum));
      continue;
    }
    match = /^(?:public |private )?def\s+([a-z][a-z0-9_]*)\s*\(/.exec(line);
    if (match) {
      items.push(completion(match[1], vscode.CompletionItemKind.Function));
      continue;
    }
    match = /^import\s+(?:c|cpp)\s+"[^"]+"\s+as\s+([a-z][a-z0-9_]*)/.exec(line);
    if (match) {
      items.push(completion(match[1], vscode.CompletionItemKind.Module));
      continue;
    }
    match = /^([A-Z][A-Z0-9_]*)\s*:/.exec(line);
    if (match) {
      items.push(completion(match[1], vscode.CompletionItemKind.Constant));
    }
  }
  return items;
}

function activate(context) {
  diagnostics = vscode.languages.createDiagnosticCollection("dudu");
  context.subscriptions.push(
    diagnostics,
    vscode.languages.registerCompletionItemProvider("dudu", {
      provideCompletionItems(document) {
        return collectDocumentCompletions(document);
      },
    }),
    vscode.commands.registerCommand("dudu.fmtFile", () => {
      const file = activeDuduFile();
      if (file) {
        runCommand(`duc fmt ${shellQuote(file)}`);
      }
    }),
    vscode.commands.registerCommand("dudu.checkFile", () => {
      const document = activeDuduDocument();
      if (document) {
        checkDocument(document);
      }
    }),
    vscode.commands.registerCommand("dudu.buildProject", () => {
      runCommand("duc build");
    }),
    vscode.commands.registerCommand("dudu.runFile", () => {
      const file = activeDuduFile();
      if (file) {
        runCommand(`duc run ${shellQuote(file)}`);
      }
    }),
    vscode.workspace.onDidSaveTextDocument((document) => {
      if (document.languageId === "dudu" || document.fileName.endsWith(".dd")) {
        checkDocument(document);
      }
    }),
  );
}

function deactivate() {
  diagnostics?.dispose();
}

module.exports = { activate, deactivate };
