// SPDX-FileCopyrightText: 2026 Madhesh
// SPDX-License-Identifier: GPL-3.0-or-later

// texmdpreview renderer: Markdown via markdown-it, math via KaTeX.
// Math is cut out of the source *before* markdown parsing, so markdown
// never mangles _ * \ inside formulas.
(function (root) {
  "use strict";

  const ENVS = "equation|align|gather|alignat|multline|flalign|eqnarray|split|cases";
  const ENV_RE = new RegExp("^[ \\t]*\\\\begin\\{((?:" + ENVS + ")\\*?)\\}");
  const FENCE_OPEN = /^ {0,3}(`{3,}|~{3,})/;

  // Returns { text, items } where each math chunk in `text` is replaced by
  // a placeholder KTMATH<i>END and items[i] = { tex, display }.
  function protect(src) {
    const out = [];
    const items = [];
    const n = src.length;
    let i = 0;
    let lineStart = true;
    let fence = null;

    const add = (tex, display) => {
      out.push("KTMATH" + items.length + "END");
      items.push({ tex: tex, display: display });
    };

    while (i < n) {
      if (lineStart) {
        lineStart = false;
        let le = src.indexOf("\n", i);
        if (le < 0) le = n;
        const line = src.slice(i, le);

        if (fence) { // inside ``` block: copy verbatim
          const t = line.trim();
          if (t.length >= fence.length && t[0] === fence[0] && /^(`+|~+)$/.test(t)) fence = null;
          out.push(line);
          i = le;
          continue;
        }
        const f = line.match(FENCE_OPEN);
        if (f) { fence = f[1]; out.push(line); i = le; continue; }

        const env = line.match(ENV_RE);
        if (env) {
          const endTok = "\\end{" + env[1] + "}";
          const j = src.indexOf(endTok, i);
          if (j >= 0) {
            add(src.slice(i, j + endTok.length).trim(), true);
            i = j + endTok.length;
            continue;
          }
        }
      }

      const c = src[i];

      if (c === "\n") { out.push(c); i++; lineStart = true; continue; }

      if (c === "\\") {
        const d = src[i + 1];
        if (d === undefined || d === "\n") { out.push(c); i++; continue; }
        if (d === "[" || d === "(") {
          const close = d === "[" ? "\\]" : "\\)";
          const j = src.indexOf(close, i + 2);
          if (j >= 0) { add(src.slice(i + 2, j), d === "["); i = j + 2; continue; }
        }
        out.push(src.slice(i, i + 2)); // keep escapes like \$ for markdown
        i += 2;
        continue;
      }

      if (c === "`") { // inline code span: copy verbatim
        let r = 0;
        while (src[i + r] === "`") r++;
        const tick = "`".repeat(r);
        let j = i + r;
        let found = -1;
        while ((j = src.indexOf(tick, j)) >= 0) {
          if (src[j + r] !== "`" && src[j - 1] !== "`") { found = j; break; }
          j += r;
        }
        if (found >= 0) { out.push(src.slice(i, found + r)); i = found + r; }
        else { out.push(tick); i += r; }
        continue;
      }

      if (c === "$") {
        if (src[i + 1] === "$") { // display $$...$$
          const j = src.indexOf("$$", i + 2);
          if (j >= 0) { add(src.slice(i + 2, j), true); i = j + 2; continue; }
          out.push("$$"); i += 2; continue;
        }
        // inline $...$ (pandoc rules: no space after opening, none before
        // closing, closing not followed by a digit, no blank line inside)
        const next = src[i + 1];
        if (next !== undefined && !/\s/.test(next)) {
          let k = i + 1;
          let end = -1;
          while (k < n) {
            const ch = src[k];
            if (ch === "\\") { k += 2; continue; }
            if (ch === "\n" && src[k + 1] === "\n") break;
            if (ch === "$") {
              if (!/\s/.test(src[k - 1]) && !/[0-9]/.test(src[k + 1] || "")) end = k;
              break;
            }
            k++;
          }
          if (end > 0) { add(src.slice(i + 1, end), false); i = end + 1; continue; }
        }
        out.push("$"); i++; continue;
      }

      out.push(c);
      i++;
    }
    return { text: out.join(""), items: items };
  }

  // ---- state ----
  let baseMacros = {};
  let md = null;

  function escapeHtml(s) {
    return s.replace(/[&<>"]/g, (ch) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[ch]));
  }

  function renderMath(item, macros) {
    try {
      return root.katex.renderToString(item.tex, {
        displayMode: item.display,
        throwOnError: false,
        globalGroup: true, // \newcommand persists to later formulas
        macros: macros,
        strict: "ignore",
        trust: false,
      });
    } catch (e) {
      return '<span class="texmd-err">' + escapeHtml(String(e.message || e)) + "</span>";
    }
  }

  // Markdown + math -> HTML (pure function of its inputs; testable in node)
  function toHtml(src, mdRender, mathRender) {
    const p = protect(src);
    const rendered = p.items.map(mathRender);
    let html = mdRender(p.text);
    html = html.replace(/<p>KTMATH(\d+)END<\/p>/g, (m, k) =>
      p.items[+k].display ? rendered[+k] : "<p>" + rendered[+k] + "</p>");
    html = html.replace(/KTMATH(\d+)END/g, (m, k) => rendered[+k]);
    return html;
  }

  function render(src, baseHref) {
    if (!md) md = root.markdownit({ html: true, linkify: false, typographer: false });
    if (baseHref) {
      const b = document.querySelector("base");
      if (b.getAttribute("href") !== baseHref) b.setAttribute("href", baseHref);
    }
    const macros = Object.assign({}, baseMacros); // fresh per render
    document.getElementById("content").innerHTML =
      toHtml(src, (t) => md.render(t), (item) => renderMath(item, macros));
  }

  // One definition per line; \DeclareMathOperator is mapped to \operatorname.
  function setMacros(text) {
    baseMacros = {};
    text.split("\n").forEach((raw) => {
      let line = raw.replace(/(^|[^\\])%.*$/, "$1").trim();
      if (!line) return;
      line = line.replace(/\\DeclareMathOperator(\*?)\s*\{?\s*(\\[A-Za-z]+)\s*\}?\s*\{(.*)\}\s*$/,
        (m, star, name, body) => "\\newcommand{" + name + "}{\\operatorname" + star + "{" + body + "}}");
      try {
        root.katex.renderToString(line, { globalGroup: true, macros: baseMacros, throwOnError: true });
      } catch (e) { /* skip lines KaTeX can't take, e.g. \usepackage */ }
    });
  }

  function setTheme(bg, fg, link, border, codeBg) {
    const s = document.documentElement.style;
    s.setProperty("--bg", bg);
    s.setProperty("--fg", fg);
    s.setProperty("--link", link);
    s.setProperty("--border", border);
    s.setProperty("--code-bg", codeBg);
  }

  const api = { render, setMacros, setTheme, protect, toHtml };
  root.texmd = api;
  if (typeof module !== "undefined") module.exports = api;
})(typeof window !== "undefined" ? window : globalThis);
