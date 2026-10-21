// =============================================================================
// captive_portal.h — CYBER CAR PRO — Secure portal + premium UI
// =============================================================================
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <esp_task_wdt.h>
#include <esp_system.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "bmp_resource.h"
#include "image_data.h"

static constexpr char SESSION_COOKIE_NAME[] = "cyber_car_session";

class PortalServer {
 public:
  explicit PortalServer(CarNavigator& nav) : server_(80), nav_(nav) {}

  void begin() {
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1),
                      IPAddress(255,255,255,0));
    WiFi.softAP(Config::AP_SSID, Config::AP_PASSWORD);
    generateToken();

    Serial.println(F("========================================="));
    Serial.println(F("  CYBER CAR PRO — CAPTIVE PORTAL READY  "));
    Serial.println(F("  http://192.168.4.1/                  "));
    Serial.println(F("========================================="));

    static const char* kHeaders[] = {"Cookie", "X-Session-Token"};
    server_.collectHeaders(kHeaders, 2);
    dns_.start(Config::DNS_PORT, "*", IPAddress(192,168,4,1));
    routes();
    server_.begin();
  }

  void update() {
    dns_.processNextRequest();
    server_.handleClient();
  }

 private:
  void generateToken() {
    static const char kHex[] = "0123456789ABCDEF";
    uint32_t randomWord = 0;
    for (uint8_t i = 0; i < Config::TOKEN_HEX_LENGTH; ++i) {
      if ((i % 8U) == 0) randomWord = esp_random();
      sessionToken_[i] = kHex[randomWord & 0x0FU];
      randomWord >>= 4U;
    }
    sessionToken_[Config::TOKEN_HEX_LENGTH] = '\0';
  }

  static bool constantTimeEquals(const char* a, size_t aLen,
                                 const char* b, size_t bLen) {
    if (aLen != bLen) return false;
    uint8_t diff = 0;
    for (size_t i = 0; i < bLen; ++i) diff |= static_cast<uint8_t>(a[i] ^ b[i]);
    return diff == 0;
  }

  bool tokenMatches(const String& supplied) const {
    return constantTimeEquals(supplied.c_str(), supplied.length(),
                              sessionToken_, Config::TOKEN_HEX_LENGTH);
  }

  bool hasSessionCookie() const {
    const String cookie = server_.header("Cookie");
    int start = 0;
    while (start < cookie.length()) {
      int end = cookie.indexOf(';', start);
      if (end < 0) end = cookie.length();
      String part = cookie.substring(start, end);
      part.trim();
      int sep = part.indexOf('=');
      if (sep > 0 && part.substring(0, sep).equals(SESSION_COOKIE_NAME)) {
        return tokenMatches(part.substring(sep + 1));
      }
      start = end + 1;
    }
    return false;
  }

  bool isAuthorized() const {
    if (hasSessionCookie()) return true;
    if (server_.hasHeader("X-Session-Token") &&
        tokenMatches(server_.header("X-Session-Token"))) return true;
    return server_.hasArg("token") && tokenMatches(server_.arg("token"));
  }

  bool requireAuth() {
    if (isAuthorized()) return true;
    sendJson(403, "{\"ok\":false,\"error\":\"unauthorized\"}");
    return false;
  }

  bool isRateLimited(uint32_t& last, uint32_t intervalMs) {
    uint32_t now = millis();
    if (last != 0 && !elapsed(now, last, intervalMs)) return true;
    last = now;
    return false;
  }

  void sendSecureHeaders() {
    server_.sendHeader("X-Frame-Options", "DENY");
    server_.sendHeader("X-Content-Type-Options", "nosniff");
    server_.sendHeader("Referrer-Policy", "no-referrer");
    server_.sendHeader("Cache-Control", "no-store, max-age=0");
    server_.sendHeader("Content-Security-Policy",
      "default-src 'self'; base-uri 'none'; frame-ancestors 'none'; "
      "connect-src 'self'; img-src 'self' data:; "
      "style-src 'self' 'unsafe-inline'; script-src 'self' 'unsafe-inline'");
  }

  void sendJson(int status, const char* body) {
    sendSecureHeaders();
    server_.send(status, "application/json; charset=utf-8", body);
  }
  void sendBadRequest(const char* msg) {
    char body[128];
    snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"%s\"}", msg);
    sendJson(400, body);
  }
  void sendRateLimited() { sendJson(429, "{\"ok\":false,\"error\":\"rate_limited\"}"); }

  bool parseBoundedInt(const char* name, int lo, int hi, int& out) {
    if (!server_.hasArg(name)) return false;
    const String value = server_.arg(name);
    if (value.length() == 0) return false;
    char* end = nullptr;
    long parsed = strtol(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != '\0' || parsed < lo || parsed > hi)
      return false;
    out = static_cast<int>(parsed);
    return true;
  }

  void routes() {
    server_.on("/", HTTP_GET, [this]() { index(); });
    server_.on("/image", HTTP_GET, [this]() { if(!requireAuth())return; serveImage(); });

    server_.on("/api/joy",   HTTP_GET, [this]() { if(!requireAuth())return; if(isRateLimited(lastJoyAt_,45))   return sendRateLimited(); joy();   });
    server_.on("/api/speed", HTTP_GET, [this]() { if(!requireAuth())return; if(isRateLimited(lastCtrlAt_,75))  return sendRateLimited(); speed(); });
    server_.on("/api/ball",  HTTP_GET, [this]() { if(!requireAuth())return; if(isRateLimited(lastCtrlAt_,75))  return sendRateLimited(); ball();  });
    server_.on("/api/mode",  HTTP_GET, [this]() { if(!requireAuth())return; if(isRateLimited(lastCtrlAt_,75))  return sendRateLimited(); mode();  });
    server_.on("/api/dance", HTTP_GET, [this]() { if(!requireAuth())return; if(isRateLimited(lastCtrlAt_,75))  return sendRateLimited(); dance(); });
    server_.on("/api/horn",  HTTP_GET, [this]() { if(!requireAuth())return; if(isRateLimited(lastCtrlAt_,75))  return sendRateLimited(); nav_.horn(); sendJson(200,"{\"ok\":true}"); });
    server_.on("/api/brake", HTTP_GET, [this]() { if(!requireAuth())return; nav_.emergencyStop(); sendJson(200,"{\"ok\":true}"); });
    server_.on("/api/state", HTTP_GET, [this]() { if(!requireAuth())return; if(isRateLimited(lastStateAt_,200)) return sendRateLimited(); state(); });

    server_.on("/hotspot-detect.html", HTTP_GET, [this]() { redirectToPortal(); });
    server_.on("/generate_204",        HTTP_GET, [this]() { redirectToPortal(); });
    server_.on("/connecttest.txt",     HTTP_GET, [this]() { redirectToPortal(); });
    server_.on("/ncsi.txt",            HTTP_GET, [this]() { redirectToPortal(); });
    server_.on("/fwlink",              HTTP_GET, [this]() { redirectToPortal(); });
    server_.onNotFound([this]() { index(); });
  }

  void redirectToPortal() {
    sendSecureHeaders();
    server_.sendHeader("Location", "http://192.168.4.1/");
    server_.send(302, "text/plain; charset=utf-8", "");
  }

  void index() {
    sendSecureHeaders();
    String cookie = String(SESSION_COOKIE_NAME) + "=" + sessionToken_ +
                    "; Path=/; HttpOnly; SameSite=Strict";
    server_.sendHeader("Set-Cookie", cookie);
    server_.send_P(200, "text/html; charset=utf-8", HTML);
  }

  void serveImage() {
    sendSecureHeaders();
    server_.sendHeader("Cache-Control", "public, max-age=3600");
    server_.setContentLength(sizeof(BMP_HEADER) + IMAGE_BYTES);
    server_.send(200, "image/bmp", "");
    server_.sendContent_P((PGM_P)BMP_HEADER, sizeof(BMP_HEADER));
    const uint8_t* base =
      (const uint8_t*)epd_bitmap_Gemini_Generated_Image_n94ijfn94ijfn94i;
    for (uint32_t off = 0; off < IMAGE_BYTES; off += 1024) {
      uint32_t chunk = (IMAGE_BYTES - off > 1024) ? 1024 : (IMAGE_BYTES - off);
      server_.sendContent_P((PGM_P)(base + off), chunk);
      esp_task_wdt_reset();
      delay(1);
    }
  }

  void joy() {
    int x = 0, y = 0;
    if (!parseBoundedInt("x", -100, 100, x) ||
        !parseBoundedInt("y", -100, 100, y)) {
      return sendBadRequest("invalid_joystick");
    }
    char cmd = 'S';
    if (abs(x) > abs(y)) { if (x > 30) cmd = 'R'; else if (x < -30) cmd = 'L'; }
    else { if (y > 30) cmd = 'F'; else if (y < -30) cmd = 'B'; }
    if (!nav_.handleManual(cmd, millis())) {
      return sendJson(409, "{\"ok\":false,\"error\":\"manual_mode_required\"}");
    }
    sendJson(200, "{\"ok\":true}");
  }
  void speed() {
    int v = 0;
    if (!parseBoundedInt("v", 0, 100, v)) return sendBadRequest("invalid_speed");
    nav_.motors().setSpeed(static_cast<uint8_t>(v));
    sendJson(200, "{\"ok\":true}");
  }
  void ball() {
    if (!server_.hasArg("a")) return sendBadRequest("missing_ball_action");
    String action = server_.arg("a");
    if (action == "release") nav_.releaseBall();
    else if (action == "lock") nav_.lockBall();
    else return sendBadRequest("invalid_ball_action");
    sendJson(200, "{\"ok\":true}");
  }
  void mode() {
    if (!server_.hasArg("m")) return sendBadRequest("missing_mode");
    String m = server_.arg("m");
    if (m == "manual") nav_.setMode(OperatingMode::Manual);
    else if (m == "auto") nav_.setMode(OperatingMode::Autonomous);
    else if (m == "rl") nav_.setMode(OperatingMode::ReinforcementLearning);
    else return sendBadRequest("invalid_mode");
    sendJson(200, "{\"ok\":true}");
  }
  void dance() {
    if (!nav_.startDance(millis())) {
      return sendJson(409, "{\"ok\":false,\"error\":\"dance_unavailable\"}");
    }
    sendJson(200, "{\"ok\":true}");
  }

  uint8_t batteryPercentage() const {
    int raw = analogRead(Config::BATTERY_PIN);
    float voltage = (static_cast<float>(raw) / Config::BATTERY_ADC_MAX) *
                    Config::ADC_REFERENCE_V * Config::BATTERY_DIVIDER_RATIO;
    long pct = lroundf((voltage - Config::BATTERY_EMPTY_V) * 100.0f /
                       (Config::BATTERY_FULL_V - Config::BATTERY_EMPTY_V));
    return static_cast<uint8_t>(constrain(pct, 0L, 100L));
  }

  void state() {
    uint32_t now = millis();
    char body[384];
    snprintf(body, sizeof(body),
      "{\"dist\":%u,\"speed\":%u,\"ball\":\"%s\",\"sensorOk\":%s,"
      "\"mode\":\"%s\",\"cmd\":\"%s\",\"safety\":%s,\"bat\":%u,\"heap\":%u,"
      "\"rl\":{\"updates\":%u,\"epsilon\":%u,\"best\":%d,\"episodes\":%u}}",
      static_cast<unsigned>(nav_.distance()),
      static_cast<unsigned>(nav_.motors().speedLimit()),
      nav_.ballLocked() ? "locked" : "released",
      nav_.sensorFresh(now) ? "true" : "false",
      nav_.modeName(), nav_.commandName(),
      nav_.safetyStopped() ? "true" : "false",
      static_cast<unsigned>(batteryPercentage()),
      static_cast<unsigned>(ESP.getFreeHeap()),
      static_cast<unsigned>(nav_.rlUpdates()),
      static_cast<unsigned>(nav_.rlEpsilonPerMille()),
      static_cast<int>(nav_.rlBestEpisode()),
      static_cast<unsigned>(nav_.rlEpisodeCount()));
    sendJson(200, body);
  }

  DNSServer dns_;
  WebServer server_;
  CarNavigator& nav_;
  char sessionToken_[Config::TOKEN_HEX_LENGTH + 1] = {};
  uint32_t lastJoyAt_   = 0;
  uint32_t lastCtrlAt_  = 0;
  uint32_t lastStateAt_ = 0;

  static const char HTML[] PROGMEM;
};

// =============================================================================
// PREMIUM UI — no emojis, refined cyberpunk layout
// =============================================================================
const char PortalServer::HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en" dir="ltr" id="htmlRoot">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
<meta name="theme-color" content="#050505">
<title>CYBER CAR PRO — AI & Robotics</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Space+Grotesk:wght@500;600;700&family=Inter:wght@400;500;600;700&display=swap" rel="stylesheet">
<style>
  :root{
    --bg:#050506;
    --bg-2:#0a0a0d;
    --panel:rgba(18,18,22,0.78);
    --panel-2:rgba(28,28,34,0.55);
    --stroke:rgba(255,230,0,0.14);
    --stroke-2:rgba(255,230,0,0.42);
    --stroke-soft:rgba(255,255,255,0.06);
    --yellow:#FFE600;
    --gold:#FFB800;
    --amber:#FF9500;
    --cyan:#00D9FF;
    --danger:#FF003C;
    --ok:#00FF88;
    --text:#F4F4F6;
    --muted:#8B8B95;
    --faint:#3A3A42;
    --f-display:'Space Grotesk','Inter',system-ui,sans-serif;
    --f-body:'Inter',system-ui,sans-serif;
  }
  *{box-sizing:border-box;margin:0;padding:0;-webkit-user-select:none;user-select:none;-webkit-tap-highlight-color:transparent;}
  html,body{background:var(--bg);color:var(--text);font-family:var(--f-body);min-height:100vh;overflow-x:hidden;}

  /* Background layers */
  body::before{
    content:"";position:fixed;inset:0;pointer-events:none;z-index:0;
    background-image:
      radial-gradient(circle at 50% -10%,rgba(255,230,0,0.10) 0%,transparent 55%),
      radial-gradient(circle at 100% 100%,rgba(0,217,255,0.06) 0%,transparent 45%),
      linear-gradient(rgba(255,230,0,0.035) 1px,transparent 1px),
      linear-gradient(90deg,rgba(255,230,0,0.035) 1px,transparent 1px);
    background-size:100% 100%,100% 100%,46px 46px,46px 46px;
    background-position:0 0,0 0,
      calc(var(--bgScroll,0)*0.20px) calc(var(--bgScroll,0)*0.12px),
      calc(var(--bgScroll,0)*0.12px) calc(var(--bgScroll,0)*0.20px);
    will-change:background-position;
  }
  body::after{
    content:"";position:fixed;inset:0;pointer-events:none;z-index:1;
    background:
      repeating-linear-gradient(0deg,rgba(0,0,0,0) 0px,rgba(0,0,0,0) 2px,
        rgba(0,0,0,0.09) 3px,rgba(0,0,0,0) 4px),
      radial-gradient(ellipse at center,transparent 40%,rgba(0,0,0,0.55) 100%);
    mix-blend-mode:overlay;
  }

  /* Topbar */
  .topbar{
    position:relative;z-index:3;
    width:100%;max-width:500px;margin:0 auto;
    display:flex;justify-content:space-between;align-items:center;
    padding:18px 22px 10px;
  }
  .brand{
    display:flex;align-items:center;gap:12px;
    font-family:var(--f-display);font-weight:700;
    letter-spacing:3px;font-size:0.72rem;color:var(--yellow);
  }
  .brand-dot{
    width:10px;height:10px;border-radius:50%;
    background:var(--muted);
    box-shadow:0 0 8px var(--muted);
    transition:all .4s ease;
  }
  .brand-dot.online{
    background:var(--ok);
    box-shadow:0 0 10px var(--ok),0 0 22px rgba(0,255,136,0.45);
    animation:pulse 1.8s ease-in-out infinite;
  }
  .brand-dot.offline{
    background:var(--danger);
    box-shadow:0 0 10px var(--danger),0 0 22px rgba(255,0,60,0.5);
  }
  @keyframes pulse{
    0%,100%{opacity:1;transform:scale(1);}
    50%{opacity:0.6;transform:scale(0.8);}
  }
  .lang-btn{
    background:transparent;border:1px solid var(--stroke-2);
    color:var(--yellow);padding:6px 14px;border-radius:6px;
    font-family:var(--f-display);font-weight:600;
    font-size:0.6rem;letter-spacing:2px;
    cursor:pointer;transition:all .2s;
  }
  .lang-btn:active{background:var(--yellow);color:#000;}

  /* Hero */
  .hero{
    position:relative;z-index:3;
    width:100%;max-width:500px;margin:4px auto 20px;
    aspect-ratio:1408/768;border-radius:20px;
    overflow:hidden;border:1px solid var(--stroke);
    box-shadow:
      0 28px 70px rgba(0,0,0,0.9),
      inset 0 0 0 1px rgba(255,230,0,0.05);
    transform:translateY(calc(var(--heroLift,0)*1px));
    will-change:transform;
  }
  .hero-img{
    width:100%;height:100%;object-fit:cover;display:block;
    transform:scale(1.14) translateY(calc(var(--heroScroll,0)*0.16px));
    transition:transform .05s linear;
    filter:contrast(1.07) brightness(0.9) saturate(0.95);
    will-change:transform;
  }
  .hero::after{
    content:"";position:absolute;inset:0;pointer-events:none;
    background:linear-gradient(180deg,
      rgba(5,5,6,0.05) 0%,
      rgba(5,5,6,0.25) 45%,
      rgba(5,5,6,0.94) 100%);
  }
  .hero-text{position:absolute;left:22px;right:22px;bottom:20px;z-index:2;}
  .hero-tag{
    display:inline-block;font-family:var(--f-display);font-weight:600;
    font-size:0.55rem;letter-spacing:4px;color:var(--yellow);
    padding:5px 12px;border:1px solid var(--stroke-2);border-radius:5px;
    margin-bottom:12px;background:rgba(0,0,0,0.4);backdrop-filter:blur(6px);
  }
  .hero-text h1{
    font-family:var(--f-display);font-weight:700;
    font-size:1.7rem;letter-spacing:1.5px;line-height:1.05;
    background:linear-gradient(90deg,var(--yellow) 0%,var(--gold) 55%,var(--amber) 100%);
    -webkit-background-clip:text;background-clip:text;
    -webkit-text-fill-color:transparent;
    filter:drop-shadow(0 2px 14px rgba(255,230,0,0.35));
    margin-bottom:6px;
  }
  .hero-text p{
    font-size:0.58rem;letter-spacing:3px;color:var(--muted);
    text-transform:uppercase;font-weight:500;
  }

  /* Container */
  .container{
    position:relative;z-index:3;
    width:100%;max-width:500px;
    padding:0 22px;display:flex;flex-direction:column;
    gap:14px;margin:0 auto;
  }

  /* Card */
  .card{
    background:var(--panel);
    backdrop-filter:blur(18px);-webkit-backdrop-filter:blur(18px);
    border:1px solid var(--stroke);border-radius:16px;
    padding:18px;position:relative;
    box-shadow:0 12px 34px rgba(0,0,0,0.6);
    transform-style:preserve-3d;
    transition:transform .15s ease-out,border-color .3s;
    opacity:0;transform:translateY(28px);
    will-change:opacity,transform;
  }
  .card.revealed{
    opacity:1;transform:translateY(0);
    transition:opacity .7s ease-out,transform .7s cubic-bezier(0.22,0.9,0.3,1);
  }
  .card.tilting{transition:transform .08s linear;}
  .card::before{
    content:"";position:absolute;top:14px;bottom:14px;left:-1px;width:3px;
    background:linear-gradient(180deg,var(--yellow) 0%,var(--gold) 60%,var(--amber) 100%);
    border-radius:3px;opacity:0.9;
  }
  .card-label{
    font-family:var(--f-display);font-weight:700;
    font-size:0.55rem;letter-spacing:3.5px;color:var(--muted);
    text-transform:uppercase;margin-bottom:14px;
    display:flex;align-items:center;gap:10px;
  }
  .card-label::after{
    content:"";flex:1;height:1px;
    background:linear-gradient(90deg,var(--stroke) 0%,transparent 100%);
  }

  /* Telemetry */
  .stats{display:grid;grid-template-columns:1fr 1fr 1fr;gap:10px;}
  .stat{
    background:var(--panel-2);
    border:1px solid var(--stroke-soft);
    border-radius:12px;padding:14px 10px;text-align:center;
    transition:border-color .3s;
  }
  .stat:hover{border-color:var(--stroke);}
  .stat-label{
    font-size:0.52rem;color:var(--muted);
    text-transform:uppercase;letter-spacing:1.5px;
    margin-bottom:8px;font-weight:600;
  }
  .stat-val{
    font-family:var(--f-display);font-weight:700;
    font-size:1.25rem;color:var(--yellow);line-height:1;
    display:flex;align-items:baseline;justify-content:center;gap:3px;
  }
  .stat-val small{font-size:0.55rem;color:var(--muted);font-weight:500;}
  .stat-val.gold{color:var(--gold);font-size:0.72rem;letter-spacing:1px;}
  .stat-val.battery{color:var(--ok);}
  .stat-val.battery.low{color:var(--danger);}

  /* Segmented battery */
  .bat-seg{
    display:flex;gap:2px;margin-top:8px;
    padding:2px;background:rgba(0,0,0,0.5);
    border-radius:3px;border:1px solid rgba(255,255,255,0.06);
  }
  .bat-seg .seg{
    flex:1;height:4px;border-radius:1px;
    background:rgba(255,255,255,0.06);
    transition:background .3s;
  }
  .bat-seg .seg.on{background:var(--ok);}
  .bat-seg .seg.mid{background:var(--gold);}
  .bat-seg .seg.low{background:var(--danger);}

  /* Modes */
  .modes{
    display:grid;grid-template-columns:repeat(3,1fr);gap:8px;
    background:rgba(0,0,0,0.5);padding:6px;border-radius:12px;
    border:1px solid var(--stroke-soft);
  }
  .mode-btn{
    background:transparent;border:none;color:var(--muted);
    padding:11px 0;border-radius:8px;
    font-family:var(--f-display);font-weight:700;
    font-size:0.62rem;letter-spacing:1.5px;
    cursor:pointer;transition:all .25s;
    position:relative;overflow:hidden;
  }
  .mode-btn::after{
    content:"";position:absolute;inset:0;
    background:radial-gradient(circle at center,rgba(255,230,0,0.15),transparent 70%);
    opacity:0;transition:opacity .3s;pointer-events:none;
  }
  .mode-btn.active{
    background:linear-gradient(180deg,rgba(255,230,0,0.22),rgba(255,184,0,0.10));
    color:var(--yellow);border:1px solid var(--stroke-2);
    box-shadow:0 0 14px rgba(255,230,0,0.15),inset 0 0 8px rgba(255,230,0,0.08);
  }
  .mode-btn.active::after{opacity:1;}
  .mode-btn:active{transform:scale(0.96);}

  /* AI panel (only visible in RL mode) */
  .ai-panel{
    margin-top:14px;padding-top:14px;
    border-top:1px solid var(--stroke-soft);
    display:none;animation:fadeIn .4s ease;
  }
  .ai-panel.show{display:block;}
  @keyframes fadeIn{from{opacity:0;transform:translateY(-4px);}to{opacity:1;transform:translateY(0);}}
  .ai-grid{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;}
  .ai-cell{
    background:rgba(0,0,0,0.45);
    border:1px solid var(--stroke-soft);
    border-radius:8px;padding:8px 6px;text-align:center;
  }
  .ai-cell .k{
    font-size:0.48rem;color:var(--muted);
    letter-spacing:1px;text-transform:uppercase;
    font-weight:600;margin-bottom:4px;
  }
  .ai-cell .v{
    font-family:var(--f-display);font-weight:700;
    font-size:0.85rem;color:var(--cyan);
  }
  .eps-bar{
    margin-top:10px;
    padding:2px;background:rgba(0,0,0,0.5);
    border-radius:4px;border:1px solid var(--stroke-soft);
  }
  .eps-bar-track{
    height:4px;background:rgba(255,255,255,0.05);
    border-radius:2px;overflow:hidden;
  }
  .eps-bar-fill{
    height:100%;width:50%;
    background:linear-gradient(90deg,var(--cyan),var(--ok));
    border-radius:2px;transition:width .4s ease;
  }
  .eps-bar-label{
    display:flex;justify-content:space-between;
    font-size:0.5rem;color:var(--muted);
    margin-bottom:5px;letter-spacing:1px;
    text-transform:uppercase;font-weight:600;
  }

  /* Speed */
  .slider-head{
    display:flex;justify-content:space-between;align-items:center;
    margin-bottom:12px;
  }
  .slider-head span:first-child{
    font-size:0.58rem;color:var(--muted);
    letter-spacing:2px;text-transform:uppercase;font-weight:600;
  }
  .slider-head span:last-child{
    font-family:var(--f-display);font-weight:700;
    color:var(--yellow);font-size:1rem;
  }
  input[type=range]{
    -webkit-appearance:none;width:100%;
    background:transparent;outline:none;
  }
  input[type=range]::-webkit-slider-runnable-track{
    width:100%;height:5px;
    background:linear-gradient(90deg,
      rgba(255,230,0,0.15) 0%,
      rgba(255,230,0,0.35) 100%);
    border-radius:3px;
  }
  input[type=range]::-webkit-slider-thumb{
    -webkit-appearance:none;
    height:20px;width:20px;border-radius:50%;
    background:radial-gradient(circle at 30% 30%,#FFF 0%,var(--yellow) 50%,var(--gold) 100%);
    margin-top:-8px;cursor:pointer;
    box-shadow:0 0 14px rgba(255,230,0,0.7),0 2px 6px rgba(0,0,0,0.6);
    transition:transform .15s;
  }
  input[type=range]::-webkit-slider-thumb:active{transform:scale(1.18);}

  /* Joystick */
  .joy-wrap{display:flex;justify-content:center;padding:18px 0 6px;}
  #joystick{
    width:210px;height:210px;
    background:radial-gradient(circle at center,
      rgba(255,230,0,0.05) 0%,
      rgba(0,0,0,0.94) 70%);
    border:1px solid var(--stroke-2);border-radius:50%;
    position:relative;display:flex;justify-content:center;
    align-items:center;touch-action:none;
    box-shadow:
      inset 0 0 50px rgba(0,0,0,0.9),
      inset 0 0 0 6px rgba(255,230,0,0.03),
      0 0 40px rgba(255,230,0,0.08);
  }
  #joystick::before{
    content:"";position:absolute;inset:30px;border-radius:50%;
    border:1px dashed rgba(255,230,0,0.12);pointer-events:none;
  }
  #joystick::after{
    content:"";position:absolute;inset:75px;border-radius:50%;
    border:1px solid rgba(255,230,0,0.08);pointer-events:none;
  }
  .crosshair{position:absolute;inset:0;pointer-events:none;opacity:0.28;}
  .crosshair::before,
  .crosshair::after{
    content:"";position:absolute;
    background:linear-gradient(90deg,transparent,var(--yellow),transparent);
  }
  .crosshair::before{width:100%;height:1px;top:50%;left:0;}
  .crosshair::after{
    width:1px;height:100%;left:50%;top:0;
    background:linear-gradient(180deg,transparent,var(--yellow),transparent);
  }
  /* Directional arrows */
  .dir-arrow{
    position:absolute;
    width:0;height:0;
    opacity:0.25;transition:opacity .15s;
    pointer-events:none;
  }
  .dir-arrow.active{opacity:1;filter:drop-shadow(0 0 6px var(--yellow));}
  .dir-up{
    top:14px;left:50%;transform:translateX(-50%);
    border-left:9px solid transparent;
    border-right:9px solid transparent;
    border-bottom:14px solid var(--yellow);
  }
  .dir-down{
    bottom:14px;left:50%;transform:translateX(-50%);
    border-left:9px solid transparent;
    border-right:9px solid transparent;
    border-top:14px solid var(--yellow);
  }
  .dir-left{
    left:14px;top:50%;transform:translateY(-50%);
    border-top:9px solid transparent;
    border-bottom:9px solid transparent;
    border-right:14px solid var(--yellow);
  }
  .dir-right{
    right:14px;top:50%;transform:translateY(-50%);
    border-top:9px solid transparent;
    border-bottom:9px solid transparent;
    border-left:14px solid var(--yellow);
  }
  #stick{
    width:70px;height:70px;border-radius:50%;
    background:radial-gradient(circle at 35% 30%,#FFF6A8 0%,var(--yellow) 40%,var(--gold) 100%);
    position:absolute;
    box-shadow:
      0 0 24px rgba(255,230,0,0.7),
      0 0 48px rgba(255,230,0,0.28),
      inset 0 -5px 14px rgba(0,0,0,0.4),
      inset 0 3px 6px rgba(255,255,255,0.45);
    pointer-events:none;transition:transform .05s linear;
  }
  #stick::after{
    content:"";position:absolute;inset:13px;border-radius:50%;
    background:radial-gradient(circle,rgba(0,0,0,0.45) 0%,transparent 70%);
  }

  /* Actions */
  .actions{display:grid;grid-template-columns:1fr 1fr;gap:10px;}
  .action-btn{
    background:rgba(255,255,255,0.03);
    border:1px solid rgba(255,255,255,0.08);
    color:var(--text);padding:15px 10px;border-radius:10px;
    font-family:var(--f-display);font-weight:700;
    font-size:0.6rem;letter-spacing:1.5px;
    cursor:pointer;transition:all .18s;
    display:flex;align-items:center;justify-content:center;
    text-align:center;position:relative;overflow:hidden;
  }
  .action-btn::before{
    content:"";position:absolute;inset:0;
    background:radial-gradient(circle at center,rgba(255,255,255,0.15),transparent 70%);
    opacity:0;transition:opacity .15s;pointer-events:none;
  }
  .action-btn:active{transform:scale(0.96);}
  .action-btn:active::before{opacity:1;}
  .action-btn.warn{border-color:rgba(255,184,0,0.42);color:var(--gold);}
  .action-btn.warn:active{background:rgba(255,184,0,0.08);}
  .action-btn.dim{border-color:rgba(255,255,255,0.06);color:var(--muted);}
  .action-btn.dim:active{background:rgba(255,255,255,0.05);}
  .action-btn.danger{
    border-color:rgba(255,0,60,0.55);color:var(--danger);
    grid-column:span 2;font-size:0.7rem;
    background:linear-gradient(180deg,rgba(255,0,60,0.05),rgba(255,0,60,0.02));
    box-shadow:inset 0 0 22px rgba(255,0,60,0.08);
    padding:17px 10px;
  }
  .action-btn.danger:active{background:rgba(255,0,60,0.15);}

  /* Team */
  .team{padding:0;background:transparent;border:none;box-shadow:none;}
  .team-inner{
    background:rgba(0,0,0,0.5);
    border:1px dashed rgba(255,230,0,0.18);
    border-radius:16px;padding:24px 20px;
    position:relative;overflow:hidden;
  }
  .team-inner::before{
    content:"";position:absolute;inset:-50%;
    background:conic-gradient(from 0deg,
      transparent 0deg,rgba(255,230,0,0.025) 90deg,
      transparent 180deg,transparent 360deg);
    animation:spin 26s linear infinite;
    pointer-events:none;
  }
  @keyframes spin{to{transform:rotate(360deg);}}
  .team-inner > *{position:relative;}
  .team-badge{
    display:inline-block;font-family:var(--f-display);font-weight:700;
    font-size:0.5rem;letter-spacing:3px;
    color:var(--yellow);
    padding:6px 14px;border-radius:20px;
    border:1px solid var(--stroke-2);
    background:rgba(255,230,0,0.06);
    margin-bottom:14px;
  }
  .team h2{
    font-family:var(--f-display);font-weight:700;
    font-size:1.05rem;color:var(--yellow);
    letter-spacing:2px;text-transform:uppercase;
    margin-bottom:8px;
  }
  .team-sub{
    font-size:0.7rem;color:var(--muted);
    margin-bottom:18px;line-height:1.6;
  }
  .team-sub b{color:var(--text);font-weight:600;}
  .team-grid{
    display:grid;grid-template-columns:1fr 1fr;gap:10px;
  }
  .member{
    background:rgba(255,230,0,0.03);
    border:1px solid rgba(255,230,0,0.10);
    border-radius:12px;padding:12px 8px;
    text-align:center;transition:all .25s;
  }
  .member:hover,.member:active{
    background:rgba(255,230,0,0.07);
    border-color:var(--stroke-2);
    transform:translateY(-2px);
  }
  .avatar{
    width:42px;height:42px;border-radius:50%;
    background:linear-gradient(135deg,var(--yellow),var(--gold));
    color:#050506;
    display:flex;align-items:center;justify-content:center;
    font-family:var(--f-display);font-weight:700;font-size:0.72rem;
    margin:0 auto 10px;
    box-shadow:0 0 14px rgba(255,230,0,0.4);
  }
  .member-name{
    font-family:var(--f-display);font-weight:600;
    font-size:0.68rem;color:var(--text);margin-bottom:3px;
  }
  .member-role{
    font-size:0.5rem;color:var(--muted);
    letter-spacing:1px;text-transform:uppercase;font-weight:500;
  }
  .footer{
    text-align:center;color:var(--faint);font-size:0.55rem;
    letter-spacing:2px;line-height:1.7;margin-top:18px;
    text-transform:uppercase;font-weight:500;
  }

  /* Toast */
  #toast{
    position:fixed;bottom:30px;left:50%;
    transform:translateX(-50%) translateY(120px);
    background:rgba(8,8,10,0.96);
    border:1px solid var(--yellow);color:var(--yellow);
    padding:12px 24px;border-radius:8px;
    font-family:var(--f-display);font-weight:700;
    font-size:0.65rem;letter-spacing:2px;text-transform:uppercase;
    box-shadow:0 0 28px rgba(255,230,0,0.32);
    transition:all .3s ease;opacity:0;pointer-events:none;
    z-index:100;backdrop-filter:blur(10px);
    white-space:nowrap;
  }
  #toast.show{transform:translateX(-50%) translateY(0);opacity:1;}

  @media (prefers-reduced-motion:reduce){
    *,*::before,*::after{
      animation-duration:0.01ms !important;
      transition-duration:0.01ms !important;
    }
    .hero-img{transform:none !important;}
  }
</style>
</head>
<body>

<div class="topbar">
  <div class="brand">
    <span class="brand-dot" id="statusDot"></span>
    CYBER CAR PRO
  </div>
  <button class="lang-btn" type="button" onclick="toggleLang()">AR / EN</button>
</div>

<div class="hero">
  <img class="hero-img" src="/image" alt="Cyber Car">
  <div class="hero-text">
    <span class="hero-tag" id="lblTag">AI ROBOTICS PLATFORM</span>
    <h1 id="lblHeroTitle">CYBER CAR PRO</h1>
    <p id="lblHeroSub">AUTONOMOUS EMBEDDED INTELLIGENCE</p>
  </div>
</div>

<div class="container">

  <div class="card reveal">
    <div class="card-label"><span id="lblTelemetry">TELEMETRY</span></div>
    <div class="stats">
      <div class="stat">
        <div class="stat-label" id="lblDist">Distance</div>
        <div class="stat-val" id="dist">--<small>cm</small></div>
      </div>
      <div class="stat">
        <div class="stat-label" id="lblBall">Ball</div>
        <div class="stat-val gold" id="ball">LOCKED</div>
      </div>
      <div class="stat">
        <div class="stat-label" id="lblBat">Battery</div>
        <div class="stat-val battery" id="bat">--<small>%</small></div>
        <div class="bat-seg" id="batSeg">
          <div class="seg"></div><div class="seg"></div>
          <div class="seg"></div><div class="seg"></div>
        </div>
      </div>
    </div>
  </div>

  <div class="card reveal">
    <div class="card-label"><span id="lblMode">CONTROL MODE</span></div>
    <div class="modes">
      <button class="mode-btn active" type="button" data-mode="manual" onclick="setMode('manual')" id="mMan">MANUAL</button>
      <button class="mode-btn" type="button" data-mode="auto" onclick="setMode('auto')" id="mAuto">AUTO</button>
      <button class="mode-btn" type="button" data-mode="rl" onclick="setMode('rl')" id="mRL">RL-AI</button>
    </div>
    <div class="ai-panel" id="aiPanel">
      <div class="ai-grid">
        <div class="ai-cell">
          <div class="k">Updates</div>
          <div class="v" id="rlUpdates">0</div>
        </div>
        <div class="ai-cell">
          <div class="k">Episodes</div>
          <div class="v" id="rlEpisodes">0</div>
        </div>
        <div class="ai-cell">
          <div class="k">Best Reward</div>
          <div class="v" id="rlBest">--</div>
        </div>
      </div>
      <div class="eps-bar">
        <div class="eps-bar-label">
          <span>Exploration</span>
          <span id="rlEps">50%</span>
        </div>
        <div class="eps-bar-track">
          <div class="eps-bar-fill" id="rlEpsFill"></div>
        </div>
      </div>
    </div>
  </div>

  <div class="card reveal">
    <div class="slider-head">
      <span id="lblSpeed">SPEED LIMIT</span>
      <span id="speedVal">80%</span>
    </div>
    <input type="range" min="30" max="100" value="80" id="speedSlider" oninput="setSpeed(this.value)">
    <div class="joy-wrap">
      <div id="joystick">
        <div class="crosshair"></div>
        <div class="dir-arrow dir-up" id="arrUp"></div>
        <div class="dir-arrow dir-down" id="arrDown"></div>
        <div class="dir-arrow dir-left" id="arrLeft"></div>
        <div class="dir-arrow dir-right" id="arrRight"></div>
        <div id="stick"></div>
      </div>
    </div>
  </div>

  <div class="card reveal">
    <div class="card-label"><span id="lblActions">QUICK ACTIONS</span></div>
    <div class="actions">
      <button class="action-btn warn" type="button" onclick="ballAction('release')" id="bRel">RELEASE BALL</button>
      <button class="action-btn dim" type="button" onclick="ballAction('lock')" id="bLock">LOCK BALL</button>
      <button class="action-btn" type="button" onclick="api('/api/horn').then(okToast).catch(showOffline)" id="bHorn">HORN</button>
      <button class="action-btn" type="button" onclick="api('/api/dance').then(okToast).catch(showOffline)" id="bDance">DANCE</button>
      <button class="action-btn danger" type="button" onclick="emergencyBrake()" id="bBrake">EMERGENCY BRAKE</button>
    </div>
  </div>

  <div class="card reveal team">
    <div class="team-inner">
      <span class="team-badge" id="lblBadge">AL-IRAQIA UNIVERSITY</span>
      <h2 id="lblTeamTitle">AI &amp; ROBOTICS ENGINEERING</h2>
      <p class="team-sub" id="lblTeamSub">
        Supervised by the <b>SPARK Team</b> — Department of Artificial Intelligence &amp; Robotics
      </p>
      <div class="team-grid">
        <div class="member">
          <div class="avatar">HN</div>
          <div class="member-name" id="n1">Haider Nadhir</div>
          <div class="member-role" id="r1">Team Lead</div>
        </div>
        <div class="member">
          <div class="avatar">AA</div>
          <div class="member-name" id="n2">Ahmed Amjad</div>
          <div class="member-role" id="r2">AI Engineer</div>
        </div>
        <div class="member">
          <div class="avatar">BF</div>
          <div class="member-name" id="n3">Baraa Fadhel</div>
          <div class="member-role" id="r3">Robotics Engineer</div>
        </div>
        <div class="member">
          <div class="avatar">AS</div>
          <div class="member-name" id="n4">Ayat Saeed</div>
          <div class="member-role" id="r4">Systems Engineer</div>
        </div>
      </div>
      <div class="footer" id="lblFooter">
        Designed &amp; Engineered by AI &amp; Robotics Engineers<br>
        &copy; 2026 CYBER CAR PRO — Under SPARK Team Supervision
      </div>
    </div>
  </div>

</div>

<div id="toast">COMMAND SENT</div>

<script>
  let currentLang = 'en';
  const t = {
    en:{
      lblTag:"AI ROBOTICS PLATFORM",lblHeroTitle:"CYBER CAR PRO",
      lblHeroSub:"AUTONOMOUS EMBEDDED INTELLIGENCE",
      lblTelemetry:"TELEMETRY",lblDist:"Distance",lblBall:"Ball",lblBat:"Battery",
      lblMode:"CONTROL MODE",mMan:"MANUAL",mAuto:"AUTO",mRL:"RL-AI",
      lblSpeed:"SPEED LIMIT",lblActions:"QUICK ACTIONS",
      bRel:"RELEASE BALL",bLock:"LOCK BALL",bHorn:"HORN",bDance:"DANCE",
      bBrake:"EMERGENCY BRAKE",
      lblBadge:"AL-IRAQIA UNIVERSITY",lblTeamTitle:"AI & ROBOTICS ENGINEERING",
      lblTeamSub:"Supervised by the <b>SPARK Team</b> — Department of Artificial Intelligence & Robotics",
      n1:"Haider Nadhir",r1:"Team Lead",n2:"Ahmed Amjad",r2:"AI Engineer",
      n3:"Baraa Fadhel",r3:"Robotics Engineer",n4:"Ayat Saeed",r4:"Systems Engineer",
      lblFooter:"Designed & Engineered by AI & Robotics Engineers<br>&copy; 2026 CYBER CAR PRO — Under SPARK Team Supervision",
      msgReleased:"BALL RELEASED",msgLocked:"BALL LOCKED",msgSafety:"SAFETY BRAKE ACTIVE",
      msgOff:"OFFLINE",msgError:"CONNECTION ERROR",msgOk:"COMMAND SENT",
      modeSwitch:"MODE SWITCHED"
    },
    ar:{
      lblTag:"منصة روبوتات ذكية",lblHeroTitle:"سايبر كار برو",
      lblHeroSub:"ذكاء مدمج مستقل",
      lblTelemetry:"القياسات",lblDist:"المسافة",lblBall:"حالة الكرة",lblBat:"البطارية",
      lblMode:"وضع التحكم",mMan:"يدوي",mAuto:"ذاتي",mRL:"تعلّم AI",
      lblSpeed:"حد السرعة",lblActions:"أوامر سريعة",
      bRel:"إسقاط الكرة",bLock:"قفل الكرة",bHorn:"تزمير",bDance:"استعراض",
      bBrake:"إيقاف طارئ",
      lblBadge:"الجامعة العراقية",lblTeamTitle:"هندسة الذكاء الاصطناعي والروبوتات",
      lblTeamSub:"بإشراف <b>فريق سبارك</b> — قسم الذكاء الاصطناعي وهندسة الروبوتات",
      n1:"حيدر نذير",r1:"قائد الفريق",n2:"أحمد أمجد",r2:"مهندس ذكاء اصطناعي",
      n3:"براء فاضل",r3:"مهندس روبوتات",n4:"آيات سعيد",r4:"مهندس أنظمة",
      lblFooter:"تصميم وهندسة قسم الذكاء الاصطناعي والروبوتات<br>&copy; 2026 سايبر كار برو — بإشراف فريق سبارك",
      msgReleased:"تم إسقاط الكرة",msgLocked:"تم قفل الكرة",msgSafety:"تم تفعيل البريك الآمن",
      msgOff:"مفصول",msgError:"تعذر الاتصال",msgOk:"تم إرسال الأمر",
      modeSwitch:"تم تبديل الوضع"
    }
  };

  function applyLang(){
    const root = document.getElementById('htmlRoot');
    root.dir = currentLang === 'ar' ? 'rtl' : 'ltr';
    root.lang = currentLang;
    const dict = t[currentLang];
    for (const k in dict){ const el = document.getElementById(k); if (el) el.innerHTML = dict[k]; }
  }
  function toggleLang(){ currentLang = currentLang==='en'?'ar':'en'; applyLang(); }
  applyLang();

  let toastTimer = 0;
  function showToast(msg){
    const el = document.getElementById('toast');
    clearTimeout(toastTimer);
    el.textContent = msg;
    el.classList.add('show');
    toastTimer = setTimeout(()=>el.classList.remove('show'), 2000);
  }
  function okToast(){ showToast(t[currentLang].msgOk); }
  function showOffline(){ showToast(t[currentLang].msgError); }
  function api(path){
    return fetch(path,{credentials:'same-origin',cache:'no-store'})
      .then(r => { if(!r.ok) throw new Error('request_failed'); return r.json(); });
  }

  let speedTimer = 0;
  function setSpeed(v){
    document.getElementById('speedVal').textContent = v + '%';
    clearTimeout(speedTimer);
    speedTimer = setTimeout(()=>{
      api('/api/speed?v='+encodeURIComponent(v)).catch(showOffline);
    }, 125);
  }

  function setActiveMode(mode){
    document.querySelectorAll('.mode-btn').forEach(b=>{
      b.classList.toggle('active', b.dataset.mode === mode);
    });
    const panel = document.getElementById('aiPanel');
    if (panel) panel.classList.toggle('show', mode === 'rl');
  }
  function setMode(mode){
    api('/api/mode?m='+encodeURIComponent(mode))
      .then(()=>{ setActiveMode(mode); showToast(t[currentLang].modeSwitch); })
      .catch(showOffline);
  }
  function ballAction(a){
    api('/api/ball?a='+encodeURIComponent(a))
      .then(()=>showToast(a==='release'?t[currentLang].msgReleased:t[currentLang].msgLocked))
      .catch(showOffline);
  }
  function emergencyBrake(){
    stopJoystick();
    api('/api/brake').then(()=>showToast(t[currentLang].msgSafety)).catch(showOffline);
  }

  /* Joystick */
  const joystick = document.getElementById('joystick');
  const stick = document.getElementById('stick');
  const arrUp   = document.getElementById('arrUp');
  const arrDown = document.getElementById('arrDown');
  const arrLeft = document.getElementById('arrLeft');
  const arrRight= document.getElementById('arrRight');
  const MAX_R = 70, DEAD = 18;
  let dragging=false, lastCmd='S', lastSent=0;

  function setStick(x,y){ stick.style.transform = `translate(${x}px,${y}px)`; }
  function clearArrows(){
    arrUp.classList.remove('active');
    arrDown.classList.remove('active');
    arrLeft.classList.remove('active');
    arrRight.classList.remove('active');
  }
  function sendJoy(cmd,x,y){
    lastCmd=cmd; lastSent=Date.now();
    api('/api/joy?x='+x+'&y='+y).catch(()=>{});
  }
  function stopJoystick(){
    if(!dragging && lastCmd==='S') return;
    dragging=false; setStick(0,0); clearArrows(); sendJoy('S',0,0);
  }
  function onMove(e){
    if(!dragging) return;
    const r = joystick.getBoundingClientRect();
    let dx = e.clientX - (r.left + r.width/2);
    let dy = e.clientY - (r.top + r.height/2);
    const d = Math.hypot(dx,dy);
    if(d > MAX_R){ const a = Math.atan2(dy,dx); dx=Math.cos(a)*MAX_R; dy=Math.sin(a)*MAX_R; }
    setStick(dx,dy);
    let cmd='S';
    clearArrows();
    if(d > DEAD){
      if(Math.abs(dx) > Math.abs(dy)){
        if(dx > 0){ cmd='R'; arrRight.classList.add('active'); }
        else      { cmd='L'; arrLeft.classList.add('active'); }
      } else {
        if(dy > 0){ cmd='B'; arrDown.classList.add('active'); }
        else      { cmd='F'; arrUp.classList.add('active'); }
      }
    }
    const now = Date.now();
    if(cmd !== lastCmd || now - lastSent > 100){
      const px = Math.round((dx/MAX_R)*100);
      const py = Math.round(-(dy/MAX_R)*100);
      sendJoy(cmd,px,py);
    }
  }
  joystick.addEventListener('pointerdown', e => {
    dragging=true; joystick.setPointerCapture(e.pointerId); onMove(e);
  });
  joystick.addEventListener('pointermove', onMove);
  joystick.addEventListener('pointerup', stopJoystick);
  joystick.addEventListener('pointercancel', stopJoystick);
  joystick.addEventListener('lostpointercapture', stopJoystick);
  window.addEventListener('blur', stopJoystick);
  document.addEventListener('visibilitychange', ()=>{ if(document.hidden) stopJoystick(); });

  /* Parallax */
  const body = document.body;
  const heroImg = document.querySelector('.hero-img');
  const hero = document.querySelector('.hero');
  let ticking = false, lastY = 0;
  function onScroll(){
    lastY = window.scrollY || window.pageYOffset || 0;
    if(!ticking){ requestAnimationFrame(applyParallax); ticking=true; }
  }
  function applyParallax(){
    body.style.setProperty('--bgScroll', lastY);
    if(heroImg) heroImg.style.setProperty('--heroScroll', lastY);
    if(hero) hero.style.setProperty('--heroLift', Math.min(lastY*0.05, 18));
    ticking = false;
  }
  window.addEventListener('scroll', onScroll, {passive:true});
  onScroll();

  /* Reveal */
  const revealObserver = new IntersectionObserver(entries=>{
    entries.forEach(entry=>{
      if(entry.isIntersecting){
        const idx = Array.from(document.querySelectorAll('.card')).indexOf(entry.target);
        setTimeout(()=>entry.target.classList.add('revealed'), (idx%6)*60);
        revealObserver.unobserve(entry.target);
      }
    });
  }, {threshold:0.12});
  document.querySelectorAll('.card.reveal').forEach(el=>revealObserver.observe(el));

  /* 3D tilt */
  document.querySelectorAll('.card').forEach(card=>{
    card.addEventListener('pointermove', e=>{
      if(e.pointerType !== 'mouse') return;
      const r = card.getBoundingClientRect();
      const x = (e.clientX - r.left) / r.width - 0.5;
      const y = (e.clientY - r.top) / r.height - 0.5;
      card.classList.add('tilting');
      card.style.transform = `perspective(900px) rotateX(${y*3}deg) rotateY(${x*3}deg)`;
    });
    card.addEventListener('pointerleave', ()=>{
      card.classList.remove('tilting');
      card.style.transform = '';
    });
  });

  /* State polling */
  const statusDot = document.getElementById('statusDot');
  let stateBusy = false, previousSafety = false, lastOk = 0;

  function fmtDistance(cm){
    if(cm === null || cm === undefined) return '--<small>cm</small>';
    if(cm >= 100){
      const m = (cm/100).toFixed(2);
      return m + '<small>m</small>';
    }
    return cm + '<small>cm</small>';
  }

  function updateBatterySeg(pct){
    const segs = document.querySelectorAll('#batSeg .seg');
    const filled = Math.ceil(pct / 25);
    let cls = 'on';
    if(pct < 20)      cls = 'low';
    else if(pct < 50) cls = 'mid';
    segs.forEach((s, i) => {
      s.classList.remove('on','mid','low');
      if(i < filled) s.classList.add(cls);
    });
  }

  function refreshState(){
    if(stateBusy) return;
    stateBusy = true;
    api('/api/state').then(d=>{
      lastOk = Date.now();
      statusDot.classList.remove('offline');
      statusDot.classList.add('online');

      const distEl = document.getElementById('dist');
      if(!d.sensorOk){
        distEl.innerHTML = 'ERR';
        distEl.style.color = 'var(--danger)';
      } else {
        distEl.innerHTML = fmtDistance(d.dist);
        distEl.style.color = d.safety ? 'var(--danger)' : 'var(--yellow)';
      }

      const batEl = document.getElementById('bat');
      batEl.innerHTML = d.bat + '<small>%</small>';
      batEl.style.color = d.bat < 20 ? 'var(--danger)' : 'var(--ok)';
      updateBatterySeg(d.bat);

      const ballEl = document.getElementById('ball');
      ballEl.textContent = d.ball === 'locked'
        ? (currentLang === 'ar' ? 'مقفل' : 'LOCKED')
        : (currentLang === 'ar' ? 'مفتوح' : 'RELEASED');

      setActiveMode(d.mode);

      /* RL panel */
      if (d.rl){
        const u = document.getElementById('rlUpdates');
        const e = document.getElementById('rlEpisodes');
        const b = document.getElementById('rlBest');
        const ep = document.getElementById('rlEps');
        const epf = document.getElementById('rlEpsFill');
        if(u) u.textContent = d.rl.updates;
        if(e) e.textContent = d.rl.episodes;
        if(b) b.textContent = d.rl.best > -10000 ? d.rl.best : '--';
        const epsPct = Math.round(d.rl.epsilon / 10);
        if(ep) ep.textContent = epsPct + '%';
        if(epf) epf.style.width = epsPct + '%';
      }

      if(d.safety && !previousSafety) showToast(t[currentLang].msgSafety);
      previousSafety = d.safety;
    }).catch(()=>{
      const distEl = document.getElementById('dist');
      distEl.textContent = t[currentLang].msgOff;
      distEl.style.color = 'var(--danger)';
      statusDot.classList.remove('online');
      statusDot.classList.add('offline');
    }).finally(()=>{ stateBusy = false; });
  }
  refreshState();
  setInterval(refreshState, 450);

  /* Fallback offline check */
  setInterval(()=>{
    if (Date.now() - lastOk > 5000 && lastOk > 0){
      statusDot.classList.remove('online');
      statusDot.classList.add('offline');
    }
  }, 2000);
</script>
</body>
</html>
)HTML";