// Светофор, метод 2 (table-driven FSM).
// Пины: 13 Red, 12 Yellow, 11 Green, 2 кнопка, 3 EMERGENCY (замкнуть на GND = авария).
// Короткое нажатие = пешеходный запрос (в Green), удержание >= 2 с = ночной режим.

const int PIN_RED = 13, PIN_YELLOW = 12, PIN_GREEN = 11, PIN_BUTTON = 2, PIN_EMERGENCY = 3;

enum State { S_GREEN, S_YELLOW, S_RED, S_WARNING, NUM_STATES };
enum Event { E_NONE, E_TIMER, E_PED, E_EMERGENCY_ON, E_EMERGENCY_OFF, E_NIGHT_TOGGLE, NUM_EVENTS };
const char* names[] = { "GREEN", "YELLOW", "RED", "WARNING" };

State currentState = S_GREEN;
unsigned long stateStart = 0, stateDur = 0;
bool pedRequest = false, emergency = false, nightMode = false;

typedef void (*Handler)(void);
Handler fsmTable[NUM_STATES][NUM_EVENTS];

// Лог: timestamp, что произошло, причина
void logMsg(const char* what, const char* info) {
  Serial.print('['); Serial.print(millis()); Serial.print(" ms] ");
  Serial.print(what); Serial.print(" | "); Serial.println(info);
}

// Общий переход: состояние, длительность, светодиоды, лог
void goToState(State s, unsigned long dur, const char* reason) {
  currentState = s;
  stateDur = dur;
  stateStart = millis();
  digitalWrite(PIN_RED, s == S_RED);
  digitalWrite(PIN_GREEN, s == S_GREEN);
  digitalWrite(PIN_YELLOW, s != S_GREEN && s != S_RED);
  logMsg(names[s], reason);
}

// ---------- Обработчики ----------
void onGreenTimer()  { goToState(S_YELLOW, 3000, "TIMER"); }
void onGreenPed()    { pedRequest = true; logMsg("EVENT", "PED_REQUEST"); }
void onYellowTimer() {
  if (pedRequest) { pedRequest = false; goToState(S_RED, 15000, "TIMER + pedestrian time"); }
  else goToState(S_RED, 10000, "TIMER");
}
void onRedTimer()    { goToState(S_GREEN, 10000, "TIMER"); }

void onWarningTimer() {   // мигание жёлтого каждые 500 мс
  stateStart = millis();
  digitalWrite(PIN_YELLOW, !digitalRead(PIN_YELLOW));
}
void onAnyEmergencyOn() {
  emergency = true;
  pedRequest = false;
  goToState(S_WARNING, 500, "EMERGENCY_ON");
}
void onWarningEmergencyOff() {
  emergency = false;
  if (!nightMode) goToState(S_GREEN, 10000, "EMERGENCY_OFF");
}
void onAnyNightToggle() {
  nightMode = !nightMode;
  if (nightMode) { pedRequest = false; goToState(S_WARNING, 500, "NIGHT_MODE_ON"); }
  else if (!emergency) goToState(S_GREEN, 10000, "NIGHT_MODE_OFF");
}

void setupTable() {
  for (int s = 0; s < NUM_STATES; ++s)
    for (int e = 0; e < NUM_EVENTS; ++e) fsmTable[s][e] = NULL;

  fsmTable[S_GREEN][E_TIMER] = onGreenTimer;
  fsmTable[S_GREEN][E_PED] = onGreenPed;
  fsmTable[S_YELLOW][E_TIMER] = onYellowTimer;
  fsmTable[S_RED][E_TIMER] = onRedTimer;
  fsmTable[S_WARNING][E_TIMER] = onWarningTimer;
  fsmTable[S_WARNING][E_EMERGENCY_OFF] = onWarningEmergencyOff;
  // авария и ночной режим - из любого состояния
  for (int s = 0; s < NUM_STATES; ++s) {
    fsmTable[s][E_EMERGENCY_ON] = onAnyEmergencyOn;
    fsmTable[s][E_NIGHT_TOGGLE] = onAnyNightToggle;
  }
}

// Формирует одно событие: авария, кнопка (антидребезг 50 мс), таймер
Event pollEvent() {
  static bool lastRaw = false, pressed = false;
  static unsigned long lastChange = 0, pressStart = 0;

  bool em = (digitalRead(PIN_EMERGENCY) == LOW);
  if (em != emergency) return em ? E_EMERGENCY_ON : E_EMERGENCY_OFF;

  bool raw = (digitalRead(PIN_BUTTON) == LOW);
  if (raw != lastRaw) { lastRaw = raw; lastChange = millis(); }
  if (millis() - lastChange >= 50 && raw != pressed) {   // сигнал стабилен 50 мс
    pressed = raw;
    if (pressed) pressStart = millis();
    else return (millis() - pressStart >= 2000) ? E_NIGHT_TOGGLE : E_PED;  // при отпускании
  }

  if (millis() - stateStart >= stateDur) return E_TIMER;
  return E_NONE;
}

void setup() {
  pinMode(PIN_RED, OUTPUT);
  pinMode(PIN_YELLOW, OUTPUT);
  pinMode(PIN_GREEN, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_EMERGENCY, INPUT_PULLUP);
  Serial.begin(9600);
  setupTable();
  goToState(S_GREEN, 10000, "start");
}

void loop() {
  // нештатное состояние -> безопасный режим (мигающий жёлтый)
  if (currentState >= NUM_STATES) { nightMode = true; goToState(S_WARNING, 500, "ERROR"); }

  Event ev = pollEvent();
  if (ev != E_NONE) {
    Handler h = fsmTable[currentState][ev];
    if (h) h();
  }
}
