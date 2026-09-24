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

  function section(title) {
    const s = el("section", "section");
    s.appendChild(el("h2", "section-title", title));
    return s;
  }

  // Removes the "No sections yet." line the first time a section is added.
  function mount(a_section) {
    const host = document.getElementById("sections");
    const placeholder = host.querySelector(".placeholder");
    if (placeholder) {
      placeholder.remove();
    }
    host.appendChild(a_section);
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

  return { el: el, section: section, mount: mount, row: row };
})();
