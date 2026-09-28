// controls.js
// Atelier - the slider row every section builds on.
//
// A row is a label, the current value, a range input and a pair of step
// buttons. The row does not know what it edits: it calls a_opts.send(value,
// final) and the section turns that into a message.
//
// While the range is dragged the row sends at most one value per
// SEND_GAP_MS, always the latest; when the drag ends it sends the final
// value marked as such, which is what makes the DLL redraw at once. A step
// button sends one final value.
"use strict";

const AtelierControls = (() => {
  const SEND_GAP_MS = 50;

  function el(tag, cls, text) {
    const e = document.createElement(tag);
    if (cls) {
      e.className = cls;
    }
    if (text !== undefined) {
      e.textContent = text;
    }
    return e;
  }

  // A section is a header that collapses it and a body that holds the rows.
  // The caller appends into section.body, never into the section itself:
  // hidden, remove() and closest() stay meaningful on the whole section.
  //
  // Collapsed is the default (D064). a_opts.open opts one out, for a section
  // whose content is the panel's primary action rather than a list to browse.
  function section(title, a_opts) {
    const opts = a_opts || {};
    const s = el("section", "section");
    const head = el("button", "section-head");
    head.type = "button";
    head.tabIndex = -1;
    head.appendChild(el("span", "section-caret"));
    head.appendChild(el("h2", "section-title", title));
    const body = el("div", "section-body");
    s.appendChild(head);
    s.appendChild(body);
    s.body = body;
    s.dataset.open = opts.open ? "1" : "0";
    head.setAttribute("aria-expanded", opts.open ? "true" : "false");
    head.addEventListener("click", () => {
      const open = s.dataset.open !== "1";
      s.dataset.open = open ? "1" : "0";
      head.setAttribute("aria-expanded", open ? "true" : "false");
    });
    return s;
  }

  // The order the sections appear in. Until something sets it, it is the order
  // they mount in - which is the order of the <script> tags, and that is the
  // accident this exists to end. A key the order does not name falls in after
  // every key it does name, by MOUNTING order.
  //
  // The tie-break is the mounting index and not the current position on
  // purpose: sorting by where things sit now makes an unnamed section drift a
  // little further every time the order is set again, so the same list applied
  // twice would not give the same page.
  let order = [];
  let mounted = 0;

  function rank(a_el) {
    const at = order.indexOf(a_el.dataset.key);
    return at < 0 ? order.length : at;
  }

  function arrange() {
    const host = document.getElementById("sections");
    Array.prototype.slice.call(host.children)
      .sort((a, b) => rank(a) - rank(b) || Number(a.dataset.mountedAt) - Number(b.dataset.mountedAt))
      .forEach((el) => host.appendChild(el));
  }

  // Replaces the order and rearranges what is already mounted. The list comes
  // from outside this file on purpose: no section order is written here.
  function setOrder(a_keys) {
    order = Array.isArray(a_keys) ? a_keys.slice() : [];
    arrange();
  }

  // Removes the "No sections yet." line the first time a section is added.
  function mount(a_section, a_key) {
    const host = document.getElementById("sections");
    const placeholder = host.querySelector(".placeholder");
    if (placeholder) {
      placeholder.remove();
    }
    a_section.dataset.key = a_key === undefined ? "" : a_key;
    a_section.dataset.mountedAt = String(mounted);
    mounted += 1;
    host.appendChild(a_section);
    arrange();
  }

  // a_opts: { label, min, max, step, button, format(v), send(v, final) }
  // button is how far one press of < or > moves the value.
  function row(a_opts) {
    const wrap = el("div", "slider");
    const name = el("span", "slider-name", a_opts.label);
    const value = el("span", "slider-value", "--");
    const input = el("input");
    input.type = "range";
    input.min = String(a_opts.min);
    input.max = String(a_opts.max);
    input.step = String(a_opts.step);
    input.value = String(a_opts.min);
    input.disabled = true;

    const down = el("button", "step-button", "<");
    const up = el("button", "step-button", ">");
    down.type = "button";
    up.type = "button";
    down.tabIndex = -1;
    up.tabIndex = -1;

    const track = el("div", "slider-track");
    track.appendChild(down);
    track.appendChild(input);
    track.appendChild(up);

    wrap.appendChild(name);
    wrap.appendChild(value);
    wrap.appendChild(track);

    const state = { dragging: false, timer: null, lastSent: 0 };

    function show(v) {
      value.textContent = a_opts.format(v);
    }

    function send(final) {
      if (state.timer !== null) {
        clearTimeout(state.timer);
        state.timer = null;
      }
      state.lastSent = performance.now();
      a_opts.send(Number(input.value), final);
    }

    input.addEventListener("input", () => {
      state.dragging = true;
      show(Number(input.value));
      const wait = SEND_GAP_MS - (performance.now() - state.lastSent);
      if (wait <= 0) {
        send(false);
      } else if (state.timer === null) {
        state.timer = setTimeout(() => send(false), wait);
      }
    });

    input.addEventListener("change", () => {
      state.dragging = false;
      send(true);
    });

    function stepBy(a_dir) {
      if (input.disabled) {
        return;
      }
      const min = Number(input.min);
      const max = Number(input.max);
      // Rounded to the input's step, so repeated presses do not drift.
      const step = Number(input.step);
      let v = Number(input.value) + a_dir * a_opts.button;
      v = Math.round(v / step) * step;
      v = Math.min(max, Math.max(min, v));
      input.value = String(v);
      show(Number(input.value));
      send(true);
    }

    down.addEventListener("click", () => stepBy(-1));
    up.addEventListener("click", () => stepBy(1));

    return {
      el: wrap,
      input: input,
      // The player's hand wins over a value from before it moved.
      dragging: () => state.dragging,
      // null disables the row and shows "--".
      set(a_v) {
        const off = a_v === null || typeof a_v !== "number" || !isFinite(a_v);
        input.disabled = off;
        down.disabled = off;
        up.disabled = off;
        if (off) {
          value.textContent = "--";
          return;
        }
        input.value = String(a_v);
        show(a_v);
      },
      setRange(a_min, a_max) {
        input.min = String(a_min);
        input.max = String(a_max);
      },
      // Replaces the value text with arbitrary content (a colour chip).
      valueEl: value,
    };
  }

  // Opens the section named a_key if it is collapsed, and scrolls it into
  // view. Returns false when the key names no mounted section - the caller
  // (a shortcut button, e.g.) decides what that means; this function does
  // not, so it stays correct whether the section is conditional or not.
  function reveal(a_key) {
    const host = document.getElementById("sections");
    const s = host.querySelector('.section[data-key="' + a_key + '"]');
    if (!s) {
      return false;
    }
    if (s.dataset.open !== "1") {
      s.dataset.open = "1";
      const head = s.querySelector(".section-head");
      if (head) {
        head.setAttribute("aria-expanded", "true");
      }
    }
    s.scrollIntoView({ block: "start", behavior: "smooth" });
    return true;
  }

  return { el: el, section: section, mount: mount, setOrder: setOrder, row: row, reveal: reveal };
})();
