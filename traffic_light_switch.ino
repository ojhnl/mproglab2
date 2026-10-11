// Светофор, метод 1 (switch-case).
// Пины: 13 Red, 12 Yellow, 11 Green, 2 кнопка, 3 EMERGENCY (замкнуть на GND = авария).
// Короткое нажатие = пешеходный запрос (в Green), удержание >= 2 с = ночной режим.

const int PIN_RED = 13, PIN_YELLOW = 12, PIN_GREEN = 11, PIN_BUTTON = 2, PIN_EMERGENCY = 3;

enum State { S_GREEN, S_YELLOW, S_RED, S_WARNING };
const char* names[] = { "GREEN", "YELLOW", "RED", "WARNING" };

State currentState = S_GREEN;
unsigned long stateStart = 0, duration = 0;
bool pedRequest = false, emergency = false, nightMode = false;

// Лог: timestamp, что произошло, причина
void logMsg(const char* what, const char* info) {
  Serial.print('['); Serial.print(millis()); Serial.print(" ms] ");
  Serial.print(what); Serial.print(" | "); Serial.println(info);
}

// Светодиоды по состоянию
void setOutputsForState(State s) {
  digitalWrite(PIN_RED, s == S_RED);
  digitalWrite(PIN_GREEN, s == S_GREEN);
  digitalWrite(PIN_YELLOW, s != S_GREEN && s != S_RED);
}

void goToState(State s, unsigned long dur, const char* reason) {
  currentState = s;
  stateStart = millis();
  duration = dur;
  setOutputsForState(s);
  logMsg(names[s], reason);
}

// Кнопка (антидребезг 50 мс) и вход аварии
void readInputs() {
  static bool lastRaw = false, pressed = false;
  static unsigned long lastChange = 0, pressStart = 0;

  bool raw = (digitalRead(PIN_BUTTON) == LOW);
  if (raw != lastRaw) { lastRaw = raw; lastChange = millis(); }
  if (millis() - lastChange >= 50 && raw != pressed) {   // сигнал стабилен 50 мс
    pressed = raw;
    if (pressed) pressStart = millis();
    else if (millis() - pressStart >= 2000) {            // отпустили после долгого нажатия
      nightMode = !nightMode;
      logMsg("EVENT", "NIGHT_MODE_TOGGLE");
    } else if (currentState == S_GREEN) {                // отпустили после короткого
      pedRequest = true;
      logMsg("EVENT", "PED_REQUEST");
    }
  }

  bool em = (digitalRead(PIN_EMERGENCY) == LOW);
  if (em != emergency) {
    emergency = em;
    logMsg("EVENT", em ? "EMERGENCY_ON" : "EMERGENCY_OFF");
  }
}

void setup() {
  pinMode(PIN_RED, OUTPUT);
  pinMode(PIN_YELLOW, OUTPUT);
  pinMode(PIN_GREEN, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_EMERGENCY, INPUT_PULLUP);
  Serial.begin(9600);
  goToState(S_GREEN, 10000, "start");
}

void loop() {
  readInputs();

  // Авария или ночной режим -> сразу мигающий жёлтый из любого состояния
  if ((emergency || nightMode) && currentState != S_WARNING) {
    pedRequest = false;
    goToState(S_WARNING, 500, emergency ? "EMERGENCY_ON" : "NIGHT_MODE_ON");
  }

  bool expired = (millis() - stateStart >= duration);

  switch (currentState) {
    case S_GREEN:
      if (expired) goToState(S_YELLOW, 3000, "TIMER");
      break;
    case S_YELLOW:
      if (expired) {
        if (pedRequest) { pedRequest = false; goToState(S_RED, 15000, "TIMER + pedestrian time"); }
        else goToState(S_RED, 10000, "TIMER");
      }
      break;
    case S_RED:
      if (expired) goToState(S_GREEN, 10000, "TIMER");
      break;
    case S_WARNING:
      if (!emergency && !nightMode) goToState(S_GREEN, 10000, "WARNING OFF");
      else if (expired) {   // мигание каждые 500 мс
        stateStart = millis();
        digitalWrite(PIN_YELLOW, !digitalRead(PIN_YELLOW));
      }
      break;
    default:
      nightMode = true;   // ошибка -> безопасный режим (мигающий жёлтый)
      break;
  }
}
