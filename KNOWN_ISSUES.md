# Known issues

What is still open in the Ghost Bear's Legacy port, in plain language.

- **Windows is tested only under Wine.** The game, menus, folder picker, sound and saved settings work there with
  Vulkan, but it has not been run on a real Windows PC, and the Direct3D 12 renderer has never drawn a frame (Wine can't
  run it). If Direct3D 12 fails to start, the game falls back to Vulkan.
- **The legacy Windows build (32-bit, OpenGL 1.2) is untested on its target.** Its renderer is tested on Linux against
  Mesa's OpenGL (llvmpipe), where it draws the same frames as the normal build to a few pixels, and the 32-bit
  simulation plays every mission as the 64-bit one does; but it has not run on Windows XP, on a real OpenGL 1.x driver,
  or on a CPU of that era. Expected risks: old drivers' OpenGL 1.1 quirks, z-fighting with a 16-bit depth buffer, and the
  CPU cost of the vertex stage and the DOS look's exact fill, both worked out on the CPU every frame.
- **The VFX1 head tracker** (Cockpit Controls) can be selected and is saved as the original does, but the port can't
  read the headset itself.
- **TITANIUM lighting in JESSSCN1 is a guess.** The Titanium Edition has no lighting entry for that mission (it keeps
  whatever the previous mission set), so the port uses plain daylight.
- **TITANIUM insignia:** as in the Titanium Edition itself, the player's star wears the Wolf crest and enemies wear the
  base game's clan crests. Titanium's GBL missions use the base game's crest set; the port keeps that rather than
  substituting GBL's own Ghost Bear crest (the DOS and ENHANCED looks show the Ghost Bear crest, from GBL's data).
- **Small details traced from the code but not yet seen side by side with the original:** how the burning effect
  animates on a 'Mech, the hovercraft's exact hover height, the dropship parts in some scenes, and a few moving
  objects (dropships, lifts) other than those in HOFF, CLAI, JESS and BRAN.
- **Randomness:** fights depend on random rolls, so no two runs match the original exactly; comparisons are made over
  several runs.
