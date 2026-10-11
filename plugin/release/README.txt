Take a Walk for Euro Truck Simulator 2
Get out of your truck and walk around on foot.
https://github.com/adamvirtualspace-lab/takeawalk

For ETS2 1.61.1.1, Windows, 64-bit. Single player only.


INSTALL

1. Close the game.
2. Extract the whole zip somewhere.
3. Double-click install.bat. It finds the game through Steam and copies the files
   into its bin\win_x64\plugins folder. If Windows asks whether to run it, allow it.

By hand instead: copy everything inside the "plugins" folder of this zip into
   <Steam library>\steamapps\common\Euro Truck Simulator 2\bin\win_x64\plugins
(create the "plugins" folder there if the game has none).

When the game starts it shows a notice that "advanced SDK features" were detected.
That is the game telling you a plugin is installed. Press OK.


USE

Park, set the parking brake, then:

  F9         get out; press again at the driver's door to get back in
  Hold F9    go back to the cab from anywhere
  W A S D    walk          Shift   run
  Ctrl       crouch        Space   jump
  Mouse      look around

The panel in the bottom left corner reminds you how to get back in.
Settings are in takeawalk.ini next to takeawalk.dll; each one is explained there.


REMOVE

Double-click uninstall.bat, or delete takeawalk.dll, takeawalk.ini and the
"takeawalk" folder from the game's bin\win_x64\plugins folder.


GOOD TO KNOW

- Made for game version 1.61.1.1. After a game update, parts of it may switch
  themselves off or the game may crash at start. Remove it and look for a new
  release.
- If something does not work, the game's log says why: search
  Documents\Euro Truck Simulator 2\game.log.txt for "takeawalk".
- It changes nothing in your save. While you are on foot it hides the speedometer
  and switches the game's developer mode on (the walking camera needs it), and puts
  both back when you get in.

The README on GitHub has the full details.
