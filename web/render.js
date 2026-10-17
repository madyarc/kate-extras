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

  // Returns { text, items, lineMap } where each math chunk in `text` is
  // replaced by a placeholder KTMATH<i>END and items[i] = { tex, display }.
  // Placeholders swallow the newlines of multi-line math, so lineMap[k] is
  // the source line of line k of `text`.
  function protect(src) {
    const out = [];
    const items = [];
    const lineMap = [0];
    const n = src.length;
    let i = 0;
    let lineStart = true;
    let fence = null;

    let scanned = 0, srcLine = 0; // incremental line count of src[0..pos)
    const lineOf = (pos) => {
      for (; scanned < pos; scanned++) if (src[scanned] === "\n") srcLine++;
      return srcLine;
    };
    // record lineMap entries for newlines copied verbatim from src[pos..]
    const copied = (s, pos) => {
      for (let k = s.indexOf("\n"); k >= 0; k = s.indexOf("\n", k + 1)) lineMap.push(lineOf(pos + k) + 1);
    };

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

      if (c === "\n") { out.push(c); copied(c, i); i++; lineStart = true; continue; }

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
        if (found >= 0) {
          const span = src.slice(i, found + r);
          out.push(span);
          copied(span, i);
          i = found + r;
        }
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
    return { text: out.join(""), items: items, lineMap: lineMap };
  }

  // ---- state ----
  let baseMacros = {};
  let md = null;

  // Tag block tokens with data-line="<source line>" for scroll sync.
  function sourceLines(state) {
    const map = state.env && state.env.lineMap;
    if (!map) return;
    state.tokens.forEach((t) => {
      if (t.map && t.nesting >= 0 && t.type !== "inline")
        t.attrSet("data-line", String(map[t.map[0]] ?? t.map[0]));
    });
  }

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

  // Markdown + math -> HTML (pure function of its inputs; testable in node).
  // mdRender(text, lineMap) may tag blocks with data-line attributes.
  function toHtml(src, mdRender, mathRender) {
    const p = protect(src);
    const rendered = p.items.map(mathRender);
    let html = mdRender(p.text, p.lineMap);
    html = html.replace(/<p( [^>]*)?>KTMATH(\d+)END<\/p>/g, (m, attrs, k) => {
      if (!p.items[+k].display) return "<p" + (attrs || "") + ">" + rendered[+k] + "</p>";
      return attrs ? "<div" + attrs + ">" + rendered[+k] + "</div>" : rendered[+k];
    });
    html = html.replace(/KTMATH(\d+)END/g, (m, k) => rendered[+k]);
    return html;
  }

  function render(src, baseHref, cursorLine) {
    if (!md) {
      md = root.markdownit({ html: true, linkify: false, typographer: false });
      md.core.ruler.push("texmd_source_lines", sourceLines);
    }
    if (baseHref) {
      const b = document.querySelector("base");
      if (b.getAttribute("href") !== baseHref) b.setAttribute("href", baseHref);
    }
    const macros = Object.assign({}, baseMacros); // fresh per render
    document.getElementById("content").innerHTML =
      toHtml(src, (t, lineMap) => md.render(t, { lineMap: lineMap }), (item) => renderMath(item, macros));
    if (cursorLine !== undefined) scrollToLine(cursorLine);
  }

  // Scroll so that source line `line` (0-based) sits a third of the way down
  // the view, interpolating between the nearest tagged blocks around it.
  function scrollToLine(line) {
    if (line <= 0) { window.scrollTo(0, 0); return; }
    let before = null, after = null;
    document.querySelectorAll("#content [data-line]").forEach((el) => {
      const l = +el.getAttribute("data-line");
      if (l <= line) {
        if (!before || l >= before.line) before = { line: l, el: el };
      } else if (!after || l < after.line) {
        after = { line: l, el: el };
      }
    });
    const top = (el) => el.getBoundingClientRect().top + window.scrollY;
    let y = 0;
    if (before) {
      y = top(before.el);
      const bottom = after ? top(after.el) : y + before.el.offsetHeight;
      const span = after ? after.line - before.line : 1;
      y += Math.min(1, (line - before.line) / span) * Math.max(0, bottom - y);
    } else if (after) {
      y = top(after.el) * line / after.line;
    }
    window.scrollTo(0, Math.max(0, y - window.innerHeight / 3));
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

  const api = { render, setMacros, setTheme, scrollToLine, protect, toHtml };
  root.texmd = api;
  if (typeof module !== "undefined") module.exports = api;
})(typeof window !== "undefined" ? window : globalThis);
