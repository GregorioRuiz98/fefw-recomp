# Controller guide

Connect your controller to Windows and open **fefw.exe**. Detection is automatic; you can also connect it while the game is running. A controller is required for gameplay. Keyboard and mouse work only in the setup tool, host settings and shortcuts (F1, F2, Alt+Enter and closing the window).

The game searches for controllers at startup and checks the device list every second to recover from missing connection notifications. DualShock 4 uses SDL’s HID driver over USB and when exposed by Windows over Bluetooth. Keyboard keys, mouse buttons, movement and the wheel do not send game actions. Old keyboard/mouse bindings in controls.ini cannot re-enable them; their gameplay settings tabs are hidden.

If the controller does not respond, open **F1 → Settings → Controls → Gamepad** and press **Rescan controllers**. That section shows the device name and status. Close F1 to return button input to the game; input is deliberately held back while the settings overlay is open. You can also reconnect the cable and wait a second.

| Game action | Xbox | PlayStation | Nintendo Pro |
| --- | --- | --- | --- |
| A / confirm | A | Cross | A |
| B / back | B | Circle | B |
| X | X | Square | X |
| Y | Y | Triangle | Y |
| L / R | LB / RB | L1 / R1 | L / R |
| ZL / ZR | LT / RT | L2 / R2 | ZL / ZR |
| PLUS / pause or skip video | Menu / Start | Options | + |
| MINUS | View / Back | Create / Share | − |
| D-pad | D-pad | D-pad | D-pad |
| Sticks and stick clicks | Both sticks | Both sticks | Both sticks |

The four face buttons follow their printed labels when SDL identifies them. PlayStation uses the equivalents listed above. Prompts drawn by the game retain their original symbols.

Settings are stored in **controls.ini**, in `[pad]` and `[padmap]`. Runtime settings also open with **F1**. Both sticks default to a 10% deadzone; triggers activate at 35% travel. Rumble and sensors are enabled when available, but their physical behavior in this game has not been verified.

Disconnecting releases buttons and sticks. Reconnect a controller to continue playing. If a controller does not respond, check `fefw.log` for a `gamepad ... added` entry with its name. Compatibility depends on Windows and SDL recognizing the device.

The complete SDL → service-driven input → HID path has been tested with virtual Xbox, DualShock 4 (two revisions), DualSense and Nintendo Switch controllers, including buttons, triggers, sticks, deadzones and connection/disconnection. An additional regression checks a preconnected DualShock 4, missing notifications and duplicate prevention. The standalone configurator has also detected a physical USB DualShock 4 (Sony 054C:09CC) and read its idle sticks and triggers. Physical button presses, cable reconnection during gameplay, rumble, sensors and a full playthrough remain outstanding.

In **fefw_setup.exe → Controllers**, inspect connected devices and raw input, rescan and adjust each stick’s deadzone. Model profiles store deadzones under `controller-profiles/<SDL-GUID>.ini`. Loading a profile leaves values pending until **Save**; it does not switch bindings or apply profiles automatically during a game. In the live test, SDL button names such as `a` refer to the mapped face buttons in the table above.
