# Known issues

What is still open in the Ghost Bear's Legacy port, in plain language.

- **Windows is tested only under Wine.** The game, menus, folder picker, sound and saved settings work there with
  Vulkan, but it has not been run on a real Windows PC, and the Direct3D 12 renderer has never drawn a frame (Wine can't
  run it). If Direct3D 12 fails to start, the game falls back to Vulkan.
- **The VFX1 head tracker** (Cockpit Controls) can be selected and is saved as the original does, but the port can't
  read the headset itself.
- **TITANIUM lighting in JESSSCN1 is a guess.** The Titanium Edition has no lighting entry for that mission (it keeps
  whatever the previous mission set), so the port uses plain daylight.
- **Small details traced from the code but not yet seen side by side with the original:** how the burning effect
  animates on a 'Mech, the hovercraft's exact hover height, the dropship parts in some scenes, and a few moving
  objects (dropships, lifts) other than those in HOFF, CLAI, JESS and BRAN.
- **Randomness:** fights depend on random rolls, so no two runs match the original exactly; comparisons are made over
  several runs.
