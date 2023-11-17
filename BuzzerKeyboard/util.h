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

#if DBG
/*
**  debug message
*/
#define DbgMsg LogMsg
#else
#define DbgMsg(...)
#endif


/*
**  log a smessage to serial
*/
void LogMsg(const char *fmt, ...);

/**/
