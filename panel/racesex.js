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
"use strict";

(() => {
  let pending = null;  // "race" or "sex" while a change is out, until the answer

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
  for (const [female, label] of [[false, "Male"], [true, "Female"]]) {
    const b = LifecastControls.el("button", "panel-button", label);
    b.type = "button";
    b.tabIndex = -1;
    b.dataset.female = female ? "1" : "0";
    b.addEventListener("click", () => {
      if (pending !== null || b.classList.contains("is-selected")) {
        return;
      }
      pending = "sex";
      sexStatus.textContent = "changing to " + label.toLowerCase() + "...";
      LifecastBridge.send("sex-set", { female: female });
    });
    sexButtons[female ? "female" : "male"] = b;
    sexBar.appendChild(b);
  }
  const sexStatus = LifecastControls.el("div", "preset-status", "--");
  sexBar.appendChild(sexStatus);
  sexSection.body.appendChild(sexBar);
  LifecastControls.mount(sexSection, "sex");

  function renderRaces(data) {
    const races = Array.isArray(data.races) ? data.races : [];
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
        pending = "race";
        raceStatus.textContent = "changing to " + name + "...";
        LifecastBridge.send("race-set", { i: i });
      });
      raceList.appendChild(li);
    });
    const cur = races[data.race];
    raceStatus.textContent = cur ? "Current: " + cur.name : "Current: not a race in this list";
  }

  function renderSex(data) {
    sexButtons.male.classList.toggle("is-selected", data.female === false);
    sexButtons.female.classList.toggle("is-selected", data.female === true);
    sexStatus.textContent = data.female === true ? "Current: female" : data.female === false ? "Current: male" : "--";
  }

  LifecastBridge.on("race-sex", (data) => {
    if (!data || data.ready !== true) {
      raceStatus.textContent = "not available";
      sexStatus.textContent = "not available";
      return;
    }
    renderRaces(data);
    renderSex(data);
  });

  LifecastBridge.on("race-sex-applied", (data) => {
    const what = data && typeof data.what === "string" ? data.what : pending;
    pending = null;
    if (data && data.ok) {
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
