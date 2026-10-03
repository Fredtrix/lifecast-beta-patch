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

  stage.addEventListener("pointerdown", (e) => {
    if (bar.contains(e.target) || !state || !state.ready) {
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
    if (bar.contains(e.target)) {
      return;
    }
    move("distance", e.deltaY * DISTANCE_PER_WHEEL);
    e.preventDefault();
  }, { passive: false });

  // ----- what the DLL says ----------------------------------------------------

  function render() {
    const ready = !!(state && state.ready);
    bar.hidden = !ready;
    readout.hidden = !ready;
    stage.classList.toggle("is-ready", ready);
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

  render();
  LifecastBridge.whenAttached((ok) => {
    if (ok) {
      LifecastBridge.send("stage-get");
    }
  });
})();
