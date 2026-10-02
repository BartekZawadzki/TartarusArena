# Media

The teaser and stills for presenting Tartarus Arena, for example in a post about a game built with Claude and the
Dark Factory Patterns. Everything here was rendered by the game itself and edited by the project's own scripts
([Tools/Teaser/](../Tools/Teaser/)).

| File | What it is | Good for |
|---|---|---|
| [`TartarusArena_Teaser.mp4`](TartarusArena_Teaser.mp4) | the teaser: 59 s, 1920×1080, 30 fps, H.264 + AAC, ~36 MB | posts, embedding, a quick look. The full-quality master (~133 MB, CRF 17) is attached to the repository's release |
| [`preview.gif`](preview.gif) | 9 s of highlights, 560 px, a loop | the README, previews where video does not play |
| [`poster.jpg`](poster.jpg) | the logo frame, 1600×900 | a thumbnail or link card |
| [`screens/`](screens/) | six stills, 1280×536 (2.39:1) | galleries, articles |

## How the teaser was made

1. **Filming.** The game's own director (`-ArenaTeaser=<scene>`) recorded bot matches frame by frame with a fixed
   1/30 s step. Its fight camera orbits 9.5 m from the action and 2 m above it, it can film in slow motion, and it
   finds the hottest fight by itself. The cut also uses fragments of the operator's own play, cropped so the HUD
   sits under the 2.39:1 bars.
2. **Music.** It is synthesised from scratch in Python and numpy, with no samples. A fantasy game theme at 100 BPM
   (Karplus–Strong harp, flute, horn, choir, frame drums) plays under the gameplay. Cinematic stingers (braam,
   taiko, choir, bells) play on the countdown, on *FIGHT!*, on the Power of Tartarus and on the logo.
3. **Titles.** A 3D gold logo in Cinzel, rendered in Blender 5.2: letters flip in one after another and a light
   sweeps across the metal.
4. **Motion design.** The edit is built by a script in Blender's sequencer. It has kinetic type on the drum hits,
   the shop as a framed panel, a three-way split screen, lower thirds, dissolves, a colour grade and a vignette.

## Crediting and naming

- Name the game **Tartarus Arena**, not "Paragon". Epic's asset listings say *"You may not use the trademark PARAGON
  to advertise or name your game"* ([docs/USING-PARAGON-ASSETS.md](../docs/USING-PARAGON-ASSETS.md)).
- Credit the art. The teaser ends with this line, and a post should carry it too: *"Built with Epic Games' free
  Paragon assets. Not affiliated with or endorsed by Epic Games."*
- The images and the video show Epic Games' content. The project's MIT license does not cover that content, and
  these files are for presenting the project.
