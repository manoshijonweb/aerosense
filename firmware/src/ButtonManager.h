#pragma once
// =============================================================================
// ButtonManager -- debounced short/long press detection for the two nav
// buttons. Produces discrete events consumed once each by UIManager; never
// blocks and never uses delay().
// =============================================================================
#include <Arduino.h>
#include "Config.h"

enum class ButtonEvent : uint8_t {
  NONE = 0,
  BTN1_SHORT,
  BTN1_LONG,
  BTN2_SHORT,
  BTN2_LONG
};

class ButtonManager {
public:
  void begin();
  void update();

  // Pops the oldest pending event, or ButtonEvent::NONE if the queue is empty.
  ButtonEvent popEvent();

private:
  struct ButtonState {
    uint8_t pin;
    bool stableLow = false;   // debounced pressed state
    bool rawLow = false;      // last raw sample
    uint32_t lastEdgeMs = 0;
    uint32_t pressStartMs = 0;
    bool longFired = false;

    explicit ButtonState(uint8_t p) : pin(p) {}
  };

  ButtonState _btn1{PIN_BTN1};
  ButtonState _btn2{PIN_BTN2};

  static const uint8_t QUEUE_SIZE = 4;
  ButtonEvent _queue[QUEUE_SIZE];
  uint8_t _queueHead = 0;
  uint8_t _queueTail = 0;

  void pushEvent(ButtonEvent evt);
  void updateButton(ButtonState &btn, ButtonEvent shortEvt, ButtonEvent longEvt);
};
