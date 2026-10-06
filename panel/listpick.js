// listpick.js
// Lifecast - choose from a list directly, instead of walking it one step at
// a time.
//
// A list row (hair, eyes, a tint's texture, a paint layer's texture...) moves
// through its entries with the slider and the < > buttons, and neither says
// what the next entry is. This adds a second way to the same value: a chip
// beside the row's name opens a pop-up under the row with every entry, and
// choosing one is exactly what pressing the step buttons does - the row sets
// the value, sends it as final and writes it to the undo history. The row stays
// as it was; the pop-up is one more way in, not a replacement.
//
// It opens inline under the row like the colour picker does (nothing floats
// over the stage), and it carries no text field - typed text is the one input
// the in-game browser still mangles. When the entries come from more than one
// plugin it offers a strip of plugin chips that narrows the list to one.
//
// Every list that reaches a pop-up is also checked against the DLL. The DLL
// stamps each list it sends with the FNV-1a signature and byte length of the
// list as text; verify() rebuilds that text from what the page received,
// signs it the same way and answers with its own stamp, and the DLL logs both
// ends. A page older than the DLL does not know the stamp and never answers,
// and the DLL logs that silence.
"use strict";

const LifecastList = (() => {
  // ----- the stamp -----
  function fnv1a(bytes) {
    let h = 0x811c9dc5;
    for (let i = 0; i < bytes.length; i += 1) {
      h ^= bytes[i];
      h = Math.imul(h, 0x01000193) >>> 0;
    }
    return ("00000000" + h.toString(16)).slice(-8);
  }

  // rows: an array of arrays of strings - the fields of each item, in the
  // order the DLL joined them with a tab; one line per item.
  function canonical(rows) {
    let text = "";
    for (const r of rows) {
      text += r.join("\t") + "\n";
    }
    return text;
  }

  function sigOf(text) {
    const bytes = new TextEncoder().encode(text);
    return { sig: fnv1a(bytes), len: bytes.length };
  }

  const answered = new Map(); // key -> the last stamp this page answered

  // Signs a list as received, compares it with the DLL's stamp and answers
  // the DLL once per distinct list. Returns true when both ends agree.
  function verify(key, rows, stamp) {
    const mine = sigOf(canonical(rows));
    const agree =
      !!stamp && stamp.sig === mine.sig && stamp.len === mine.len && (stamp.n === undefined || stamp.n === rows.length);
    const tag = mine.sig + "/" + String(mine.len);
    if (answered.get(key) !== tag) {
      answered.set(key, tag);
      if (!agree) {
        console.warn("[list] " + key + ": the DLL stamped " + JSON.stringify(stamp || null) + ", the page computed " + tag);
      }
      LifecastBridge.send("list-sig", { k: key, sig: mine.sig, len: mine.len, n: rows.length });
    }
    return agree;
  }

  // ----- the pop-up -----
  // a_opts: { row, label, none }
  //   row   - the LifecastControls row the pop-up chooses for
  //   none  - true when the row's lowest value is -1, "None": the pop-up then
  //           opens with a None entry, and entry k of the list is value k
  function create(a_opts) {
    const row = a_opts.row;
    const lowest = a_opts.none ? -1 : 0;

    const chip = LifecastControls.el("button", "list-chip", "list");
    chip.type = "button";
    chip.tabIndex = -1;
    chip.title = "Choose from the whole list";
    row.el.firstChild.appendChild(chip);

    const wrap = LifecastControls.el("div", "listpop");
    wrap.hidden = true;
    const origins = LifecastControls.el("div", "listpop-origins");
    const count = LifecastControls.el("div", "listpop-count");
    const body = LifecastControls.el("div", "listpop-items");
    wrap.appendChild(origins);
    wrap.appendChild(count);
    wrap.appendChild(body);
    row.el.appendChild(wrap);

    const state = { items: [], key: "", current: lowest - 1, origin: null, buttons: new Map(), originChips: new Map() };

    function sourcesOf(items) {
      const seen = new Set();
      for (const it of items) {
        if (it.source) {
          seen.add(it.source);
        }
      }
      return Array.from(seen).sort((a, b) => a.localeCompare(b));
    }

    function visible(it) {
      return state.origin === null || it.source === state.origin;
    }

    function markSelected() {
      for (const [value, b] of state.buttons) {
        b.classList.toggle("selected", value === state.current);
      }
    }

    function updateCount() {
      let shown = 0;
      for (const it of state.items) {
        if (visible(it)) {
          shown += 1;
        }
      }
      count.textContent =
        state.origin === null
          ? String(state.items.length) + " entries"
          : String(shown) + " of " + String(state.items.length) + " entries - " + state.origin;
    }

    function applyFilter() {
      for (const [value, b] of state.buttons) {
        const it = value < 0 ? null : state.items[value];
        b.hidden = it !== null && !visible(it);
      }
      for (const [source, c] of state.originChips) {
        c.classList.toggle("selected", source === state.origin);
      }
      state.allChip.classList.toggle("selected", state.origin === null);
      updateCount();
    }

    function pick(value) {
      row.choose(value);
      state.current = value;
      markSelected();
      wrap.hidden = true;
    }

    function addButton(value, label, title) {
      const b = LifecastControls.el("button", "listpop-item", label);
      b.type = "button";
      b.tabIndex = -1;
      b.title = title || label;
      b.dataset.index = String(value);
      b.addEventListener("click", () => pick(value));
      body.appendChild(b);
      state.buttons.set(value, b);
    }

    // items: [{ name, source?, title? }], current: the row's value. Rebuilds
    // the DOM only when the list is not the one already drawn.
    function setItems(a_items, a_current) {
      const key = a_items.map((it) => it.name + "\t" + (it.source || "")).join("\n");
      if (key !== state.key) {
        state.key = key;
        state.items = a_items;
        state.origin = null;
        state.buttons = new Map();
        state.originChips = new Map();
        body.replaceChildren();
        origins.replaceChildren();
        if (a_opts.none) {
          addButton(-1, "None");
        }
        a_items.forEach((it, i) => addButton(i, it.name === "" ? "Entry " + String(i + 1) : it.name, it.title || it.source || it.name));
        const sources = sourcesOf(a_items);
        origins.hidden = sources.length < 2;
        state.allChip = LifecastControls.el("button", "listpop-origin", "All");
        state.allChip.type = "button";
        state.allChip.tabIndex = -1;
        state.allChip.addEventListener("click", () => {
          state.origin = null;
          applyFilter();
        });
        origins.appendChild(state.allChip);
        for (const source of sources) {
          const c = LifecastControls.el("button", "listpop-origin", source);
          c.type = "button";
          c.tabIndex = -1;
          c.title = source;
          c.addEventListener("click", () => {
            state.origin = source;
            applyFilter();
          });
          origins.appendChild(c);
          state.originChips.set(source, c);
        }
        applyFilter();
      }
      setCurrent(a_current);
      chip.hidden = a_items.length === 0;
    }

    function setCurrent(a_value) {
      state.current = typeof a_value === "number" ? a_value : lowest - 1;
      markSelected();
    }

    chip.addEventListener("click", () => {
      wrap.hidden = !wrap.hidden;
      if (!wrap.hidden) {
        const at = state.buttons.get(state.current);
        if (at) {
          body.scrollTop = Math.max(0, at.offsetTop - body.offsetTop - body.clientHeight / 2);
        }
        wrap.scrollIntoView({ block: "nearest" });
      }
    });

    return {
      el: wrap,
      chip: chip,
      setItems: setItems,
      setCurrent: setCurrent,
      close: () => {
        wrap.hidden = true;
      },
      // For the measurement: what the pop-up offers right now, in the order
      // of the list, with what the filter hides marked.
      entries: () =>
        Array.from(state.buttons.entries()).map(([value, b]) => ({ value: value, name: b.textContent, hidden: b.hidden })),
      origin: () => state.origin,
    };
  }

  return { create: create, verify: verify, sigOf: sigOf, canonical: canonical };
})();
