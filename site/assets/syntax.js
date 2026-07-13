(function () {
  "use strict";

  if (typeof Prism === "undefined") {
    return;
  }

  Prism.languages.toml = {
    comment: /#.*/,
    table: {
      pattern: /(^[ \t]*)\[\[?[^\]\r\n]+\]\]?/m,
      lookbehind: true,
      alias: "class-name",
    },
    key: {
      pattern: /(^[ \t]*)[A-Za-z0-9_.-]+(?=[ \t]*=)/m,
      lookbehind: true,
      alias: "property",
    },
    string: {
      pattern: /"""[\s\S]*?"""|'''[\s\S]*?'''|"(?:\\.|[^"\\\r\n])*"|'[^'\r\n]*'/,
      greedy: true,
    },
    boolean: /\b(?:false|true)\b/,
    number: /[+-]?\b(?:0x[\dA-Fa-f](?:_?[\dA-Fa-f])*|0o[0-7](?:_?[0-7])*|0b[01](?:_?[01])*|\d(?:_?\d)*(?:\.\d(?:_?\d)*)?(?:[eE][+-]?\d(?:_?\d)*)?|inf|nan)\b/i,
    punctuation: /[\[\]{},.=]/,
  };

  Prism.languages.dudu = {
    "error-comment": {
      pattern: /(^[ \t]*)# error:[^\r\n]*(?:\r?\n[ \t]*#[^\r\n]*)*/m,
      lookbehind: true,
      greedy: true,
      alias: "comment",
    },
    comment: {
      pattern: /(^|[^\\])#.*/,
      lookbehind: true,
      greedy: true,
    },
    "triple-quoted-string": {
      pattern: /(?:[rubf]{0,2})(?:"""(?:\\[\s\S]|(?!""")[^\\])*"""|'''(?:\\[\s\S]|(?!''')[^\\])*''')/i,
      greedy: true,
      alias: "string",
    },
    string: {
      pattern: /(?:[rubf]{0,2})(?:"(?:\\.|[^\\\r\n"])*"|'(?:\\.|[^\\\r\n'])*')/i,
      greedy: true,
    },
    decorator: {
      pattern: /(^[ \t]*)@[A-Za-z_]\w*(?:\.[A-Za-z_]\w*)*/m,
      lookbehind: true,
      alias: "annotation",
    },
    "function-definition": {
      pattern: /(\bdef[ \t]+)[A-Za-z_]\w*/,
      lookbehind: true,
      alias: "function",
    },
    "class-definition": {
      pattern: /(\b(?:class|enum|type)[ \t]+)[A-Za-z_]\w*/,
      lookbehind: true,
      alias: "class-name",
    },
    function: /\b[A-Za-z_]\w*(?=\s*(?:\[[^\]\r\n]*\])?\()/,
    "named-binding": {
      pattern: /\b[a-z_]\w*(?=\s*:)/,
      alias: "parameter",
    },
    constant: /\b[A-Z][A-Z0-9_]*\b/,
    builtin: /\b(?:array|array_view|bool|byte|dict|dyn|ellipsis|f16|f32|f64|fn|i8|i16|i32|i64|isize|list|new_axis|Option|Result|set|slice|str|u8|u16|u32|u64|usize|void)\b/,
    boolean: /\b(?:False|None|True)\b/,
    keyword: /\b(?:and|as|assert|break|case|catch|class|continue|debug_assert|def|delete|elif|else|enum|except|finally|for|from|if|import|in|is|match|new|not|or|pass|raise|return|static|super|throw|try|type|while)\b/,
    "class-name": /\b[A-Z][A-Za-z0-9_]*(?:\.[A-Z][A-Za-z0-9_]*)*\b/,
    property: {
      pattern: /(\.)[A-Za-z_]\w*/,
      lookbehind: true,
    },
    number: /\b(?:0[xX][\dA-Fa-f](?:_?[\dA-Fa-f])*|0[bB][01](?:_?[01])*|\d(?:_?\d)*\.\d*(?:[eE][+-]?\d(?:_?\d)*)?|\d(?:_?\d)*(?:[eE][+-]?\d(?:_?\d)*)?|\.\d+(?:[eE][+-]?\d(?:_?\d)*)?)\b/,
    operator: /\.\.\.|->|\*\*|\/\/|<<|>>|:=|==|!=|<=|>=|\+=|-=|\*=|\/=|%=|&=|\|=|\^=|[-+%=]=?|[!<>]=?|[&|^~]/,
    punctuation: /[{}\[\];(),.:]/,
  };

  function codeLanguage(code) {
    if (code.dataset.language) {
      return code.dataset.language;
    }

    const window = code.closest(".code-window");
    if (window && window.dataset.language) {
      return window.dataset.language;
    }

    const label = window ? window.querySelector(".code-label") : null;
    const text = label ? label.textContent.trim().toLowerCase() : "";
    if (text.includes("python")) {
      return "python";
    }
    if (text === "rust") {
      return "rust";
    }
    if (text === "c++" || text === "c++ inferred bindings" || text === "c++ operator") {
      return "cpp";
    }
    if (text === "c") {
      return "c";
    }
    if (text.includes("glsl")) {
      return "glsl";
    }
    if (text.includes("shell") || text.includes("terminal") || text.includes("command")) {
      return "bash";
    }
    return document.body.dataset.codeLanguage || "dudu";
  }

  document.querySelectorAll("pre > code").forEach(function (code) {
    const language = codeLanguage(code);
    code.className = code.className
      .split(/\s+/)
      .filter(function (name) { return name && !name.startsWith("language-"); })
      .concat("language-" + language)
      .join(" ");
    Prism.highlightElement(code);
  });
}());
