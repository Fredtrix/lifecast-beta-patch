// racesex.js
// Lifecast - race and sex, the last of RaceMenu's doors with a public way in
// (D125). The door was one row, "Race & Sex"; it comes inside as TWO controls
// in two categories, because that is where the author put them: the race is
// the Race category's picker (groups.js), the sex lives in Body (D133).
//
// Both are one expensive change each - the DLL switches the actor and redraws
// it whole - so a click applies and nothing is applied by browsing. The page
// shows what the DLL read back, never what was clicked: after every change
// the DLL sends "race-sex" again, from the game.
//
// What a change does to the REST of the panel is not decided here. The DLL
// goes through Session::Replaced, which is how every other section hears that
// the character changed under it (Edit/Session.h).
//
// Undo (E31, F47). A change enters the history like any other gesture, and it
// is undone the same way it was done: the same "race-set" / "sex-set" the click
// sent, so the DLL runs RaceMenu's own change and marks the race in the film's
// list. What the history cannot do by sending the old race back is give the
// character back: the game rewrites the face, the hair, the eye and tint
// colours and the weight when the race changes (E31 P0). So the entry carries
// the portrait of the whole panel as it was at the click (the F27 mechanism),
// and after the game has settled, puts back what differs. Settled means the
// "race-sex-applied" answer AND the sections that rebuild a moment later (the
// body morphs arrive up to ~0.8 s after it): the page waits for its rows to be
// as many as they were.
"use strict";

(() => {
  // A change out, until the answer: "race" or "sex".
  let pending = null;
  // What the DLL last said the player is. The race is an index into `races`.
  const cur = { race: -1, female: null };
  let races = [];
  // The change the player's own click started, until it is answered.
  let gesture = null;
  // Set while the history (undo/redo) is the one waiting for an answer.
  let waiter = null;
  let waiterTimer = null;
  const ANSWER_TIMEOUT_MS = 8000;
  const ROWS_TIMEOUT_MS = 3000;
  const ROWS_MARGIN_MS = 150;

  // --- Race: one row per playable race, the current one marked.
  const raceSection = LifecastControls.section("Race");
  const raceStatus = LifecastControls.el("div", "preset-status", "--");
  const raceList = LifecastControls.el("ul", "preset-list");
  raceSection.body.appendChild(raceStatus);
  raceSection.body.appendChild(raceList);
  LifecastControls.mount(raceSection, "race");

  // --- Sex: two buttons, the current one marked.
  const sexSection = LifecastControls.section("Sex");
  const sexBar = LifecastControls.el("div", "preset-bar");
  const sexButtons = {};

  // A change the player asked for: what the character holds now is taken
  // BEFORE the message goes, because it is what the change is about to erase.
  function begin(kind, target) {
    gesture = {
      kind: kind,
      target: target,
      from: { race: cur.race, female: cur.female, portrait: LifecastControls.portrait() },
    };
    pending = kind;
    if (kind === "sex") {
      LifecastBridge.send("sex-set", { female: target.female });
    } else {
      LifecastBridge.send("race-set", { i: target.race });
    }
  }

  for (const [female, label] of [[false, "Male"], [true, "Female"]]) {
    const b = LifecastControls.el("button", "panel-button", label);
    b.type = "button";
    b.tabIndex = -1;
    b.dataset.female = female ? "1" : "0";
    b.addEventListener("click", () => {
      if (pending !== null || b.classList.contains("is-selected")) {
        return;
      }
      sexStatus.textContent = "changing to " + label.toLowerCase() + "...";
      begin("sex", { female: female });
    });
    sexButtons[female ? "female" : "male"] = b;
    sexBar.appendChild(b);
  }
  const sexStatus = LifecastControls.el("div", "preset-status", "--");
  sexBar.appendChild(sexStatus);
  sexSection.body.appendChild(sexBar);
  LifecastControls.mount(sexSection, "sex");

  function renderRaces(data) {
    races = Array.isArray(data.races) ? data.races : [];
    raceList.replaceChildren();
    races.forEach((r, i) => {
      const name = r && typeof r.name === "string" && r.name !== "" ? r.name : "(unnamed)";
      const li = LifecastControls.el("li", "preset-row");
      li.dataset.index = String(i);
      li.appendChild(LifecastControls.el("span", "preset-name", name));
      // The FormID is for whoever is measuring, not for the player's eye.
      li.title = r && typeof r.id === "string" ? r.id : "";
      if (i === data.race) {
        li.classList.add("is-selected");
      }
      li.addEventListener("click", () => {
        if (pending !== null || i === data.race) {
          return;
        }
        raceStatus.textContent = "changing to " + name + "...";
        begin("race", { race: i });
      });
      raceList.appendChild(li);
    });
    const now = races[data.race];
    raceStatus.textContent = now ? "Current: " + now.name : "Current: not a race in this list";
  }

  function renderSex(data) {
    sexButtons.male.classList.toggle("is-selected", data.female === false);
    sexButtons.female.classList.toggle("is-selected", data.female === true);
    sexStatus.textContent = data.female === true ? "Current: female" : data.female === false ? "Current: male" : "--";
  }

  // --- The history's side (E31).

  function raceName(i) {
    const r = races[i];
    return r && typeof r.name === "string" && r.name !== "" ? r.name : "race " + String(i);
  }

  // The same message the click sends, asked for by the history. Resolves true
  // when the DLL answered ok (or the player already is there), false when it
  // refused or did not answer in time.
  function change(kind, v) {
    return new Promise((resolve) => {
      const there = kind === "race" ? cur.race === v.race : cur.female === v.female;
      if (there) {
        resolve(true);
        return;
      }
      if (pending !== null) {
        resolve(false);
        return;
      }
      pending = kind;
      gesture = null;
      waiter = resolve;
      waiterTimer = setTimeout(() => {
        if (waiter === resolve) {
          waiter = null;
          pending = null;
          resolve(false);
        }
      }, ANSWER_TIMEOUT_MS);
      if (kind === "sex") {
        sexStatus.textContent = "changing to " + (v.female ? "female" : "male") + "...";
        LifecastBridge.send("sex-set", { female: v.female });
      } else {
        raceStatus.textContent = "changing to " + raceName(v.race) + "...";
        LifecastBridge.send("race-set", { i: v.race });
      }
    });
  }

  // Resolves once the page holds as many rows as the portrait it is about to
  // put back (the late body morphs are the last to come), or after the cap.
  function waitRows(a_want) {
    return new Promise((resolve) => {
      const t0 = performance.now();
      const tick = () => {
        if (LifecastControls.portraitSize() >= a_want || performance.now() - t0 > ROWS_TIMEOUT_MS) {
          setTimeout(resolve, ROWS_MARGIN_MS);
        } else {
          setTimeout(tick, 50);
        }
      };
      tick();
    });
  }

  function landOn(a_entry, v) {
    if (v === a_entry.before) {
      // Undoing: every later entry is already undone, so the character is
      // exactly as this change left it. That is the portrait Redo puts back.
      a_entry.after.portrait = LifecastControls.portrait();
    }
    return change(v.kind, v).then((ok) => {
      if (!ok || !v.portrait) {
        return ok;
      }
      return waitRows(v.portrait.rows.length).then(() => {
        const n = LifecastControls.restorePortrait(v.portrait);
        (v.kind === "sex" ? sexStatus : raceStatus).textContent = "put back: " + String(n) + " values";
        return true;
      });
    });
  }

  function recordChange(g) {
    // A race the list does not hold cannot be sent back.
    if (g.kind === "race" ? g.from.race < 0 : g.from.female === null) {
      return;
    }
    const entry = {
      label: g.kind === "race" ? "Race" : "Sex",
      before: { kind: g.kind, race: g.from.race, female: g.from.female, portrait: g.from.portrait },
      after: { kind: g.kind, race: g.target.race, female: g.target.female, portrait: null },
      describe: (v) => ({
        kind: v.kind,
        race: v.race,
        female: v.female,
        portrait: v.portrait ? v.portrait.rows.length + " rows" : null,
      }),
      apply: (v) => landOn(entry, v),
    };
    LifecastHistory.record(entry);
  }

  LifecastBridge.on("race-sex", (data) => {
    if (!data || data.ready !== true) {
      raceStatus.textContent = "not available";
      sexStatus.textContent = "not available";
      return;
    }
    cur.race = typeof data.race === "number" ? data.race : -1;
    cur.female = typeof data.female === "boolean" ? data.female : null;
    renderRaces(data);
    renderSex(data);
  });

  LifecastBridge.on("race-sex-applied", (data) => {
    const what = data && typeof data.what === "string" ? data.what : pending;
    const ok = !!(data && data.ok);
    const g = gesture;
    gesture = null;
    pending = null;
    if (waiter !== null) {
      const w = waiter;
      waiter = null;
      clearTimeout(waiterTimer);
      w(ok);
    } else if (ok && g !== null && g.kind === what) {
      recordChange(g);
    }
    if (ok) {
      return;  // the "race-sex" that follows says what it is now
    }
    const why = data && typeof data.why === "string" && data.why !== "" ? " - " + data.why : "";
    (what === "sex" ? sexStatus : raceStatus).textContent = "not changed" + why;
  });

  LifecastBridge.whenAttached((ok) => {
    if (ok) {
      LifecastBridge.send("race-sex-get");
    }
  });
})();
