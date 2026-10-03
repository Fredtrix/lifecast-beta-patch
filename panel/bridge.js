// bridge.js
// Lifecast - the page side of the message channel to the DLL.
//
// Message shape, both directions:
//     { v: <protocol version>, type: "<name>", data: <any JSON> }
//
// Page to DLL goes through window.Lifecast.send(message), a fire-and-forget
// native: it returns at once and never blocks the page. DLL to page arrives
// as JSON text through window.Lifecast.on("message", fn).
//
// The message is passed to send() as an OBJECT, not as JSON.stringify(...):
// the native side receives every argument as a JSON document, so an object
// arrives as its JSON text and a string would arrive quoted and need a
// second decode.
"use strict";

const LifecastBridge = (() => {
  const PROTOCOL = 1;
  const EVENT_NAME = "message";

  // The native object is installed by the host. If the script runs before
  // it is there, wait for it a short while instead of failing outright;
  // this never blocks, it only schedules the next look.
  const ATTACH_TRY_MS = 100;
  const ATTACH_TRIES = 30;

  const handlers = new Map();
  let attached = false;
  const onAttach = [];

  function native() {
    const a = window.Lifecast;
    return a && typeof a.send === "function" && typeof a.on === "function" ? a : null;
  }

  function dispatch(text) {
    let message;
    try {
      message = typeof text === "string" ? JSON.parse(text) : text;
    } catch (e) {
      console.warn("[bridge] dll->page: not JSON", text);
      return;
    }
    if (!message || typeof message.type !== "string") {
      console.warn("[bridge] dll->page: no type", message);
      return;
    }
    const handler = handlers.get(message.type);
    if (handler) {
      try {
        handler(message.data, message);
      } catch (e) {
        console.error("[bridge] handler for " + message.type + " threw", e);
      }
    }
  }

  function attach(triesLeft) {
    const a = native();
    if (!a) {
      if (triesLeft > 0) {
        setTimeout(() => attach(triesLeft - 1), ATTACH_TRY_MS);
      } else {
        onAttach.splice(0).forEach((fn) => fn(false));
      }
      return;
    }
    a.on(EVENT_NAME, dispatch);
    attached = true;
    onAttach.splice(0).forEach((fn) => fn(true));
  }

  attach(ATTACH_TRIES);

  return {
    PROTOCOL,

    // Calls fn(true) once the native bridge is there, or fn(false) if it
    // never shows up - which is the case when the page is opened outside
    // the game.
    whenAttached(fn) {
      if (attached) {
        fn(true);
      } else {
        onAttach.push(fn);
      }
    },

    // Registers the handler for a DLL-to-page message type. One per type.
    on(type, fn) {
      handlers.set(type, fn);
    },

    // Sends a message to the DLL. Returns false when there is no bridge.
    send(type, data) {
      const a = native();
      if (!a) {
        return false;
      }
      try {
        a.send({ v: PROTOCOL, type: type, data: data === undefined ? {} : data });
        return true;
      } catch (e) {
        console.error("[bridge] send threw", e);
        return false;
      }
    },
  };
})();
