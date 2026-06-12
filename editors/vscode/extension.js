const vscode = require("vscode");

let terminal;

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

function activate(context) {
  context.subscriptions.push(
    vscode.commands.registerCommand("dudu.fmtFile", () => {
      const file = activeDuduFile();
      if (file) {
        runCommand(`duc fmt ${shellQuote(file)}`);
      }
    }),
    vscode.commands.registerCommand("dudu.checkFile", () => {
      const file = activeDuduFile();
      if (file) {
        runCommand(`duc check ${shellQuote(file)}`);
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
  );
}

function deactivate() {}

module.exports = { activate, deactivate };
