# Safety

A diesel heater is a combustion appliance. This project can switch one on while nobody is in the room. Treat that
seriously.

- Use only heaters that are installed correctly (exhaust leading outside, fuel line and fuel tank as the manufacturer
  describes, nothing flammable near the intake or the exhaust, working carbon monoxide alarm in the room).
- The heater has its own safety logic (overheat sensor, flame sensor, cool-down run). The controller never overrides it and
  only sends the same buttons the remote sends. Do not rely on this software as a safety device.
- Smart mode starts the heater by itself when the room is cold. Do not use it in a space where an unexpected start is a
  problem (a closed tent, a room with fuel vapours, a place with flammable materials stored close to the heater).
- Power off is always allowed in the panel, in every state, and interrupts whatever the controller is doing. Every other
  command is checked twice (by the server and by the controller) against the last state the heater reported.
- The panel is protected by one password and a session cookie. Serve it over HTTPS only, use a long password, and keep the
  server up to date.
- If the controller loses the heater (no answers), it decides nothing; if it loses the room sensor while smart mode heats,
  it switches the heater off. If the controller itself loses power or WiFi, the heater keeps doing what it was doing, so
  switch it off at the heater when you leave for a long time.
- Test every new heater model by hand first (the console commands `v1 poll`, `v1 rawpoll`, `v1 menu`), with the heater in
  view.
