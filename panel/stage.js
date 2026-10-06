// stage.js
// Lifecast - the character view: a frame to the right of the panel, and the
// controls that move the character inside it.
//
// The page draws nothing of the character. It is transparent here, and the
// game draws the RaceSex Menu's character behind it; the DLL moves the camera
// and turns the body so that the character sits in the frame. This file only
// draws the border and the controls, and sends the moves.
//
// Moves are DELTAS; the DLL keeps where the view is, clamps it to its limits
// and answers with the state ("stage"), which is what the readout and the
// disabled buttons show. A drag sends at most one message per SEND_MS, with
// everything moved since the last one.
//
// In the frame, with the mouse:
//   drag                  turn the character
//   drag, right button    move the camera, up/down and left/right
//   drag, Ctrl held       tilt the camera (when the DLL can)
//   wheel                 closer and farther
// The buttons under the frame repeat the turn, the distance, the height and
// the tilt, so none of THOSE depends on knowing a mouse gesture.
//
// SIDEWAYS IS THE EXCEPTION, and it is a gap rather than a decision: the pan
// axis has no button, so moving the camera left and right is reachable only by
// the right-button drag. This comment used to claim every move had a button,
// and it was true when it was written - the pan axis did not exist yet. It
// arrived later, on the drag, without a button, and the sentence above it
// stayed. A reader who trusted it would conclude the keyboard and the buttons
// cover the whole view, and they do not.
//
// When the DLL says the view is not ready, the frame shows why and the
// controls go away; nothing else in the panel changes.
//
// A development build of the DLL also sends "tuning", and then a section of
// number boxes appears under the readout: the seven numbers of the view the
// author retunes at OLHO DO AUTOR. A release DLL never sends that key, so the
// section is never built - there is no flag here to set, and nothing to
// remember to turn off. Said here because the comment above this one was once
// left behind by a feature that arrived later (the pan axis), and a reader who
// trusts a stale header concludes the wrong thing about what this file draws.
"use strict";

(() => {
  const SEND_MS = 50;

  // Drag and wheel scales: degrees or units per pixel, units per wheel step.
  const TURN_PER_PX = 0.4;
  const LIFT_PER_PX = 0.3;
  const PAN_PER_PX = 0.3;
  const TILT_PER_PX = 0.1;
  const DISTANCE_PER_WHEEL = 0.1;  // wheel deltaY is about 100 per notch

  // Button steps.
  const TURN_STEP = 15;
  const DISTANCE_STEP = 10;
  const LIFT_STEP = 10;
  const TILT_STEP = 5;

  const stage = document.getElementById("stage");
  const note = document.getElementById("stage-note");
  const bar = document.getElementById("stage-bar");
  const readout = document.getElementById("stage-readout");

  let state = null;
  const pending = { turn: 0, distance: 0, lift: 0, pan: 0, tilt: 0 };
  let timer = null;

  function flush() {
    timer = null;
    const moved = pending.turn || pending.distance || pending.lift || pending.pan || pending.tilt;
    if (!moved) {
      return;
    }
    const data = {};
    for (const key of ["turn", "distance", "lift", "pan", "tilt"]) {
      if (pending[key]) {
        data[key] = pending[key];
        pending[key] = 0;
      }
    }
    LifecastBridge.send("stage-move", data);
  }

  function move(key, amount) {
    if (!state || !state.ready || !amount) {
      return;
    }
    if (key === "tilt" && !state.tilt) {
      return;
    }
    pending[key] += amount;
    if (timer === null) {
      timer = setTimeout(flush, SEND_MS);
    }
  }

  // ----- buttons -----------------------------------------------------------

  const buttons = [];

  function button(label, title, onClick, isAtLimit) {
    const b = LifecastControls.el("button", "panel-button stage-button", label);
    b.type = "button";
    b.title = title;
    b.addEventListener("click", onClick);
    buttons.push({ el: b, isAtLimit: isAtLimit });
    return b;
  }

  function group(title, items) {
    const g = LifecastControls.el("div", "stage-group");
    g.appendChild(LifecastControls.el("span", "stage-group-title", title));
    items.forEach((i) => g.appendChild(i));
    bar.appendChild(g);
    return g;
  }

  const near = 0.01;
  group("Turn", [
    button("\u25C0", "Turn left", () => move("turn", -TURN_STEP), null),
    button("\u25B6", "Turn right", () => move("turn", TURN_STEP), null),
  ]);
  group("Zoom", [
    button("+", "Closer", () => move("distance", -DISTANCE_STEP),
      (s) => s.distance <= s.limits.minDistance + near),
    button("\u2212", "Farther", () => move("distance", DISTANCE_STEP),
      (s) => s.distance >= s.limits.maxDistance - near),
  ]);
  group("Height", [
    button("\u25B2", "Camera up", () => move("lift", LIFT_STEP),
      (s) => s.lift >= s.limits.liftMax - near),
    button("\u25BC", "Camera down", () => move("lift", -LIFT_STEP),
      (s) => s.lift <= s.limits.liftMin + near),
  ]);
  const tiltGroup = group("Tilt", [
    button("\u25B2", "Tilt one way", () => move("tilt", TILT_STEP),
      (s) => s.tiltDeg >= s.limits.tiltMax - near),
    button("\u25BC", "Tilt the other way", () => move("tilt", -TILT_STEP),
      (s) => s.tiltDeg <= -s.limits.tiltMax + near),
  ]);
  const reframe = LifecastControls.el("button", "panel-button stage-reframe", "Reframe");
  reframe.type = "button";
  reframe.title = "Back to the view the menu opened with";
  reframe.addEventListener("click", () => {
    pending.turn = pending.distance = pending.lift = pending.pan = pending.tilt = 0;
    LifecastBridge.send("stage-reset");
  });
  bar.appendChild(reframe);

  // ----- mouse in the frame --------------------------------------------------

  let drag = null;

  // Controls that sit inside the frame and must keep their own mouse. The
  // tuning box is one: a drag started on it would capture the pointer and
  // swallow the mousedown, so its inputs could never take focus.
  function onControls(t) {
    return bar.contains(t) || (tuningBox !== null && tuningBox.contains(t));
  }

  stage.addEventListener("pointerdown", (e) => {
    if (onControls(e.target) || !state || !state.ready) {
      return;
    }
    const mode = e.button === 2 ? "lift" : (e.ctrlKey ? "tilt" : (e.button === 0 ? "turn" : null));
    if (!mode) {
      return;
    }
    drag = { mode: mode, x: e.clientX, y: e.clientY, id: e.pointerId };
    stage.setPointerCapture(e.pointerId);
    stage.classList.add("is-dragging");
    e.preventDefault();
  });

  stage.addEventListener("pointermove", (e) => {
    if (!drag || e.pointerId !== drag.id) {
      return;
    }
    const dx = e.clientX - drag.x;
    const dy = e.clientY - drag.y;
    drag.x = e.clientX;
    drag.y = e.clientY;
    if (drag.mode === "turn") {
      move("turn", dx * TURN_PER_PX);
    } else if (drag.mode === "lift") {
      // E4 (Fase 6.5): the author measured the old vertical-only, inverted
      // drag as wrong - "o eixo ta invertido... alem disso ele so mexe na
      // vertical." Both axes now follow the same grab convention as a map
      // or a Blender pan: the view moves WITH the cursor, so the camera
      // itself moves the opposite way.
      move("lift", dy * LIFT_PER_PX);
      move("pan", -dx * PAN_PER_PX);
    } else {
      move("tilt", -dy * TILT_PER_PX);
    }
  });

  function endDrag(e) {
    if (drag && e.pointerId === drag.id) {
      drag = null;
      stage.classList.remove("is-dragging");
    }
  }
  stage.addEventListener("pointerup", endDrag);
  stage.addEventListener("pointercancel", endDrag);
  stage.addEventListener("contextmenu", (e) => e.preventDefault());

  stage.addEventListener("wheel", (e) => {
    if (onControls(e.target)) {
      return;
    }
    move("distance", e.deltaY * DISTANCE_PER_WHEEL);
    e.preventDefault();
  }, { passive: false });

  // ----- the tuning knobs (development builds only) ---------------------------

  // These exist only when the DLL was built with LIFECAST_TUNING. There is no
  // preprocessor here to gate them with, and none is needed: a release DLL
  // never sends "tuning", so the section is never built and never shown. The
  // gate is the DLL's, which is the only place it can be enforced anyway.
  //
  // The seven are read from "tuning" and NOT from "limits". Four of them are in
  // both, and taking them from "limits" would quietly make this section stop
  // working the day a fourth number joins it that is not a bound.
  const TUNABLES = [
    ["maxDistance", "Max distance", "The farthest the camera can pull back", 1],
    ["liftUp", "Max height", "The highest it rises above where the menu opened", 1],
    ["panMax", "Max sideways", "How far it travels left and right", 1],
    ["tiltMax", "Max tilt", "How far it can tip up or down, in degrees", 1],
    ["pairApart", "Pair separation", "World distance between you and the second figure", 1],
    ["pairSpan", "Frame share", "How much of the view the pair spans, 0.5 being half", 0.01],
    ["ease", "Easing", "Share of the remaining gap covered per frame; 1 is no easing", 0.01],
  ];

  let tuningBox = null;
  const tuningInputs = new Map();

  function buildTuning() {
    tuningBox = LifecastControls.el("div", "stage-tuning");
    tuningBox.appendChild(
      LifecastControls.el("span", "stage-group-title", "Tuning (development build)"));
    for (const [key, label, title, step] of TUNABLES) {
      const row = LifecastControls.el("label", "stage-tuning-row");
      row.title = title;
      row.appendChild(LifecastControls.el("span", "stage-tuning-label", label));
      const input = document.createElement("input");
      input.type = "number";
      input.step = step;
      input.className = "stage-tuning-input";
      // "change" and not "input": committing on every keystroke sends a
      // message per digit, and typing "120" would pass through 1 and 12 -
      // each one a real camera move, and 1 is outside every sane bound.
      input.addEventListener("change", () => {
        const v = parseFloat(input.value);
        if (!isFinite(v)) {
          return;
        }
        LifecastBridge.send("stage-tune", { [key]: v });
      });
      tuningInputs.set(key, input);
      row.appendChild(input);
      tuningBox.appendChild(row);
    }
    bar.parentNode.insertBefore(tuningBox, readout.nextSibling);
  }

  function renderTuning() {
    const t = state && state.tuning;
    if (!t) {
      if (tuningBox) {
        tuningBox.hidden = true;
      }
      return;
    }
    if (!tuningBox) {
      buildTuning();
    }
    tuningBox.hidden = false;
    for (const [key] of TUNABLES) {
      const input = tuningInputs.get(key);
      // Never overwrite the box he is typing in. The DLL echoes the whole set
      // on every answer, and a tick answers while a value is half-typed.
      if (input && document.activeElement !== input && typeof t[key] === "number") {
        input.value = String(t[key]);
      }
    }
  }

  // ----- what the DLL says ----------------------------------------------------

  function render() {
    const ready = !!(state && state.ready);
    bar.hidden = !ready;
    readout.hidden = !ready;
    stage.classList.toggle("is-ready", ready);
    // Before the early return: a refused view carries no "tuning" either, and
    // the knobs have to go away with the rest of the controls.
    renderTuning();
    if (!ready) {
      note.hidden = false;
      note.textContent = "Character view unavailable" + (state && state.why ? ": " + state.why : "");
      return;
    }
    note.hidden = true;
    tiltGroup.hidden = !state.tilt;
    for (const b of buttons) {
      b.el.disabled = b.isAtLimit ? b.isAtLimit(state) : false;
    }
    readout.textContent =
      "turn " + Math.round(state.turn) + " deg  |  distance " + Math.round(state.distance) +
      "  |  height " + Math.round(state.lift) + "  |  pan " + Math.round(state.pan) +
      (state.tilt ? "  |  tilt " + Math.round(state.tiltDeg) + " deg" : "");
  }

  LifecastBridge.on("stage", (data) => {
    state = data && typeof data === "object" ? data : null;
    render();
  });

  // The DLL can hide the frame without touching anything else: the view
  // underneath stays exactly where it is.
  LifecastBridge.on("stage-visible", (data) => {
    stage.hidden = !(data && data.on);
  });

  // ----- telling the DLL where the stage IS ------------------------------
  // The camera has to centre the character in the stage - the window MINUS
  // the panel - and not in the window, so somebody has to know where the
  // stage's edges are. The page does: #stage is laid out from the panel's
  // right edge to the window's right edge, so its own rect IS the answer,
  // already including the panel's width, the panel scale the player set, the
  // belt's narrow form and the resolution. The DLL is told rather than
  // working it out, because none of those four are visible from C++ and any
  // constant there would be a number that is right on one screen.
  //
  // Measured, never assumed: the same reason control.js reads this rect for
  // the comparison instead of keeping it.
  function frame() {
    const width = window.innerWidth;
    if (!stage || stage.hidden || !(width > 0)) {
      return null;
    }
    const rect = stage.getBoundingClientRect();
    if (!(rect.width > 0)) {
      return null;
    }
    return { left: rect.left / width, right: rect.right / width };
  }

  // Resent only when the edges actually moved, and rounded to the fraction
  // the comparison is worth before that test: the observers below fire on
  // every frame of the panel's transition, and a solve per frame would be a
  // camera chasing an animation instead of following a layout. A twentieth of
  // a per cent of the window is far below the 2% the framing is judged by.
  const FRAME_EPSILON = 0.0002;
  let sentFrame = null;

  function sendFrame() {
    const where = frame();
    if (!where) {
      return;
    }
    if (sentFrame &&
        Math.abs(where.left - sentFrame.left) < FRAME_EPSILON &&
        Math.abs(where.right - sentFrame.right) < FRAME_EPSILON) {
      return;
    }
    if (LifecastBridge.send("stage-frame", { on: true, left: where.left, right: where.right })) {
      sentFrame = where;
    }
  }

  // The panel opening and closing is an attribute on <html> and the stage's
  // width follows it in CSS, so the attribute is not watched directly: the
  // stage's own resize is, and that catches the scale and the window too
  // without knowing about any of them. Same approach control.js uses to keep
  // its bar out of the camera bar's way.
  window.addEventListener("resize", sendFrame);
  if (typeof ResizeObserver !== "undefined") {
    new ResizeObserver(sendFrame).observe(stage);
  }

  render();
  LifecastBridge.whenAttached((ok) => {
    if (ok) {
      LifecastBridge.send("stage-get");
      sendFrame();
    }
  });
})();
