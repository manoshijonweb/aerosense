#pragma once
// =============================================================================
// Secrets.example.h -- template for the credentials the firmware needs.
//
// Copy this file to Secrets.h and fill it in. Secrets.h is gitignored; this
// template is the one that gets committed, so NEVER put real values here.
//
//   copy Secrets.example.h Secrets.h
//
// Every value below is documented in FIREBASE_SETUP.md, which walks through
// where each one comes from in the Firebase console.
// =============================================================================

// ------------------------------- Wi-Fi ---------------------------------------
// The network the device ships configured for. Leave WIFI_SSID as "" to skip
// stored credentials entirely and provision over SmartConfig only.
//
// 2.4 GHz only -- the ESP32 has no 5 GHz radio, and a phone hotspot defaulting
// to 5 GHz is the single most common reason a board never connects.
#define WIFI_SSID           ""
#define WIFI_PASSWORD       ""

// ------------------------------- Firebase ------------------------------------
// Console > Project settings > General > Web API Key
#define FIREBASE_API_KEY    ""

// Console > Realtime Database > the URL above the data tree. Include the
// scheme and no trailing slash, e.g.
//   https://aerosense-1234-default-rtdb.asia-southeast1.firebasedatabase.app
#define FIREBASE_DATABASE_URL ""

// The dedicated device account: Console > Authentication > Users > Add user.
// This is a service identity for the hardware, not a person -- give it an
// address you control on a domain you own, and a long random password.
#define FIREBASE_USER_EMAIL     ""
#define FIREBASE_USER_PASSWORD  ""

// ------------------------------- Identity ------------------------------------
// Where this unit writes in the database: /devices/<DEVICE_ID>/...
// Must be unique per physical device if you ever run more than one.
#define DEVICE_ID           "aerosense-01"
