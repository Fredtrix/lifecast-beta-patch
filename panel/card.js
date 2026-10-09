/*
  The card for the way back, shown while RaceMenu's own film is in front.

  The browser stays on screen without focus then (Panel::ShowCard in the DLL),
  so the whole page is made invisible except this card: it is text, it takes no
  click, and the keyboard and the mouse stay with RaceMenu. The DLL sends
  "card" { on, key } when it goes in front and when it ends, and the page
  answers "card-state" with what it drew, read from its own DOM.
*/

(() => {
  "use strict";

  const card = document.getElementById("back-card");
  if (!card) {
    return;
  }

  function read(on) {
    const r = card.getBoundingClientRect();
    const cs = getComputedStyle(card);
    return {
      on: on,
      text: card.textContent,
      hidden: card.hidden,
      display: cs.display,
      visibility: cs.visibility,
      rect: [Math.round(r.left), Math.round(r.top), Math.round(r.width), Math.round(r.height)],
      viewport: [innerWidth, innerHeight],
    };
  }

  LifecastBridge.on("card", (data) => {
    const on = !!(data && data.on);
    if (on) {
      const key = data && typeof data.key === "string" ? data.key : "F4";
      card.textContent = "Press " + key + " to return to Lifecast";
    }
    card.hidden = !on;
    document.documentElement.classList.toggle("card-mode", on);
    if (on) {
      // After the layout has taken the class, so the rect is the drawn one.
      requestAnimationFrame(() => LifecastBridge.send("card-state", read(true)));
    }
  });
})();
