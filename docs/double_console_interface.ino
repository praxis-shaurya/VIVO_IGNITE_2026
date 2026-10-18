/*
  AVROD | ESP8266 safety dashboard + RC522 + servo lock + buzzer
  Board: NodeMCU ESP8266
  Libraries: MFRC522 by Miguel Balboa, ESP8266 board package

  Wiring:
    FC-22 AO -> A0 (verify your NodeMCU board's A0 input range)
    RC522 SDA/SS -> D8, SCK -> D5, MOSI -> D7, MISO -> D6, RST -> D3
    RC522 3.3V -> 3V3, GND -> GND
    Servo signal -> D4 (match your latest diagnostic wiring); servo power should use a suitable supply, with common GND
    Active buzzer + -> D2, buzzer - -> GND
    Winch motor driver control input -> D1 (GPIO5); motor must use a driver and separate suitable supply

  Dashboard opens directly. Simulation mode is software-only; the real FC-22 raw ADC remains separately visible. Simulated values are not calibrated measurements or proof of a safe atmosphere.
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Servo.h>

#define GAS_PIN A0
#define RFID_SS_PIN D8
#define RFID_RST_PIN D3
#define SERVO_PIN D4
#define BUZZER_PIN D2
#define WINCH_PIN D1  // GPIO5: control signal to a motor driver, NOT the motor itself
const bool BUZZER_ACTIVE_LOW = false; // Change to true only if your buzzer module is active-low

const char* AP_SSID = "AVROD_Safety";
const char* AP_PASSWORD = "avrod1234"; // At least 8 characters

const int GAS_WARNING = 450;
const int GAS_DANGER = 500;
// Explicit software-only atmosphere simulation for exhibition demos.
// Simulated values are NOT sensor measurements or safety certification.
bool simulationMode = false;
int gasReading = 0;
int simH2S = 0;       // ppm (simulated)
int simCO = 0;        // ppm (simulated)
int simLEL = 0;       // % LEL (simulated)
float simO2 = 20.9;   // % volume (simulated)
int simNH3 = 0;       // ppm (simulated)

bool simulatedHazard() {
  return simH2S >= 10 || simCO >= 35 || simLEL >= 10 ||
         simO2 < 19.5 || simO2 > 23.5 || simNH3 >= 25;
}
bool activeHazard() {
  return simulationMode ? simulatedHazard() : (gasReading >= GAS_DANGER);
}
const int SERVO_LOCK_ANGLE = 135;   // Perpendicular = locked at startup
const int SERVO_UNLOCK_ANGLE = 0;  // Authorized card moves servo away from perpendicular

// Replace this with the UID of your authorized demo card if it differs.
const byte AUTH_UID[] = {0x23, 0x25, 0x0E, 0xF8};
const byte AUTH_UID_LENGTH = sizeof(AUTH_UID);

ESP8266WebServer server(80);
MFRC522 rfid(RFID_SS_PIN, RFID_RST_PIN);
Servo lockServo;

bool isUnlocked = false;
bool lastCardAccepted = false;
bool cardEverRead = false;
bool buzzerTestActive = false;
unsigned long buzzerTestUntil = 0;
unsigned long lastSensorRead = 0;
unsigned long lastLogAt = 0;
String lastCardUID = "—";
String lastEvent = "System started";
String eventLog[12];
int eventCount = 0;
int eventNext = 0;

// Dead-man demonstration state. This is a simulated movement input, not a physical motion sensor.
bool workerInside = false;
bool winchRunning = false;
bool deadmanMonitoring = false;
bool deadmanNoMovement = false;
bool deadmanAlarm = false;
unsigned long noMovementSince = 0;
const unsigned long DEADMAN_TIMEOUT_MS = 8000;

const char PAGE[] PROGMEM =
  "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
  "<title>AVROD | Safety Control · Atmospheric Simulator</title>\n"
  "<style>\n"
  ":root{color-scheme:dark;--bg:#090d14;--panel:#101722;--panel2:#0d131d;--line:#222d3d;--text:#e8eef7;--muted:#8997ab;--cyan:#63d8e8;--green:#50d890;--amber:#f2bd58;--red:#ff6474;--blue:#7b9cff}\n"
  "*{box-sizing:border-box}body{margin:0;background:radial-gradient(ellipse at 15% 0%,#142235 0,transparent 40%),var(--bg);font:14px/1.45 Inter,ui-sans-serif,system-ui,-apple-system,Segoe UI,sans-serif;color:var(--text)}button,input{font:inherit}button{cursor:pointer;border:1px solid var(--line);border-radius:10px;padding:10px 13px;color:var(--text);background:#182231;transition:.15s}button:hover{border-color:#53647b;transform:translateY(-1px)}button:disabled{opacity:.5;cursor:not-allowed;transform:none}.primary{background:var(--cyan);color:#061219;border-color:var(--cyan);font-weight:750}.dangerBtn{border-color:#63313a;background:#2a151d;color:#ffb6bd}.quiet{background:transparent}.hidden{display:none!important}.publicPage #simulatorCard{display:none!important}.publicPage .adminOnly{display:none!important}.publicPage #pageSwitch{font-size:11px;padding:7px 10px}.adminPage #deadmanCard{border-color:#34495c}.loginWrap{min-height:100vh;display:grid;place-items:center;padding:22px}.loginCard{width:min(420px,100%);padding:30px;border:1px solid var(--line);border-radius:20px;background:linear-gradient(145deg,#131e2b,#0c121b);box-shadow:0 24px 90px #0007}.brandMark{display:inline-grid;place-items:center;width:44px;height:44px;border-radius:13px;background:#163642;border:1px solid #286172;color:var(--cyan);font-weight:900;letter-spacing:-1px}.eyebrow{font-size:11px;letter-spacing:2px;text-transform:uppercase;color:var(--cyan);font-weight:800}.loginCard h1{font-size:26px;letter-spacing:-.7px;margin:17px 0 5px}.sub{color:var(--muted)}label{display:block;color:#b9c5d6;font-size:12px;margin:18px 0 7px}input{width:100%;border:1px solid var(--line);border-radius:10px;background:#080e16;color:var(--text);padding:12px;outline:none}input:focus{border-color:var(--cyan)}\n"
  "/* Keep browser autofill from painting the dark login fields yellow. */\n"
  "input:-webkit-autofill,input:-webkit-autofill:hover,input:-webkit-autofill:focus,input:-webkit-autofill:active{-webkit-text-fill-color:var(--text);-webkit-box-shadow:0 0 0 1000px #080e16 inset;box-shadow:0 0 0 1000px #080e16 inset;caret-color:var(--text);transition:background-color 99999s ease-in-out 0s}\n"
  ".loginCard .primary{width:100%;margin-top:22px;padding:12px}.loginError{color:var(--red);min-height:20px;margin-top:10px;font-size:12px}.loginFoot{border-top:1px solid var(--line);margin-top:22px;padding-top:16px;color:var(--muted);font-size:11px}\n"
  ".app{max-width:1500px;margin:auto;padding:22px}.topbar{display:flex;align-items:center;justify-content:space-between;gap:14px;margin-bottom:24px}.brand{display:flex;align-items:center;gap:12px}.brand h2{font-size:17px;margin:0;letter-spacing:.2px}.brand small{display:block;color:var(--muted);font-size:11px;margin-top:2px}.topActions{display:flex;align-items:center;gap:9px;flex-wrap:wrap}.livePill,.pill{display:inline-flex;align-items:center;gap:7px;padding:7px 10px;border:1px solid var(--line);border-radius:99px;font-size:11px;color:var(--muted);white-space:nowrap}.dot{width:7px;height:7px;border-radius:50%;background:var(--green);box-shadow:0 0 12px #50d89066}.pageTitle{display:flex;justify-content:space-between;align-items:end;gap:15px;margin-bottom:17px}.pageTitle h1{margin:0;font-size:26px;letter-spacing:-.8px}.pageTitle p{margin:5px 0 0;color:var(--muted)}.grid{display:grid;grid-template-columns:repeat(12,minmax(0,1fr));gap:14px}.card{background:linear-gradient(145deg,#111a26,#0d141e);border:1px solid var(--line);border-radius:15px;padding:17px;min-width:0}.metric{grid-column:span 3;min-height:140px}.cardHead{display:flex;justify-content:space-between;align-items:center;gap:10px;color:var(--muted);font-size:12px}.metricValue{font-size:34px;font-weight:750;letter-spacing:-1.4px;margin:14px 0 5px;font-variant-numeric:tabular-nums}.metricFoot{color:var(--muted);font-size:11px}.statusTag{font-size:10px;letter-spacing:.7px;text-transform:uppercase;padding:5px 8px;border-radius:7px;border:1px solid var(--line);color:var(--muted)}.good{color:var(--green)!important;border-color:#285a46!important;background:#10251e!important}.warn{color:var(--amber)!important;border-color:#634e2c!important;background:#261f13!important}.bad{color:var(--red)!important;border-color:#63313a!important;background:#29161d!important}.graphCard{grid-column:span 8}.sideCard{grid-column:span 4}.wide{grid-column:span 12}.half{grid-column:span 6}.sectionTitle{font-size:14px;font-weight:700;margin:0}.sectionDesc{font-size:11px;color:var(--muted);margin-top:4px}.chartMeta{display:flex;align-items:center;justify-content:space-between;gap:10px;flex-wrap:wrap}.chartLegend{display:flex;gap:14px;color:var(--muted);font-size:11px}.legendLine{display:inline-block;width:17px;height:2px;background:var(--cyan);vertical-align:middle;margin-right:6px}.chartScroll{overflow-x:auto;overflow-y:hidden;margin-top:16px;border:1px solid #1b2634;border-radius:10px;background:#0a1018;scrollbar-color:#34465b #0a1018;scrollbar-width:thin}.chartInner{height:245px;min-width:900px;position:relative}.chartInner canvas{width:100%;height:100%;display:block}.chartHint{display:flex;justify-content:space-between;color:var(--muted);font-size:10px;margin-top:9px}.progress{height:5px;background:#202a38;border-radius:9px;overflow:hidden;margin-top:13px}.progress>span{display:block;height:100%;width:0;background:var(--cyan);transition:width .2s}.statusHero{display:flex;align-items:center;gap:13px;margin:19px 0}.statusIcon{width:48px;height:48px;border-radius:14px;display:grid;place-items:center;background:#14251f;border:1px solid #285a46;font-size:22px}.statusHero h3{margin:0 0 3px;font-size:18px}.statusHero p{margin:0;color:var(--muted);font-size:11px}.kv{display:flex;justify-content:space-between;gap:12px;padding:10px 0;border-top:1px solid #1d2836;font-size:12px}.kv span:first-child{color:var(--muted)}.kv strong{font-weight:650;text-align:right;overflow-wrap:anywhere}.actions{display:flex;gap:8px;flex-wrap:wrap;margin-top:14px}.actions button,.actions .actionButton{flex:1;min-width:105px}.actionButton{display:block;text-align:center;text-decoration:none;border:1px solid var(--line);border-radius:10px;padding:10px 13px;color:var(--text);background:#182231;touch-action:manipulation;-webkit-tap-highlight-color:transparent}.actionButton:active{filter:brightness(1.2)}.actionButton.primary{background:var(--cyan);color:#061219;border-color:var(--cyan);font-weight:750}.actionButton.dangerBtn{border-color:#63313a;background:#2a151d;color:#ffb6bd}.miniStat{display:flex;justify-content:space-between;gap:12px;padding:12px 0;border-bottom:1px solid #1d2836}.miniStat:last-child{border-bottom:0}.miniStat span{color:var(--muted);font-size:12px}.miniStat strong{font-size:12px;text-align:right}.logList{display:flex;flex-direction:column;gap:0;margin-top:12px;max-height:245px;overflow-y:auto}.logItem{display:grid;grid-template-columns:66px 8px 1fr;gap:10px;align-items:start;padding:11px 0;border-bottom:1px solid #1d2836;font-size:12px}.logTime{font-size:10px;color:var(--muted);font-variant-numeric:tabular-nums}.logDot{width:7px;height:7px;border-radius:50%;background:var(--cyan);margin-top:4px}.logMsg{color:#c5d0df}.simGrid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px;margin-top:14px}.simControl{padding:12px;border:1px solid var(--line);border-radius:11px;background:#0b121c;min-width:0}.simControl label{display:flex;justify-content:space-between;gap:10px;align-items:center;margin:0 0 10px;color:#b9c5d6;font-size:12px}.simControl label strong{color:var(--cyan);white-space:nowrap}.simControl input[type=range]{width:100%;padding:0;accent-color:var(--cyan);background:transparent}.simScale{display:flex;justify-content:space-between;gap:6px;color:var(--muted);font-size:10px}.simResult{display:flex;justify-content:space-between;align-items:center;gap:15px;margin-top:14px;padding:14px;border:1px solid var(--line);border-radius:11px;background:#0b121c}.simResult strong{display:block;font-size:16px;margin-top:4px}.simResult #simOverallDetail{color:var(--muted);font-size:12px;text-align:right;max-width:55%}.notice{padding:11px 12px;border:1px solid #4d3e26;border-radius:10px;background:#211b12;color:#dfc48e;font-size:11px;margin-top:14px}.footer{display:flex;justify-content:space-between;gap:12px;flex-wrap:wrap;color:#637187;font-size:10px;padding:18px 2px 4px}.toast{position:fixed;right:18px;bottom:18px;padding:12px 15px;border:1px solid #31465a;border-radius:12px;background:#142131;box-shadow:0 12px 40px #0008;max-width:320px;z-index:9}.icon{color:var(--cyan);font-size:16px}.muted{color:var(--muted)}\n"
  "@media(max-width:900px){.metric{grid-column:span 6}.graphCard,.sideCard{grid-column:span 12}.half{grid-column:span 12}.app{padding:15px}.pageTitle h1{font-size:22px}}@media(max-width:520px){.simGrid{grid-template-columns:1fr}.simResult{align-items:flex-start;flex-direction:column}.simResult #simOverallDetail{text-align:left;max-width:100%}.metric{grid-column:span 12;min-height:115px}.metricValue{font-size:30px}.topbar{align-items:flex-start}.topActions{justify-content:flex-end}.pageTitle{align-items:flex-start;flex-direction:column}.card{padding:14px}.loginCard{padding:23px}}\n"
  "</style></head><body>\n"
  "<div id=\"dashboard\" class=\"app\"><header class=\"topbar\"><div class=\"brand\"><div class=\"brandMark\">A.</div><div><h2>AVROD <span style=\"color:var(--muted);font-weight:450\">/ SAFETY CONTROL</span></h2><small>Municipal operations monitoring</small></div></div><div class=\"topActions\"><span class=\"livePill\"><i class=\"dot\"></i> LIVE DEVICE LINK</span><span id=\"clock\" class=\"pill\">--:--:--</span><span class=\"pill\" id=\"pageModePill\">PUBLIC DISPLAY</span><a class=\"actionButton\" id=\"pageSwitch\" href=\"/admin\">Admin console ↗</a></div></header>\n"
  "<div class=\"pageTitle\"><div><div class=\"eyebrow\">OPERATIONS OVERVIEW</div><h1>Safety dashboard</h1><p>Live sensor readings, PPE verification and access control.</p></div><span id=\"connectionPill\" class=\"statusTag\">SCRIPT STARTING…</span></div>\n"
  "<div class=\"grid\">\n"
  "<section class=\"card metric\"><div class=\"cardHead\"><span>GAS SENSOR · RAW ADC</span><span class=\"icon\">⌁</span></div><div class=\"metricValue\" id=\"gasValue\">—</div><div class=\"metricFoot\" id=\"gasSub\">Waiting for sensor data</div><div class=\"progress\"><span id=\"gasBar\"></span></div></section>\n"
  "<section class=\"card metric\"><div class=\"cardHead\"><span>ATMOSPHERE STATUS</span><span class=\"icon\">◈</span></div><div class=\"metricValue\" style=\"font-size:25px;margin-top:20px\" id=\"gasState\">CHECKING</div><div class=\"metricFoot\" id=\"thresholdText\">Warning ≥ 450 · Danger ≥ 500</div></section>\n"
  "<section class=\"card metric\"><div class=\"cardHead\"><span>ACCESS INTERLOCK</span><span class=\"icon\">▣</span></div><div class=\"metricValue\" style=\"font-size:25px;margin-top:20px\" id=\"lockState\">LOCKED</div><div class=\"metricFoot\" id=\"lockSub\">Servo lock engaged</div></section>\n"
  "<section class=\"card metric\"><div class=\"cardHead\"><span>PPE KIT DETECTION</span><span class=\"icon\">◉</span></div><div class=\"metricValue\" style=\"font-size:23px;margin-top:20px\" id=\"ppeState\">AWAITING CARD</div><div class=\"metricFoot\" id=\"cardSub\">Present registered RFID card</div></section>\n"
  "<section class=\"card wide\" id=\"simulatorCard\">\n"
  "<div class=\"cardHead\"><div><h3 class=\"sectionTitle\">Atmospheric hazard simulator</h3><div class=\"sectionDesc\">Software-generated scenario inputs · not live gas measurements</div></div><span class=\"statusTag warn\" id=\"simModeBadge\">SIMULATION OFF</span></div>\n"
  "<div class=\"notice\" style=\"margin-top:12px\">Demo only: all values below are simulated. Thresholds are illustrative trigger points for demonstrating control logic, not calibrated exposure limits or entry-clearance criteria.</div>\n"
  "<div class=\"actions\" style=\"margin:12px 0\"><button class=\"primary\" id=\"simEnableBtn\" type=\"button\">Enable simulation</button><button id=\"simDisableBtn\" type=\"button\" class=\"quiet\">Return to live sensor</button><button id=\"simSafeBtn\" type=\"button\" class=\"quiet\">Load safe scenario</button><button id=\"simHazardBtn\" type=\"button\" class=\"dangerBtn\">Load hazard scenario</button></div>\n"
  "<div class=\"actions adminOnly\" style=\"margin:12px 0\"><button id=\"deadmanStartBtn\" type=\"button\" class=\"primary\">Start post-entry monitoring</button><button id=\"deadmanNoMoveBtn\" type=\"button\">Simulate no movement</button><button id=\"deadmanMoveBtn\" type=\"button\">Movement detected</button><button id=\"deadmanResetBtn\" type=\"button\" class=\"dangerBtn\">Acknowledge / reset alarm</button></div>\n"
  "<div class=\"simGrid\">\n"
  "<div class=\"simControl\"><label for=\"simH2S\">Hydrogen sulfide · H₂S <strong><span id=\"simH2SValue\">0</span> ppm</strong></label><input id=\"simH2S\" type=\"range\" min=\"0\" max=\"100\" step=\"1\" value=\"0\"><div class=\"simScale\"><span>0</span><span>Illustrative alert: 10 ppm</span><span>100</span></div></div>\n"
  "<div class=\"simControl\"><label for=\"simCO\">Carbon monoxide · CO <strong><span id=\"simCOValue\">0</span> ppm</strong></label><input id=\"simCO\" type=\"range\" min=\"0\" max=\"300\" step=\"5\" value=\"0\"><div class=\"simScale\"><span>0</span><span>Illustrative alert: 35 ppm</span><span>300</span></div></div>\n"
  "<div class=\"simControl\"><label for=\"simLEL\">Combustible gas <strong><span id=\"simLELValue\">0</span>% LEL</strong></label><input id=\"simLEL\" type=\"range\" min=\"0\" max=\"100\" step=\"1\" value=\"0\"><div class=\"simScale\"><span>0</span><span>Illustrative alert: 10% LEL</span><span>100</span></div></div>\n"
  "<div class=\"simControl\"><label for=\"simO2\">Oxygen · O₂ <strong><span id=\"simO2Value\">20.9</span>% vol</strong></label><input id=\"simO2\" type=\"range\" min=\"0\" max=\"25\" step=\"0.1\" value=\"20.9\"><div class=\"simScale\"><span>0%</span><span>Illustrative range: 19.5–23.5%</span><span>25%</span></div></div>\n"
  "<div class=\"simControl\"><label for=\"simNH3\">Ammonia · NH₃ <strong><span id=\"simNH3Value\">0</span> ppm</strong></label><input id=\"simNH3\" type=\"range\" min=\"0\" max=\"100\" step=\"1\" value=\"0\"><div class=\"simScale\"><span>0</span><span>Illustrative alert: 25 ppm</span><span>100</span></div></div>\n"
  "</div><div class=\"simResult\"><div><div class=\"sectionDesc\">SIMULATED ATMOSPHERE STATE</div><strong id=\"simOverallState\">NO SIMULATED ALERT</strong></div><div id=\"simOverallDetail\">Simulation is off. Live sensor logic is active.</div></div>\n"
  "</section>\n"
  "<section class=\"card wide\" id=\"deadmanCard\">\n"
  "<div class=\"cardHead\"><div><h3 class=\"sectionTitle\">Post-entry worker monitoring</h3><div class=\"sectionDesc\">Dead-man logic demonstration · movement input is simulated from the admin console</div></div><span class=\"statusTag\" id=\"deadmanBadge\">NOT ACTIVE</span></div>\n"
  "<div class=\"simResult\" style=\"margin-top:12px\"><div><div class=\"sectionDesc\">WORKER PRESENCE</div><strong id=\"workerPresence\">NO WORKER REGISTERED</strong></div><div><div class=\"sectionDesc\">WORKER ACTIVITY</div><strong id=\"deadmanState\">Monitoring not started</strong></div><div><div class=\"sectionDesc\">NO-MOVEMENT TIMER</div><strong id=\"deadmanTimer\">— / 8 seconds</strong></div></div>\n"
  "<div class=\"actions adminOnly\" style=\"margin-top:12px\"><button class=\"primary\" id=\"workerEnteredBtn\" type=\"button\">Worker Entered</button><button id=\"workerExitedBtn\" type=\"button\">Worker Exited / Close Session</button><button id=\"deadmanStartBtn\" type=\"button\">Start Monitoring</button><button id=\"deadmanNoMoveBtn\" type=\"button\">Simulate No Movement</button><button id=\"deadmanMoveBtn\" type=\"button\">Movement Detected</button><button id=\"deadmanResetBtn\" type=\"button\">Acknowledge / Reset Alert</button><button id=\"winchStartBtn\" type=\"button\" class=\"dangerBtn\">Simulate Extraction / Winch ON</button><button id=\"winchStopBtn\" type=\"button\">Stop Winch</button></div>\n"
  "<div class=\"notice\">Winch controls are demo-only and require an external motor driver. Start is accepted only when a worker is marked inside and a hazard/dead-man alert is active. Post-entry logic: hazards trigger alarms and extraction-required status. The demo does not command the access servo to close while a worker is marked inside. Movement is simulated; the winch output is only a control signal until a motor driver and physical winch are connected.</div>\n"
  "</section>\n"
  "<section class=\"card graphCard\"><div class=\"chartMeta\"><div><h3 class=\"sectionTitle\">Gas sensor history</h3><div class=\"sectionDesc\">Scrollable timeline · live raw ADC readings</div></div><div class=\"chartLegend\"><span><i class=\"legendLine\"></i>Sensor reading</span><span id=\"pointCount\">0 points</span></div></div><div class=\"chartScroll\" id=\"chartScroll\"><div class=\"chartInner\" id=\"chartInner\"><canvas id=\"gasChart\"></canvas></div></div><div class=\"chartHint\"><span>← Scroll horizontally to inspect history →</span><span>Warning: 450 · Danger: 500</span></div><div class=\"notice\">Readings are raw ADC counts, not calibrated ppm. This prototype cannot certify that a confined space is safe.</div></section>\n"
  "<section class=\"card sideCard\"><div class=\"cardHead\"><h3 class=\"sectionTitle\">Access & alarms</h3><span class=\"statusTag\" id=\"alarmBadge\">STANDBY</span></div><div class=\"statusHero\"><div class=\"statusIcon\" id=\"statusIcon\">✓</div><div><h3 id=\"mainStatus\">System monitoring</h3><p id=\"statusDescription\">Awaiting live device state</p></div></div><div class=\"kv\"><span>Last card UID</span><strong id=\"lastUID\">—</strong></div><div class=\"kv\"><span>Servo position</span><strong id=\"servoPosition\">Waiting for device…</strong></div><div class=\"kv\"><span>Buzzer</span><strong id=\"buzzerState\">OFF</strong></div><div class=\"kv\"><span>Last event</span><strong id=\"lastEvent\" style=\"max-width:62%\">System started</strong></div><div class=\"actions adminOnly\"><a class=\"actionButton\" href=\"/cmd/lock\">Lock now</a><a class=\"actionButton\" href=\"/cmd/buzzer\">Test buzzer</a><a class=\"actionButton dangerBtn\" href=\"/cmd/emergency\">Emergency alert</a></div></section>\n"
  "<section class=\"card half\"><div class=\"cardHead\"><h3 class=\"sectionTitle\">System status</h3><span class=\"statusTag good\" id=\"deviceStatus\">ONLINE</span></div><div class=\"miniStat\"><span>Controller</span><strong>NodeMCU ESP8266</strong></div><div class=\"miniStat\"><span>RFID module</span><strong>MFRC522 · SPI</strong></div><div class=\"miniStat\"><span>Sensor channel</span><strong>FC-22 · A0</strong></div><div class=\"miniStat\"><span>Network</span><strong id=\"networkName\">AVROD_Safety</strong></div><div class=\"miniStat\"><span>Last update</span><strong id=\"lastUpdate\">—</strong></div></section>\n"
  "<section class=\"card half\"><div class=\"cardHead\"><h3 class=\"sectionTitle\">Event log</h3><button class=\"quiet\" id=\"clearLogBtn\" style=\"padding:6px 9px;font-size:11px\">Clear view</button></div><div class=\"sectionDesc\">Recent sensor, access and alarm events.</div><div id=\"eventLog\" class=\"logList\"><div class=\"muted\" style=\"padding:15px 0\">Waiting for events…</div></div></section>\n"
  "</div><footer class=\"footer\"><span>AVROD · SAFETY MONITORING INTERFACE</span><span>Prototype status only · Not a certified safety instrument</span></footer></div>\n"
  "<div id=\"toast\" class=\"toast hidden\"></div>\n"
  "<script>\n"
  "let historyData=[], logs=[], lastLoggedState='', lastCardSeen='', lastDeviceEvent='', lastRender=0, toastTimer;\n"
  "const $=id=>document.getElementById(id);\n"
  "const adminPage=location.pathname==='/admin';\n"
  "(function configurePage(){document.body.classList.add(adminPage?'adminPage':'publicPage');if($('pageModePill'))$('pageModePill').textContent=adminPage?'ADMIN CONSOLE':'PUBLIC DISPLAY';if($('pageSwitch')){$('pageSwitch').href=adminPage?'/':'/admin';$('pageSwitch').textContent=adminPage?'Public dashboard ↗':'Admin console ↗';}if(!adminPage){document.querySelectorAll('.adminOnly').forEach(el=>el.classList.add('hidden'));const sc=$('simulatorCard');if(sc)sc.classList.add('hidden');}else{const pt=document.querySelector('.pageTitle h1');if(pt)pt.textContent='Admin controls';const desc=document.querySelector('.pageTitle p');if(desc)desc.textContent='Operator-only demo inputs and hardware tests. Public dashboard remains available on the other device.';}})();\n"
  "// Visible runtime diagnostics: if this label never changes from SCRIPT STARTING, the browser did not execute this script.\n"
  "window.addEventListener('error',function(e){const p=$('connectionPill');if(p){p.textContent='SCRIPT ERROR';p.className='statusTag bad';}const d=$('statusDescription');if(d)d.textContent='JavaScript error: '+(e.message||'unknown')+' (line '+(e.lineno||'?')+')';});\n"
  "window.addEventListener('unhandledrejection',function(e){const p=$('connectionPill');if(p){p.textContent='SCRIPT PROMISE ERROR';p.className='statusTag bad';}const d=$('statusDescription');if(d)d.textContent='Dashboard promise error: '+String(e.reason||'unknown');});\n"
  "try{const p=$('connectionPill');if(p){p.textContent='SCRIPT RUNNING · CONNECTING';p.className='statusTag warn';}}catch(_e){}\n"
  "function showToast(msg){$('toast').textContent=msg;$('toast').classList.remove('hidden');clearTimeout(toastTimer);toastTimer=setTimeout(()=>$('toast').classList.add('hidden'),3000)}\n"
  "function stamp(){return new Date().toLocaleTimeString([], {hour12:false})}\n"
  "function addLog(msg,type='normal',time=stamp()){logs.unshift({msg,type,time});logs=logs.slice(0,40);renderLogs()}\n"
  "function renderLogs(){const box=$('eventLog');if(!logs.length){box.innerHTML='<div class=\"muted\" style=\"padding:15px 0\">No events in this view.</div>';return}box.innerHTML=logs.map(x=>`<div class=\"logItem\"><span class=\"logTime\">${x.time}</span><i class=\"logDot\" style=\"background:${x.type==='danger'?'var(--red)':x.type==='warn'?'var(--amber)':'var(--cyan)'}\"></i><span class=\"logMsg\">${escapeHtml(x.msg)}</span></div>`).join('')}\n"
  "function escapeHtml(s){return String(s).replace(/[&<>\"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;',\"'\":'&#39;'}[c]))}\n"
  "function applyStatus(d){const dm=$('deadmanBadge');if(dm){dm.textContent=d.deadmanAlarm?'DEAD-MAN ALERT':!d.deadmanMonitoring?'NOT ACTIVE':d.deadmanNoMovement?'NO MOVEMENT':'MOVEMENT DETECTED';dm.className='statusTag '+(d.deadmanAlarm?'bad':d.deadmanNoMovement?'warn':d.deadmanMonitoring?'good':'');if($('workerPresence'))$('workerPresence').textContent=d.workerInside?'WORKER INSIDE':'NO WORKER REGISTERED';$('deadmanState').textContent=d.deadmanAlarm?'ALERT ACTIVE — acknowledge required':!d.deadmanMonitoring?'Monitoring not started':d.deadmanNoMovement?'No movement reported':'Movement reported';$('deadmanTimer').textContent=d.deadmanAlarm?'8 / 8 seconds':d.deadmanNoMovement?(Number(d.deadmanSeconds||0)+' / 8 seconds'):(d.deadmanMonitoring?'0 / 8 seconds':'— / 8 seconds');}const g=Number(d.gas||0),state=d.simulationMode?(d.simHazard?'DANGER':'NORMAL'):(g>=500?'DANGER':g>=450?'WARNING':'NORMAL');$('gasValue').textContent=g;$('gasSub').textContent='Actual FC-22 raw ADC · 0–1023 (shown even in simulation)';$('simModeBadge').textContent=d.simulationMode?'SIMULATION ACTIVE':'SIMULATION OFF';$('simModeBadge').className='statusTag '+(d.simulationMode?'warn':'');window.avrodSimulationEnabled=!!d.simulationMode; if(d.simulationMode){$('simH2S').value=d.h2s;$('simCO').value=d.co;$('simLEL').value=d.lel;$('simO2').value=d.o2;$('simNH3').value=d.nh3;renderSimControls();$('simOverallState').textContent=d.simHazard?'HAZARD ALERT':'NO SIMULATED ALERT';$('simOverallDetail').textContent=d.simHazard?'Physical interlock is commanded locked while simulated triggers are active.':'Simulation has no configured trigger active; RFID is still required.';}else{$('simOverallDetail').textContent='Simulation is off. Live sensor logic is active.';}$('gasBar').style.width=Math.min(100,g/1023*100)+'%';$('gasBar').style.background=state==='DANGER'?'var(--red)':state==='WARNING'?'var(--amber)':'var(--cyan)';$('gasState').textContent=state;$('gasState').className='metricValue '+(state==='DANGER'?'bad':state==='WARNING'?'warn':'good');$('thresholdText').textContent=d.simulationMode?'Simulation mode · see individual gas controls':'Raw ADC · warning ≥ 450 · danger ≥ 500';\n"
  "const unlocked=!!d.unlocked;$('lockState').textContent=unlocked?'UNLOCKED':'LOCKED';$('lockState').className='metricValue '+(unlocked?'warn':'good');$('lockSub').textContent=unlocked?'Servo release position':'Servo lock engaged';$('servoPosition').textContent=(unlocked?'Unlocked · ':'Locked · ')+(Number.isFinite(Number(d.servoAngle))?Number(d.servoAngle): (unlocked?0:90))+'°';$('ppeState').textContent=!d.cardEverRead?'AWAITING CARD':(d.cardAccepted?'CARD ACCEPTED':'CARD REJECTED');$('ppeState').className='metricValue '+(!d.cardEverRead?'':(d.cardAccepted?'good':'bad'));$('cardSub').textContent=!d.cardEverRead?'Present registered RFID card':(d.cardAccepted?'Authorized UID detected · access granted':'Card detected · access denied');$('lastUID').textContent=d.lastUID||'—';$('buzzerState').textContent=d.buzzer?'ACTIVE':'OFF';$('alarmBadge').textContent=d.buzzer?'ALARM ACTIVE':state==='WARNING'?'WARNING':'STANDBY';$('alarmBadge').className='statusTag '+(d.buzzer||state==='DANGER'?'bad':state==='WARNING'?'warn':'good');$('statusIcon').textContent=state==='DANGER'?'!':state==='WARNING'?'△':'✓';$('statusIcon').style.background=state==='DANGER'?'#29161d':state==='WARNING'?'#261f13':'#14251f';$('statusIcon').style.borderColor=state==='DANGER'?'#63313a':state==='WARNING'?'#634e2c':'#285a46';$('mainStatus').textContent=d.deadmanAlarm?'DEAD-MAN ALERT':(d.simulationMode?(d.simHazard?'Simulated hazard detected':'Simulation: no trigger crossed'):(state==='DANGER'?'Danger threshold reached':state==='WARNING'?'Elevated sensor reading':'System monitoring'));$('statusDescription').textContent=d.workerInside&&state==='DANGER'?'Worker marked inside: alarm active; extraction response required; access servo not commanded closed.':d.deadmanAlarm?'No movement for 8 seconds. Alarm active; extraction required (simulated response).':(d.simulationMode?(d.simHazard?'Simulation is driving lockout and alarm logic.':'Simulated state only; live ADC is shown separately.'):(state==='DANGER'?'Interlock locked; alarm active.':state==='WARNING'?'Caution: raw sensor reading has crossed warning level.':'No configured raw-ADC threshold crossed.'));$('lastEvent').textContent=d.lastEvent||'—';$('connectionPill').textContent='DEVICE ONLINE';$('connectionPill').className='statusTag good';$('deviceStatus').textContent='ONLINE';$('deviceStatus').className='statusTag good';$('lastUpdate').textContent=stamp();\n"
  "if(state!==lastLoggedState){addLog('Gas status changed to '+state,state==='DANGER'?'danger':state==='WARNING'?'warn':'normal');lastLoggedState=state}if(d.lastUID&&d.lastUID!==lastCardSeen&&d.lastUID!=='—'){addLog('RFID card scanned: '+d.lastUID,d.cardAccepted?'normal':'warn');lastCardSeen=d.lastUID}if(d.lastEvent&&d.lastEvent!==lastDeviceEvent){addLog(d.lastEvent,/ALERT|EMERGENCY|DEAD-MAN|hazard|denied/i.test(d.lastEvent)?'danger':'normal');lastDeviceEvent=d.lastEvent;}\n"
  "}\n"
  "let simPushTimer=null;\n"
  "function simValues(){return {h2s:Number($('simH2S').value),co:Number($('simCO').value),lel:Number($('simLEL').value),o2:Number($('simO2').value),nh3:Number($('simNH3').value)}}\n"
  "function simClientState(v){const hits=[];if(v.h2s>=10)hits.push('H₂S');if(v.co>=35)hits.push('CO');if(v.lel>=10)hits.push('combustible gas');if(v.o2<19.5||v.o2>23.5)hits.push('oxygen');if(v.nh3>=25)hits.push('NH₃');return hits}\n"
  "function renderSimControls(){const v=simValues();$('simH2SValue').textContent=v.h2s;$('simCOValue').textContent=v.co;$('simLELValue').textContent=v.lel;$('simO2Value').textContent=v.o2.toFixed(1);$('simNH3Value').textContent=v.nh3;const hits=simClientState(v);$('simOverallState').textContent=hits.length?'HAZARD ALERT':'NO SIMULATED ALERT';$('simOverallState').style.color=hits.length?'var(--red)':'var(--green)';$('simOverallDetail').textContent=hits.length?'Trigger(s): '+hits.join(', '):'No illustrative trigger crossed. This is not proof of a safe atmosphere.'}\n"
  "async function pushSimulation(mode){const v=simValues();const q=new URLSearchParams({mode:mode?'1':'0',h2s:v.h2s,co:v.co,lel:v.lel,o2:v.o2,nh3:v.nh3,ts:Date.now()});try{const r=await fetch('/api/sim?'+q.toString(),{cache:'no-store'});const d=await r.json();if(!r.ok)throw new Error(d.message||'HTTP '+r.status);showToast(d.message||'Simulation updated');await fetchStatus()}catch(e){showToast('Simulation update failed: '+e.message)}}\n"
  "function queueSimulationPush(){renderSimControls();clearTimeout(simPushTimer);simPushTimer=setTimeout(()=>{if(window.avrodSimulationEnabled)pushSimulation(true)},180)}\n"
  "function bindSimulator(){['simH2S','simCO','simLEL','simO2','simNH3'].forEach(id=>$(id).addEventListener('input',queueSimulationPush));$('simEnableBtn').addEventListener('click',()=>{window.avrodSimulationEnabled=true;pushSimulation(true)});$('simDisableBtn').addEventListener('click',()=>{window.avrodSimulationEnabled=false;pushSimulation(false)});$('simSafeBtn').addEventListener('click',()=>{$('simH2S').value=0;$('simCO').value=0;$('simLEL').value=0;$('simO2').value=20.9;$('simNH3').value=0;renderSimControls();window.avrodSimulationEnabled=true;pushSimulation(true)});$('simHazardBtn').addEventListener('click',()=>{$('simH2S').value=25;$('simCO').value=50;$('simLEL').value=15;$('simO2').value=18.5;$('simNH3').value=30;renderSimControls();window.avrodSimulationEnabled=true;pushSimulation(true)});renderSimControls()}\n"
  "function drawChart(){const inner=$('chartInner'),canvas=$('gasChart');if(!canvas||!historyData.length)return;const width=Math.max(900,historyData.length*24);inner.style.width=width+'px';const dpr=window.devicePixelRatio||1;canvas.width=width*dpr;canvas.height=245*dpr;const ctx=canvas.getContext('2d');ctx.scale(dpr,dpr);const w=width,h=245,pad={l:44,r:15,t:17,b:28},cw=w-pad.l-pad.r,ch=h-pad.t-pad.b;ctx.clearRect(0,0,w,h);ctx.font='10px system-ui';ctx.strokeStyle='#202b39';ctx.fillStyle='#8090a5';ctx.lineWidth=1;[0,250,450,500,750,1000].forEach(v=>{let y=pad.t+ch-(v/1023)*ch;ctx.beginPath();ctx.moveTo(pad.l,y);ctx.lineTo(w-pad.r,y);ctx.stroke();ctx.fillText(v,pad.l-31,y+3);});const yv=v=>pad.t+ch-(Math.min(1023,Math.max(0,v))/1023)*ch;[[450,'#f2bd58'],[500,'#ff6474']].forEach(([v,c])=>{ctx.strokeStyle=c;ctx.setLineDash([5,5]);ctx.beginPath();ctx.moveTo(pad.l,yv(v));ctx.lineTo(w-pad.r,yv(v));ctx.stroke();ctx.setLineDash([])});ctx.strokeStyle='#63d8e8';ctx.lineWidth=2;ctx.beginPath();historyData.forEach((pt,i)=>{const x=pad.l+(historyData.length<2?0:i/(historyData.length-1)*cw),y=yv(pt.v);if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y)});ctx.stroke();historyData.forEach((pt,i)=>{if(i%Math.max(1,Math.floor(historyData.length/14))===0||i===historyData.length-1){const x=pad.l+(historyData.length<2?0:i/(historyData.length-1)*cw);ctx.fillStyle='#8090a5';ctx.fillText(pt.t,x-17,h-9)}});$('pointCount').textContent=historyData.length+' points';if(!$('chartScroll').dataset.initialScroll){$('chartScroll').scrollLeft=$('chartScroll').scrollWidth;$('chartScroll').dataset.initialScroll='1';}}\n"
  "function fetchStatus(){\n"
  "  // XMLHttpRequest is used for broad mobile-browser compatibility.\n"
  "  try {\n"
  "  const p=$('connectionPill');if(p&&p.textContent.indexOf('DEVICE ONLINE')<0){p.textContent='POLLING STATUS…';p.className='statusTag warn';}\n"
  "  const xhr=new XMLHttpRequest();xhr.open('GET','/api/status?ts='+Date.now(),true);xhr.timeout=4000;\n"
  "  xhr.onreadystatechange=function(){if(xhr.readyState!==4)return;\n"
  "    if(xhr.status>=200&&xhr.status<300){try{const d=JSON.parse(xhr.responseText);applyStatus(d);const now=Date.now();if(now-lastRender>900){historyData.push({v:Number(d.gas||0),t:stamp()});if(historyData.length>180)historyData.shift();drawChart();lastRender=now}}\n"
  "      catch(e){$('connectionPill').textContent='STATUS DATA ERROR';$('connectionPill').className='statusTag bad';$('deviceStatus').textContent='DATA ERROR';$('deviceStatus').className='statusTag bad';$('statusDescription').textContent='Could not parse /api/status: '+e.message}}\n"
  "    else{$('connectionPill').textContent='DEVICE OFFLINE';$('connectionPill').className='statusTag bad';$('deviceStatus').textContent='OFFLINE';$('deviceStatus').className='statusTag bad';$('statusDescription').textContent='Status request failed (HTTP '+xhr.status+').'}\n"
  "  };\n"
  "  xhr.onerror=function(){$('connectionPill').textContent='DEVICE OFFLINE';$('connectionPill').className='statusTag bad';$('deviceStatus').textContent='OFFLINE';$('deviceStatus').className='statusTag bad';$('statusDescription').textContent='Cannot reach NodeMCU. Check Wi-Fi connection.'};\n"
  "  xhr.ontimeout=function(){$('connectionPill').textContent='STATUS TIMEOUT';$('connectionPill').className='statusTag bad';$('deviceStatus').textContent='TIMEOUT';$('statusDescription').textContent='NodeMCU status request timed out.'};xhr.send();\n"
  "  } catch(e) { const p=$('connectionPill');if(p){p.textContent='POLLING SCRIPT ERROR';p.className='statusTag bad';}const d=$('statusDescription');if(d)d.textContent='Status polling error: '+(e.message||String(e)); }\n"
  "}\n"
  "async function command(cmd){\n"
  "  showToast(cmd==='lock'?'Sending lock command…':cmd==='buzzer'?'Testing buzzer…':'Sending emergency command…');\n"
  "  try {\n"
  "    const url=window.location.origin+'/api/cmd?do='+encodeURIComponent(cmd)+'&_='+Date.now();\n"
  "    const r=await fetch(url,{method:'GET',cache:'no-store'});\n"
  "    const raw=await r.text();\n"
  "    let d={}; try { d=JSON.parse(raw); } catch (_) {}\n"
  "    if(!r.ok) throw new Error('HTTP '+r.status+' '+raw);\n"
  "    showToast(d.message||('Command sent: '+cmd));\n"
  "    if(d.event)addLog(d.event,cmd==='emergency'?'danger':'normal');\n"
  "    await fetchStatus();\n"
  "  } catch(e) { showToast('Command failed: '+e.message); }\n"
  "}\n"
  "window.command=command;\n"
  "// Bind controls explicitly after the page elements exist (more reliable than inline onclick).\n"
  "// Control actions are ordinary links to server-side routes, so they work on mobile even if JavaScript/fetch is blocked.\n"
  "$('clearLogBtn').addEventListener('click',()=>{logs=[];renderLogs();showToast('Event log view cleared')});\n"
  "window.addEventListener('error',e=>showToast('Dashboard script error: '+(e.message||'unknown error')));\n"
  "let pollTimer=null,clockTimer=null;function startDashboard(){if(pollTimer)return;fetchStatus();pollTimer=setInterval(fetchStatus,1000);clockTimer=setInterval(()=>$('clock').textContent=stamp(),1000);$('clock').textContent=stamp()}window.addEventListener('resize',()=>drawChart());bindSimulator();\n"
  "async function workerAction(action){try{const r=await fetch('/api/worker?action='+encodeURIComponent(action)+'&ts='+Date.now(),{cache:'no-store'});const d=await r.json();if(!r.ok)throw new Error(d.message||'HTTP '+r.status);showToast(d.message||'Worker state updated');fetchStatus();}catch(e){showToast('Worker command failed: '+e.message)}}\n"
  "async function deadmanAction(action){try{const r=await fetch('/api/deadman?action='+encodeURIComponent(action)+'&ts='+Date.now(),{cache:'no-store'});const d=await r.json();if(!r.ok)throw new Error(d.message||'HTTP '+r.status);showToast(d.message||'Dead-man state updated');fetchStatus();}catch(e){showToast('Dead-man command failed: '+e.message)}}\n"
  "if($('deadmanStartBtn'))$('deadmanStartBtn').addEventListener('click',()=>deadmanAction('start'));\n"
  "if($('workerEnteredBtn'))$('workerEnteredBtn').addEventListener('click',()=>workerAction('entered'));\n"
  "if($('workerExitedBtn'))$('workerExitedBtn').addEventListener('click',()=>workerAction('exited'));\n"
  "if($('deadmanNoMoveBtn'))$('deadmanNoMoveBtn').addEventListener('click',()=>deadmanAction('missing'));\n"
  "if($('deadmanMoveBtn'))$('deadmanMoveBtn').addEventListener('click',()=>deadmanAction('moving'));\n"
  "if($('deadmanResetBtn'))$('deadmanResetBtn').addEventListener('click',()=>deadmanAction('reset'));\n"
  "if($('winchStartBtn'))$('winchStartBtn').addEventListener('click',()=>command('winch_on'));\n"
  "if($('winchStopBtn'))$('winchStopBtn').addEventListener('click',()=>command('winch_off'));\n"
  "showToast('Dashboard controls ready');startDashboard();\n"
  "</script></body></html>\n"
;

const char* statusName() {
  if (gasReading >= GAS_DANGER) return "DANGER";
  if (gasReading >= GAS_WARNING) return "WARNING";
  return "NORMAL";
}

void addEvent(const String& message) {
  lastEvent = message;
  eventLog[eventNext] = message;
  eventNext = (eventNext + 1) % 12;
  if (eventCount < 12) eventCount++;
  Serial.println(message);
}

String uidToString(MFRC522::Uid *uid) {
  String s;
  for (byte i = 0; i < uid->size; i++) {
    if (uid->uidByte[i] < 0x10) s += "0";
    s += String(uid->uidByte[i], HEX);
    if (i < uid->size - 1) s += ":";
  }
  s.toUpperCase();
  return s;
}

bool isAuthorized(MFRC522::Uid *uid) {
  if (uid->size != AUTH_UID_LENGTH) return false;
  for (byte i = 0; i < AUTH_UID_LENGTH; i++) {
    if (uid->uidByte[i] != AUTH_UID[i]) return false;
  }
  return true;
}

void setBuzzer(bool on) {
  digitalWrite(BUZZER_PIN, (on != BUZZER_ACTIVE_LOW) ? HIGH : LOW);
}

void lockAccess(const String& reason) {
  isUnlocked = false;
  lockServo.write(SERVO_LOCK_ANGLE);
  addEvent(reason);
}

void handleRoot() {
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.send_P(200, "text/html; charset=utf-8", PAGE);
}

void handleStatus() {
  String json = "{";
  json += "\"gas\":" + String(gasReading) + ",";
  json += "\"simulationMode\":" + String(simulationMode ? "true" : "false") + ",";
  json += "\"simHazard\":" + String(simulatedHazard() ? "true" : "false") + ",";
  json += "\"h2s\":" + String(simH2S) + ",";
  json += "\"co\":" + String(simCO) + ",";
  json += "\"lel\":" + String(simLEL) + ",";
  json += "\"o2\":" + String(simO2, 1) + ",";
  json += "\"nh3\":" + String(simNH3) + ",";
  json += "\"unlocked\":" + String(isUnlocked ? "true" : "false") + ",";
  json += "\"servoAngle\":" + String(isUnlocked ? SERVO_UNLOCK_ANGLE : SERVO_LOCK_ANGLE) + ",";
  json += "\"buzzer\":" + String((buzzerTestActive || activeHazard() || deadmanAlarm) ? "true" : "false") + ",";
  json += "\"lastUID\":\"" + lastCardUID + "\",";
  json += "\"cardAccepted\":" + String(lastCardAccepted ? "true" : "false") + ",";
  json += "\"cardEverRead\":" + String(cardEverRead ? "true" : "false") + ",";
  json += "\"workerInside\":" + String(workerInside ? "true" : "false") + ",";
  json += "\"deadmanMonitoring\":" + String(deadmanMonitoring ? "true" : "false") + ",";
  json += "\"deadmanNoMovement\":" + String(deadmanNoMovement ? "true" : "false") + ",";
  json += "\"deadmanAlarm\":" + String(deadmanAlarm ? "true" : "false") + ",";
  unsigned long dmSeconds = deadmanNoMovement && !deadmanAlarm ? min((millis() - noMovementSince) / 1000UL, 8UL) : 0;
  json += "\"deadmanSeconds\":" + String(dmSeconds) + ",";
  json += "\"lastEvent\":\"";
  String safeEvent = lastEvent;
  safeEvent.replace("\\", "\\\\"); safeEvent.replace("\"", "\\\"");
  safeEvent.replace("\n", " "); safeEvent.replace("\r", " ");
  json += safeEvent + "\"}";
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma", "no-cache");
  server.send(200, "application/json", json);
}

void handleSimulation() {
  String mode = server.arg("mode");
  simulationMode = (mode == "1" || mode == "on" || mode == "true");
  if (server.hasArg("h2s")) simH2S = constrain(server.arg("h2s").toInt(), 0, 100);
  if (server.hasArg("co")) simCO = constrain(server.arg("co").toInt(), 0, 300);
  if (server.hasArg("lel")) simLEL = constrain(server.arg("lel").toInt(), 0, 100);
  if (server.hasArg("o2")) simO2 = constrain(server.arg("o2").toFloat(), 0.0f, 25.0f);
  if (server.hasArg("nh3")) simNH3 = constrain(server.arg("nh3").toInt(), 0, 100);
  if (activeHazard()) {
    // Before entry, keep the interlock locked. Once a worker is marked inside,
    // alarm and request response without commanding the access servo to close.
    if (!workerInside && isUnlocked) lockAccess(simulationMode ? "Simulation hazard before entry: servo relocked" : "Live sensor danger before entry: servo relocked");
    setBuzzer(true);
    if (workerInside) addEvent("ATMOSPHERE ALERT while worker inside: extraction response required");
  } else if (!buzzerTestActive && !deadmanAlarm) {
    setBuzzer(false);
  }
  addEvent(String("Atmosphere mode: ") + (simulationMode ? "SIMULATION" : "LIVE SENSOR"));
  String msg = simulationMode ? "Simulation enabled; simulated values drive demo alert logic" : "Simulation disabled; live sensor logic restored";
  server.send(200, "application/json", String("{\"message\":\"") + msg + "\"}");
}

void handleWorkerState() {
  String action = server.arg("action");
  if (action == "entered") {
    if (activeHazard()) {
      workerInside = false;
      addEvent("Worker Entered rejected: simulated/live hazard active");
      server.send(409, "application/json", "{\"message\":\"Hazard active: entry state not accepted in demo logic\"}");
      return;
    }
    workerInside = true;
    deadmanMonitoring = true;
    deadmanNoMovement = false;
    deadmanAlarm = false;
    noMovementSince = 0;
    addEvent("Worker marked INSIDE: post-entry monitoring active");
    server.send(200, "application/json", "{\"message\":\"Worker marked inside; post-entry monitoring active\"}");
  } else if (action == "exited") {
    digitalWrite(WINCH_PIN, LOW);
    winchRunning = false;
    workerInside = false;
    deadmanMonitoring = false;
    deadmanNoMovement = false;
    deadmanAlarm = false;
    noMovementSince = 0;
    if (!activeHazard() && !buzzerTestActive) setBuzzer(false);
    addEvent("Worker marked EXITED: session closed");
    // Only return to normal entry-control mode after the operator marks the worker out.
    if (activeHazard()) {
      isUnlocked = false;
      lockServo.write(SERVO_LOCK_ANGLE);
      addEvent("Entry interlock remains locked: hazard still active after exit");
    }
    server.send(200, "application/json", "{\"message\":\"Worker marked exited; session closed\"}");
  } else {
    server.send(400, "application/json", "{\"message\":\"Unknown worker action\"}");
  }
}

void handleDeadman() {
  String action = server.arg("action");
  if (action == "start") {
    deadmanMonitoring = true; deadmanNoMovement = false; deadmanAlarm = false; noMovementSince = 0;
    addEvent("Post-entry monitoring started (simulated movement input)");
    if (!activeHazard() && !buzzerTestActive) setBuzzer(false);
    server.send(200, "application/json", "{\"message\":\"Post-entry monitoring started\"}");
  } else if (action == "missing") {
    if (!deadmanMonitoring) deadmanMonitoring = true;
    if (!deadmanAlarm) { deadmanNoMovement = true; noMovementSince = millis(); addEvent("Dead-man timer started: no movement simulated"); }
    server.send(200, "application/json", "{\"message\":\"No-movement timer started (8 seconds)\"}");
  } else if (action == "moving") {
    if (!deadmanAlarm) { deadmanNoMovement = false; noMovementSince = 0; addEvent("Movement detected: dead-man timer reset"); }
    server.send(200, "application/json", "{\"message\":\"Movement detected; timer reset if no alert was active\"}");
  } else if (action == "reset") {
    deadmanAlarm = false; deadmanNoMovement = false; deadmanMonitoring = false; noMovementSince = 0;
    if (!activeHazard() && !buzzerTestActive) setBuzzer(false);
    addEvent("Dead-man alert acknowledged; monitoring stopped");
    server.send(200, "application/json", "{\"message\":\"Alert acknowledged; monitoring stopped\"}");
  } else {
    server.send(400, "application/json", "{\"message\":\"Unknown dead-man action\"}");
  }
}

void handleCommand() {
  String cmd = server.arg("do");
  if (cmd == "lock") {
    lockAccess("Dashboard: lock commanded");
    server.send(200, "application/json", "{\"message\":\"Servo lock engaged\",\"event\":\"Manual lock command\"}");
  } else if (cmd == "buzzer") {
    buzzerTestActive = true;
    buzzerTestUntil = millis() + 1200;
    setBuzzer(true);
    addEvent("Dashboard: buzzer test");
    server.send(200, "application/json", "{\"message\":\"Buzzer test started\",\"event\":\"Buzzer test started\"}");
  } else if (cmd == "winch_on") {
    if (!workerInside || !(activeHazard() || deadmanAlarm)) {
      digitalWrite(WINCH_PIN, LOW); winchRunning = false;
      server.send(409, "application/json", "{\"message\":\"Winch start blocked: mark worker inside and trigger a hazard/dead-man alert first\"}");
      return;
    }
    digitalWrite(WINCH_PIN, HIGH); winchRunning = true;
    addEvent("DEMO WINCH ON: extraction movement commanded via motor driver");
    server.send(200, "application/json", "{\"message\":\"Winch output ON — monitor the model and stop it manually\",\"event\":\"Demo winch started\"}");
  } else if (cmd == "winch_off") {
    digitalWrite(WINCH_PIN, LOW); winchRunning = false;
    addEvent("Demo winch stopped");
    server.send(200, "application/json", "{\"message\":\"Winch output OFF\",\"event\":\"Demo winch stopped\"}");
  } else if (cmd == "emergency") {
    if (!workerInside) lockAccess("EMERGENCY ALERT: access locked");
    else addEvent("EMERGENCY ALERT while worker inside: extraction response required; access servo not closed");
    buzzerTestActive = true;
    buzzerTestUntil = millis() + 5000;
    setBuzzer(true);
    server.send(200, "application/json", "{\"message\":\"Emergency alert active: lock + buzzer\",\"event\":\"Emergency alert triggered\"}");
  } else {
    server.send(400, "application/json", "{\"message\":\"Unknown command\"}");
  }
}

void redirectToDashboard() {
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Location", "/", true);
  server.send(303, "text/plain", "Command executed; returning to dashboard.");
}

void handleQuickLock() {
  lockAccess("Dashboard: lock commanded");
  redirectToDashboard();
}

void handleQuickBuzzer() {
  buzzerTestActive = true;
  buzzerTestUntil = millis() + 1200;
  setBuzzer(true);
  addEvent("Dashboard: buzzer test");
  redirectToDashboard();
}

void handleQuickEmergency() {
  if (!workerInside) lockAccess("EMERGENCY ALERT: access locked");
  else addEvent("EMERGENCY ALERT while worker inside: extraction response required; access servo not closed");
  buzzerTestActive = true;
  buzzerTestUntil = millis() + 5000;
  setBuzzer(true);
  addEvent("Dashboard: emergency alert triggered");
  redirectToDashboard();
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(WINCH_PIN, OUTPUT);
  digitalWrite(WINCH_PIN, LOW); // winch driver disabled at startup
  winchRunning = false;
  setBuzzer(false);
  lockServo.attach(SERVO_PIN);
  lockServo.write(SERVO_LOCK_ANGLE);

  SPI.begin();
  rfid.PCD_Init();
  delay(50);
  rfid.PCD_SetAntennaGain(rfid.RxGain_max);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  delay(100);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/admin", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/sim", HTTP_GET, handleSimulation);
  server.on("/api/deadman", HTTP_GET, handleDeadman);
  server.on("/api/worker", HTTP_GET, handleWorkerState);
  server.on("/api/cmd", HTTP_GET, handleCommand);
  server.on("/cmd/lock", HTTP_GET, handleQuickLock);
  server.on("/cmd/buzzer", HTTP_GET, handleQuickBuzzer);
  server.on("/cmd/emergency", HTTP_GET, handleQuickEmergency);
  server.onNotFound(handleNotFound);
  server.begin();

  gasReading = analogRead(GAS_PIN);
  addEvent("AVROD dashboard started");
  Serial.println();
  Serial.println("AVROD dashboard ready");
  Serial.print("Wi-Fi network: "); Serial.println(AP_SSID);
  Serial.print("Dashboard URL: http://"); Serial.println(WiFi.softAPIP());
  Serial.println("Login screen disabled: dashboard opens directly");
  Serial.print("RC522 firmware version: 0x"); rfid.PCD_DumpVersionToSerial();
}

void loop() {
  server.handleClient();
  unsigned long now = millis();

  if (now - lastSensorRead >= 250) {
    lastSensorRead = now;
    gasReading = analogRead(GAS_PIN);

    // Fail-closed behavior at the danger threshold.
    if (activeHazard()) {
      if (!workerInside && isUnlocked) lockAccess(simulationMode ? "Simulated hazard before entry: servo relocked" : "Danger threshold before entry: servo relocked");
      setBuzzer(true);
    } else if (deadmanAlarm) {
      setBuzzer(true);
    } else {
      if (!buzzerTestActive) setBuzzer(false);
    }
  }

  if (deadmanMonitoring && deadmanNoMovement && !deadmanAlarm && (now - noMovementSince >= DEADMAN_TIMEOUT_MS)) {
    deadmanAlarm = true;
    if (!workerInside) lockAccess("DEAD-MAN ALERT: no movement for 8 seconds; extraction required");
    else addEvent("DEAD-MAN ALERT while worker inside: extraction response required; access servo not closed");
    setBuzzer(true);
  }

  if (buzzerTestActive && (long)(now - buzzerTestUntil) >= 0) {
    buzzerTestActive = false;
    if (!activeHazard() && !deadmanAlarm) setBuzzer(false);
  }

  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    lastCardUID = uidToString(&rfid.uid);
    cardEverRead = true;
    lastCardAccepted = false;

    // Brief audible acknowledgement whenever a card is read.
    // The dashboard also updates PPE KIT DETECTION and Last card UID.
    buzzerTestActive = true;
    buzzerTestUntil = millis() + 180;
    setBuzzer(true);

    if (workerInside) {
      lastCardAccepted = false;
      addEvent("RFID scanned during occupied session; access servo unchanged");
    } else if (isAuthorized(&rfid.uid)) {
      if (activeHazard()) {
        lockAccess(simulationMode ? "Authorized card denied: simulated hazard" : "Authorized card denied: danger reading");
        lastCardAccepted = false;
      } else {
        isUnlocked = true;
        lockServo.write(SERVO_UNLOCK_ANGLE);
        lastCardAccepted = true;
        addEvent("Authorized card accepted: " + lastCardUID);
      }
    } else {
      isUnlocked = false;
      lockServo.write(SERVO_LOCK_ANGLE);
      addEvent("Unauthorized card: " + lastCardUID);
    }
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
  }

  yield();
}
