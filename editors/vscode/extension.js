const vscode = require("vscode");
const fs = require("fs");
const path = require("path");
const { LanguageClient, State } = require("vscode-languageclient/node");

let terminal;
let client;
let statusItem;
let stateSubscription;
let diagnosticsSubscription;

function shellQuote(value) {
  return `'${value.replace(/'/g, "'\\''")}'`;
}

function workspaceDirectory() {
  const folder = vscode.workspace.workspaceFolders?.[0];
  return folder ? folder.uri.fsPath : process.cwd();
}

function duduPath() {
  return vscode.workspace.getConfiguration("dudu").get("path", "dudu");
}

function duduLspPath() {
  return vscode.workspace.getConfiguration("dudu").get("lspPath", "dudu-lsp");
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

function lspStateText() {
  if (!client) {
    return "LSP stopped";
  }
  switch (client.state) {
    case State.Running:
      return "LSP";
    case State.Starting:
      return "LSP starting";
    case State.Stopped:
      return "LSP stopped";
    default:
      return "LSP";
  }
}

function hasNativeHeaderProblem() {
  return vscode.languages
    .getDiagnostics()
    .some(([, items]) => items.some((item) => item.source === "dudu/native-header"));
}

function updateStatus() {
  if (!statusItem) {
    return;
  }
  const nativeState = hasNativeHeaderProblem() ? "native headers failing" : "native headers ok";
  statusItem.text = `$(symbol-method) Dudu: ${lspStateText()}`;
  statusItem.tooltip = `dudu: ${duduPath()}\nlsp: ${duduLspPath()}\ntarget: ${targetStatus()}\n${nativeState}`;
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

function clientOptions() {
  return {
    documentSelector: [{ scheme: "file", language: "dudu" }],
    synchronize: {
      fileEvents: vscode.workspace.createFileSystemWatcher("**/*.dd"),
    },
  };
}

function serverOptions() {
  return {
    command: duduLspPath(),
    args: [],
    options: {
      cwd: workspaceDirectory(),
    },
  };
}

function createClient(context) {
  const next = new LanguageClient("dudu", "Dudu Language Server", serverOptions(), clientOptions());
  context.subscriptions.push(next);
  stateSubscription?.dispose();
  stateSubscription = next.onDidChangeState(() => updateStatus());
  context.subscriptions.push(stateSubscription);
  return next;
}

async function startClient(context) {
  if (client?.state === State.Running || client?.state === State.Starting) {
    return;
  }
  client = createClient(context);
  updateStatus();
  try {
    await client.start();
  } catch (error) {
    vscode.window.showErrorMessage(`Could not start dudu-lsp: ${error.message}`);
  } finally {
    updateStatus();
  }
}

async function stopClient() {
  if (!client) {
    return;
  }
  const oldClient = client;
  client = undefined;
  stateSubscription?.dispose();
  stateSubscription = undefined;
  try {
    await oldClient.stop();
  } catch {
  } finally {
    updateStatus();
  }
}

async function restartClient(context) {
  await stopClient();
  await startClient(context);
}

function activate(context) {
  statusItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 90);
  context.subscriptions.push(statusItem);
  diagnosticsSubscription = vscode.languages.onDidChangeDiagnostics(() => updateStatus());
  context.subscriptions.push(diagnosticsSubscription);

  context.subscriptions.push(
    vscode.commands.registerCommand("dudu.fmtFile", async () => {
      const editor = vscode.window.activeTextEditor;
      if (editor?.document.languageId === "dudu" || editor?.document.fileName.endsWith(".dd")) {
        await vscode.commands.executeCommand("editor.action.formatDocument");
      }
    }),
    vscode.commands.registerCommand("dudu.checkFile", () => {
      const file = activeDuduFile();
      if (file) {
        runCommand(`${shellQuote(duduPath())} check ${shellQuote(file)}`);
      }
    }),
    vscode.commands.registerCommand("dudu.buildProject", () => {
      runCommand(`${shellQuote(duduPath())} build`);
    }),
    vscode.commands.registerCommand("dudu.runFile", () => {
      const file = activeDuduFile();
      if (file) {
        runCommand(`${shellQuote(duduPath())} run ${shellQuote(file)}`);
      }
    }),
    vscode.commands.registerCommand("dudu.testProject", () => {
      runCommand(`${shellQuote(duduPath())} test`);
    }),
    vscode.commands.registerCommand("dudu.restartLsp", () => restartClient(context)),
  );

  startClient(context);
}

async function deactivate() {
  await stopClient();
  statusItem?.dispose();
}

module.exports = { activate, deactivate };
