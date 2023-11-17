/*
	BuzzerKeyboard
	Arduino-based HID (keyboard) with some big buttons to control e.g. web applications.
    Copyright (C) 2016 - 2023 Christian Lorenz

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#define DBG 0

#include <stdarg.h>
#include <Keyboard.h>
#include <EEPROM.h>
#include <time.h>
#include "util.h"

/*
**  pin used as an activity indicator (LED)
*/
#define PIN_ACTIVITY 3

/*
**  time buzzer #0 has to pressed continuslycw for this time [ms],
**  before the device enters the configuration mode
*/
#define CONF_ENTER_DELAY 5000

/*
**  when de device is in config mode, the activity LED should
**  blink with the specified frequency [ms]
*/
#define CONF_MODE_BLINK 100
#define PROG_MODE_BLINK 500

/*
**  the delay [ms] between button detections
*/
#define BUTTON_DETECTION_DELAY 50

/*
**  the time [ms] a keystroke should take
**  (before the modifier keys will be released again)
*/
#define KEY_STROKE_DELAY 100

/*
**  the base key which will be send
**  the 1st key (Button A) will send KEY_CODE_BASE + 0
**  the 2nd key (Button B) will send KEY_CODE_BASE + 1
**  ...
*/
#define KEY_CODE_BASE 'j'

/*
**  structure to keep a button definition
*/
typedef struct _keyboard {
  /*
  ** input pin
  **
  ** this pin is used for the button itself
  */
  int pin_in;

  /*
  ** output pin
  **
  ** this pin controls the LED of the buzzer button
  */
  int pin_out;

  /*
  **  store the current state here
  */
  int state;

  /*
  **  timestamp of the last change on the pin_in
  */
  unsigned long last_change;
} BUZZER;

/*
**  the button definitions
**  input pin ... for the switch/buzzer button isself
**  output pin .. used for the corresponding LED
**
**  NOTE: the keycodes are chosen, so that these will hopefully
**  work in the browsers and do not collide with browsers shortcuts.
*/
static BUZZER _buzzers[] = {
  /* Buzzer A */ { 4, 5 },
  /* Buzzer B */ { 6, 7 },
  /* Buzzer C */ { 8, 9 },
  /* Buzzer D */ { 10, 16 },
  /* Buzzer E */ { 14, 15 },
  /* end of list */ { 0 }
};

/*
**  address in the EEPROM to store the modifier configuration
*/
#define EEPROM_ADDR_MODIFIER 0

/*
**  the different modifier modes
*/
#define MODIFIER_INVALID 0
#define MODIFIER_NONE 1
#define MODIFIER_ALT 2
#define MODIFIER_ALTSHIFT 3
#define MODIFIER_ALTCTRL 4

/*
**  control the mode of the arduino
*/
static int _mode_conf = 0;                 // we start in normal operation, not in config mode
static int _mode_prog = 0;                 // program mode (more or less for debugging and reprogramming of the arduino, serial communication is enabled)
static int _mode_modifier = MODIFIER_ALT;  // one of the MODIFIER_*

/*
**  switch a buzzer light on/off
*/
void buzzers_set_light(int buzzer, int on) {
  digitalWrite(_buzzers[buzzer].pin_out, (on) ? HIGH : LOW);
}

/*
**  let a buzzer light blink
*/
void buzzers_blink_light(int buzzer, int times, int ms) {
  for (int n = times - 1; n >= 0; n--) {
    buzzers_set_light(buzzer, 1);
    delay(ms);
    buzzers_set_light(buzzer, 0);
    if (n > 0)
      delay(ms);
  }
}

/*
**  switch all buzzer lights on/off
*/
void buzzers_set_all_lights(int on) {
  for (int buzzer = 0; _buzzers[buzzer].pin_in; buzzer++)
    buzzers_set_light(buzzer, on);
}

/*
**  let all buzzer lights blink at the same time
*/
void buzzers_blink_all_lights(int times, int ms) {
  for (int n = times - 1; n >= 0; n--) {
    for (int buzzer = 0; _buzzers[buzzer].pin_in; buzzer++)
      buzzers_set_all_lights(1);
    delay(ms);
    for (int buzzer = 0; _buzzers[buzzer].pin_in; buzzer++)
      buzzers_set_all_lights(0);
    if (n > 0)
      delay(ms);
  }
}

/*
**  let all buzzer lights blink serialized
*/
void buzzers_run_all_lights(int times, int ms) {
  for (int buzzer = 0; _buzzers[buzzer].pin_in; buzzer++)
    buzzers_blink_light(buzzer, times, ms);
}

/*
**  do a little light show
*/
void buzzers_lightshow() {
  for (int n = 0; n < 5; n++)
    buzzers_run_all_lights(1, 50);
  buzzers_blink_all_lights(5, 50);
}

/*
**  scan the buzzers and return the first one pressed
*/
int buzzers_get_pressed(void) {
  buzzers_set_all_lights(LOW);
  for (int buzzer = 0; _buzzers[buzzer].pin_in; buzzer++) {
    int state = digitalRead(_buzzers[buzzer].pin_in);
    if (state != _buzzers[buzzer].state) {
      /*
      **  state has changed on that button
      */
      if (_buzzers[buzzer].last_change > 0 && millis() - _buzzers[buzzer].last_change < BUTTON_DETECTION_DELAY) {
        /*
        **  de-bouncing: this change is too fast, so ignore it
        */
        continue;
      }

      /*
      **  store state and time
      */
      _buzzers[buzzer].state = state;
      _buzzers[buzzer].last_change = millis();

      if (state == LOW) {
        LogMsg("Buzzer #%d pressed", buzzer);
        buzzers_set_light(buzzer, HIGH);
        return buzzer;
      }
    }
  }
  return -1;
}

/*
**  get a plain text name of the modifier
*/
char *modifier_name(int modifier) {
  switch (_mode_modifier) {
    case MODIFIER_NONE:
      return "NONE";
    case MODIFIER_ALT:
      return "ALT";
    case MODIFIER_ALTSHIFT:
      return "ALT+SHIFT";
    case MODIFIER_ALTCTRL:
      return "ALT+CTRL";
  }
  return "INVALID";
}

/*
**  setup called once on powerup
*/
void setup() {
  /*
  ** configure the activity LED
  */
  pinMode(PIN_ACTIVITY, OUTPUT);

  /*
  ** configure the buzzers in & out
  */
  for (int buzzer = 0; _buzzers[buzzer].pin_in; buzzer++) {
    if (_buzzers[buzzer].pin_in)
      pinMode(_buzzers[buzzer].pin_in, INPUT_PULLUP);
    if (_buzzers[buzzer].pin_out)
      pinMode(_buzzers[buzzer].pin_out, OUTPUT);
    _buzzers[buzzer].state = digitalRead(_buzzers[buzzer].pin_in);
  }
  buzzers_lightshow();

  /*
  **  get the modifier mode from the EEPROM
  */
  _mode_modifier = EEPROM.read(EEPROM_ADDR_MODIFIER);

  /*
  ** check if buzzer #0 is pressed
  */
  if (digitalRead(_buzzers[0].pin_in) == LOW) {
    /*
    ** if button #0 is pressed on startup,
    ** we will not enable the normal mode, but go to the
    ** programm mode
    */
    _mode_prog = 1;

    /*
    **  initialize serial communications
    */
    Serial.begin(9600);
    LogMsg("Starting up BuzzerKeyboard in ProgMode ...");
    LogMsg("Modifier read from EEPROM: %d (%s)", _mode_modifier, modifier_name(_mode_modifier));
    LogMsg("Setup done.");
  } else {
    /*
    **  initialize the keyboard interface
    */
    Keyboard.begin();
  }
}

/*
**  continous loop
*/
void loop() {
  DbgMsg("Buzzer: ProgMode:%d  ConfMode:%d", _mode_prog, _mode_conf);

  /*
  **  set/unset the activity LED
  **  depending on the mode
  */
  digitalWrite(PIN_ACTIVITY, ((!_mode_conf && !_mode_prog) || (millis() / ((_mode_conf) ? CONF_MODE_BLINK : PROG_MODE_BLINK)) % 2) ? HIGH : LOW);

  /*
  **  check if we have to enter the config mode
  */
  DbgMsg("Buzzer #0: state=%d  delta=%lu", _buzzers[0].state, millis() - _buzzers[0].last_change);
  if (!_mode_conf && _buzzers[0].state == LOW && _buzzers[0].last_change && millis() - _buzzers[0].last_change >= CONF_ENTER_DELAY) {
    LogMsg("ConfMode enabled");
    buzzers_blink_all_lights(5, 50);
    _mode_conf = 1;
  }

  /*
  **  scan the buttons
  **/
  int buzzer = buzzers_get_pressed();

  if (buzzer >= 0) {
    /*
    **  ok, we have a pressed button
    */
    digitalWrite(PIN_ACTIVITY, LOW);
    if (_mode_conf) {
      int changed_modifier = MODIFIER_INVALID;

      switch (buzzer) {
        case 1:
          /*
          **  set modifier to ALT
          */
          changed_modifier = MODIFIER_ALT;
          break;
        case 2:
          /*
          **  set modifier to ALT + SHIFT
          */
          changed_modifier = MODIFIER_ALTSHIFT;
          break;
        case 3:
          /*
          **  set modifier to ALT + CTRL
          */
          changed_modifier = MODIFIER_ALTCTRL;
          break;
        case 4:
          /*
          **  set modifier to NONE
          */
          changed_modifier = MODIFIER_NONE;
          break;
        default:
          /*
          **  leave the config mode
          */
          EEPROM.write(EEPROM_ADDR_MODIFIER, _mode_modifier);
          LogMsg("ConfMode disabled");
          _mode_conf = 0;
          buzzers_lightshow();
          _buzzers[0].last_change = 0;
          break;
      }
      if (changed_modifier != MODIFIER_INVALID) {
        /*
        **  acknowledge the configuration change
        */
        _mode_modifier = changed_modifier;
        LogMsg("ConfMode: modifier is now %s", modifier_name(_mode_modifier));
        buzzers_blink_light(buzzer, 5, 50);
      }
    } else if (!_mode_prog) {
      /*
      **  now set the modifier and send the key code
      */
      switch (_mode_modifier) {
        case MODIFIER_ALT:
          Keyboard.press(KEY_LEFT_ALT);
        case MODIFIER_ALTSHIFT:
          Keyboard.press(KEY_LEFT_ALT);
          Keyboard.press(KEY_LEFT_SHIFT);
          break;
        case MODIFIER_ALTCTRL:
          Keyboard.press(KEY_LEFT_ALT);
          Keyboard.press(KEY_LEFT_CTRL);
          break;
      }
      Keyboard.write(KEY_CODE_BASE + buzzer);
      delay(KEY_STROKE_DELAY);
      Keyboard.releaseAll();
    } else {
      /*
      **  we are in ProgMode, so imply log what we would have been done
      */
      LogMsg("ProgMode: sending key %s + 0x%x/char %c", modifier_name(_mode_modifier), KEY_CODE_BASE + buzzer, KEY_CODE_BASE + buzzer);
    }
  }
  if (DBG)
    delay(100);  // throttle the serial output a little bit
} /**/