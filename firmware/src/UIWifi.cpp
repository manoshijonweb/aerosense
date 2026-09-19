// =============================================================================
// UIWifi.cpp -- the Wi-Fi Setup screen.
//
// The screen is a mirror, not a control surface. Choosing a network and typing
// its password happen on the phone, through WifiPortal; this file's job is to
// tell the user what to join, and then to say honestly what is happening so
// they are never left watching a phone that has gone quiet.
//
// The single button binding is "hold Btn1 to leave", which also tears the
// access point down. Everything else the screen does, it does by itself.
// =============================================================================
#include "UIManager.h"

void UIManager::openWifiSetup() {
  // Both are needed: the portal to run the session, the manager to do the
  // connecting. An offline build passes neither, and the menu entry is inert.
  if (!_wifi || !_portal) return;
  _wifiOpen = true;
  _wifiEntered = false;
  _wifiDirty = true;
  _wifiLastState = PortalState::OFF;
  _wifiLastSecs = 0;
  _portal->start();
  _forceFrame = true;
}

void UIManager::closeWifiSetup() {
  if (_portal) _portal->stop();
  _wifiOpen = false;
  _pageEntered = false;   // full repaint of whatever was underneath
  _forceFrame = true;
}

void UIManager::handleWifiInput(ButtonEvent evt) {
  // A success closes itself once the phone has had a chance to see it, but the
  // user can always leave early.
  if (evt == ButtonEvent::BTN1_LONG || evt == ButtonEvent::BTN2_LONG) closeWifiSetup();
}

void UIManager::renderWifiSetup(bool full) {
  Adafruit_ST7735 &tft = _disp->tft();

  PortalState st = _portal->state();
  uint16_t secs = _portal->secondsLeft();

  // The portal closes itself on success or timeout; follow it out rather than
  // leaving a stale screen up over a session that no longer exists.
  if (st == PortalState::OFF && _wifiEntered) { closeWifiSetup(); return; }

  if (st != _wifiLastState || secs != _wifiLastSecs) {
    _wifiDirty = true;
    _wifiLastState = st;
    _wifiLastSecs = secs;
  }
  if (!full && !_wifiDirty) return;
  _wifiDirty = false;

  if (full) _disp->drawHeader("WI-FI SETUP");
  const int16_t bodyY = Theme::HEADER_H + 2;
  tft.fillRect(0, bodyY, SCREEN_WIDTH, SCREEN_HEIGHT - bodyY, Theme::BG);

  const int16_t hintY = SCREEN_HEIGHT - 10;
  char buf[40];

  switch (st) {
    case PortalState::WAITING:
    case PortalState::JOINED: {
      _disp->drawText(6, bodyY + 2, TextStyle::SMALL, Theme::TEXT_DIM, "1. JOIN THIS NETWORK");
      _disp->drawText(10, bodyY + 14, TextStyle::VALUE, Theme::ACCENT, WIFI_AP_SSID);

      if (strlen(WIFI_AP_PASSWORD) >= 8) {
        snprintf(buf, sizeof(buf), "password:  %s", WIFI_AP_PASSWORD);
        _disp->drawText(10, bodyY + 34, TextStyle::SMALL, Theme::TEXT, buf);
      } else {
        _disp->drawText(10, bodyY + 34, TextStyle::SMALL, Theme::TEXT_DIM, "no password");
      }

      _disp->drawText(6, bodyY + 50, TextStyle::SMALL, Theme::TEXT_DIM, "2. THE PAGE OPENS ITSELF");
      _disp->drawText(10, bodyY + 62, TextStyle::SMALL, Theme::TEXT, "or go to  192.168.4.1");

      if (st == PortalState::JOINED) {
        _disp->drawIcon(6, bodyY + 78, 8, IconType::CHECK, Theme::GOOD);
        _disp->drawText(20, bodyY + 78, TextStyle::SMALL, Theme::GOOD, "phone connected");
      } else {
        _disp->drawText(6, bodyY + 78, TextStyle::SMALL, Theme::TEXT_DIM, "waiting for a phone...");
      }

      snprintf(buf, sizeof(buf), "%u:%02u", secs / 60, secs % 60);
      _disp->drawTextRight(SCREEN_WIDTH - 6, hintY, TextStyle::SMALL, Theme::TEXT_DIM, buf);
      _disp->drawText(6, hintY, TextStyle::SMALL, Theme::TEXT_DIM, "Hold: cancel");
      break;
    }

    case PortalState::CONNECTING:
      _disp->drawTextCentered(46, TextStyle::VALUE, Theme::TEXT, "Connecting");
      _disp->drawTextCentered(70, TextStyle::SMALL, Theme::ACCENT, _portal->targetSsid());
      _disp->drawTextCentered(88, TextStyle::SMALL, Theme::TEXT_DIM, "keep the page open");
      _disp->drawText(6, hintY, TextStyle::SMALL, Theme::TEXT_DIM, "Hold: cancel");
      break;

    case PortalState::SUCCESS:
      _disp->drawIcon(SCREEN_WIDTH / 2 - 8, 32, 16, IconType::CHECK, Theme::GOOD);
      _disp->drawTextCentered(54, TextStyle::VALUE, Theme::GOOD, "Connected");
      _disp->drawTextCentered(78, TextStyle::SMALL, Theme::TEXT, _portal->targetSsid());
      _disp->drawTextCentered(92, TextStyle::SMALL, Theme::TEXT_DIM, "saved for next power-on");
      break;

    case PortalState::FAILED:
      _disp->drawIcon(SCREEN_WIDTH / 2 - 8, 30, 16, IconType::CROSS, Theme::HAZARDOUS);
      _disp->drawTextCentered(52, TextStyle::VALUE, Theme::HAZARDOUS, "Failed");
      _disp->drawTextCentered(74, TextStyle::SMALL, Theme::TEXT_DIM, "wrong password, or the");
      _disp->drawTextCentered(86, TextStyle::SMALL, Theme::TEXT_DIM, "network is not 2.4GHz");
      _disp->drawTextCentered(100, TextStyle::SMALL, Theme::ACCENT, "try again on the phone");
      _disp->drawText(6, hintY, TextStyle::SMALL, Theme::TEXT_DIM, "Hold: cancel");
      break;

    case PortalState::STARTING:
      _disp->drawTextCentered(50, TextStyle::VALUE, Theme::TEXT, "Starting");
      _disp->drawTextCentered(74, TextStyle::SMALL, Theme::TEXT_DIM, "pausing cloud upload...");
      _disp->drawText(6, hintY, TextStyle::SMALL, Theme::TEXT_DIM, "Hold: cancel");
      break;

    case PortalState::OFF:
      _disp->drawTextCentered(56, TextStyle::SMALL, Theme::TEXT_DIM, "closing...");
      break;
  }
}
