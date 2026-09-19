#include "ButtonManager.h"

// Temporary: trace raw pin edges to tell a dead button apart from a dead code
// path. Set to 0 once the Button 2 wiring is resolved.
#define DEBUG_BUTTON_PINS 1

void ButtonManager::begin() {
  pinMode(_btn1.pin, INPUT_PULLUP);
  pinMode(_btn2.pin, INPUT_PULLUP);
}

void ButtonManager::update() {
  updateButton(_btn1, ButtonEvent::BTN1_SHORT, ButtonEvent::BTN1_LONG);
  updateButton(_btn2, ButtonEvent::BTN2_SHORT, ButtonEvent::BTN2_LONG);
}

void ButtonManager::updateButton(ButtonState &btn, ButtonEvent shortEvt, ButtonEvent longEvt) {
  bool rawLowNow = (digitalRead(btn.pin) == LOW);
  uint32_t now = millis();

  if (rawLowNow != btn.rawLow) {
#if DEBUG_BUTTON_PINS
    // Raw pin edges, before debounce. If a pin never appears here it is not
    // reaching the ESP32 at all, which is a wiring question, not a code one.
    Serial.printf("[btn] GPIO%u -> %s\n", (unsigned)btn.pin, rawLowNow ? "LOW (pressed)" : "HIGH");
#endif
    btn.rawLow = rawLowNow;
    btn.lastEdgeMs = now;
  }

  if ((now - btn.lastEdgeMs) >= BUTTON_DEBOUNCE_MS && btn.stableLow != btn.rawLow) {
    btn.stableLow = btn.rawLow;

    if (btn.stableLow) {
      // Just pressed.
      btn.pressStartMs = now;
      btn.longFired = false;
    } else {
      // Just released -- if a long-press event hasn't already fired, this was a short press.
      if (!btn.longFired) {
        pushEvent(shortEvt);
      }
    }
  }

  if (btn.stableLow && !btn.longFired && (now - btn.pressStartMs) >= BUTTON_LONGPRESS_MS) {
    btn.longFired = true;
    pushEvent(longEvt);
  }
}

void ButtonManager::pushEvent(ButtonEvent evt) {
  uint8_t nextTail = (_queueTail + 1) % QUEUE_SIZE;
  if (nextTail == _queueHead) return; // queue full, drop oldest-would-be-overwritten event
  _queue[_queueTail] = evt;
  _queueTail = nextTail;
}

ButtonEvent ButtonManager::popEvent() {
  if (_queueHead == _queueTail) return ButtonEvent::NONE;
  ButtonEvent evt = _queue[_queueHead];
  _queueHead = (_queueHead + 1) % QUEUE_SIZE;
  return evt;
}
