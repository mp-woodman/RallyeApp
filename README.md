# RallyeApp
Hilfsmittel zum Training für Oldtimer gleichmäßigkeits Rallyes.
Bestehend aus zwei Komponenten, der Lichtschranken/Schlauch-Einheit mit DCF77 Funkzeitempfänger(lightbarier_esp) und dem Auswertegerät mit paperwhite display und akustischem Signalgeber im Oldtimer Cockpit(CAR_BLE_Client).
Dier Bieden Geräte sind über Bluetooth (BLE) verbunden, Wenn man mit dem Oldtimer dich Lichtschanke auslöst, ertönt ein Akustisches Signal im Auswertegerät und kurz danach wird einem die Funkzeit der Druchfahrt angezeigt. 

## IDE
- Arduino IDE 2.0
  - Board manager
      - Arduino esp32 Boards v2.0.18
  - Library Manager
      - Adafruit BusIO by Adafruit v1.17.0
      - Adafruit GFX Library by Adafruit v1.12.0
      - DCF77 by thijs Elenbaas v1.0.0
