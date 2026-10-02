# Using Epic Games' Paragon assets in this project

This page explains which Epic Games content the project uses, what Epic's terms say about it, how the repository
complies with them, and what must change before the project is shown under its own name. The terms were checked
on **2026-10-02** against Epic's Fab listing for a Paragon pack and the Fab license agreement. Terms can change, so
re-check the sources below before any release. **This is a summary for orientation, not legal advice.**

## 1. What the project uses

| Content | Publisher, where it comes from | How the project uses it |
|---|---|---|
| **Paragon hero packs**: Greystone, Countess, Gideon, Sparrow, Kwang, Crunch, Iggy & Scorch, Khaimera, Morigesh, Revenant, Sevarog | Epic Games, free on Fab (first released in 2018 when Paragon closed) | character models, skins, animations and animation blueprints, ability effects, voice lines |
| **Paragon: Minions** | Epic Games, free on Fab | the lane minions (Dawn and Dusk), their animations and effects |
| **Paragon: Agora and Monolith** | Epic Games, free on Fab | environment props and floor materials of the Arena and Conquest maps |
| **Open World Demo Collection** (`KiteDemo`) | Epic Games, free on Fab | trees, plants and ground tiles |
| Unreal Engine 5.8 templates and engine content | Epic Games, part of the engine | mannequins and prototyping blocks for the prototypes, basic shapes, engine fonts |

All of it lives in the developer's local project (`Content/Paragon*/`, `Content/KiteDemo/`, the template folders),
added from their own Fab library. None of it is in this repository (see section 3).

## 2. What Epic's terms say

### The Paragon listings on Fab

Epic's own listing for a Paragon pack (*Paragon: Morigesh*, publisher **Epic Games**) states:

> "Morigesh includes the character model, animations, AnimBP's, skins and FX released to the Unreal Engine community
> for free to use as you like in your Unreal Engine projects!"
>
> "You may not use the trademark PARAGON to advertise or name your game."

The listing's license terms are the **Fab Standard License**, and its *"allows use with AI"* field is **No** (Fab's
"NoAI" flag). The other Paragon packs carry the same wording. On the former Unreal Engine Marketplace the packs also said
they were *"Licensed for use only with and under the same terms as the Unreal Engine"*. This project treats them as
Unreal-Engine-only, and they are used only in Unreal Engine.

### The Fab Standard License (Fab EULA)

In summary, with the agreement's sections:

- **Allowed:** use the content commercially or privately; modify it to fit a project; distribute (also commercially)
  a project that incorporates it; share the content with collaborators who work on the project, directly, **through
  a private repository** or inside the project. Crediting the publisher is not required.
- **Not allowed:** resell or redistribute the content on its own, even for free.
- **§6(a) Incompatible licenses:** Standard-License content may not be combined with code or content under a license
  that would require the content to be under other terms, for example the GPL, the LGPL (except dynamic linking to a
  shared library) or Creative Commons Attribution-ShareAlike.
- **§6(b) General restrictions** include: no reverse engineering or extracting source data from the content; no
  "stand-alone" distribution (a distributed project must add value beyond the content itself, and the content must
  not be its main purpose); no letting third parties put the content into their own products (e.g. through level
  editors, templates or modelling tools that export it); no removing proprietary notices; and, for **NoAI** content,
  no use *in datasets used by generative AI programs*, *in the development of generative AI programs*, or *as input for
  training generative AI programs* (definitions in §16(l)).

## 3. How this repository complies

- **No Epic content is distributed.** The repository holds only the project's own work: C++ source, data
  (`heroes.json`), the four maps, the project's own materials and icons, fonts under the OFL, tools and docs. The
  current tree **and the whole git history** were checked: no `Content/Paragon*`, `KiteDemo` or template folder was
  ever committed, and `.gitignore` excludes them. The derived copies the tools generate from the packs (hit reactions
  and fade materials in `Content/Arena/Generated/`) are excluded too.
- **The repository points at Epic content; it does not contain it.** Code, data and the maps refer to pack assets by
  their paths. Anyone who builds the project adds the free packs from their own Fab library, as the README explains.
- **The project adds its own value.** It is a game with its own code, rules, maps, bots and tools, not an asset pack.
- **The project's own license must not be copyleft** (§6(a)): no GPL, LGPL or CC BY-SA for the code. A permissive
  license (MIT or Apache-2.0) or an "all rights reserved" notice is compatible.
- **Videos and screenshots** of the running game show the content to present the project. Like the game itself, they
  must not use the PARAGON trademark in the project's name or advertising (section 4).

## 4. The name: Tartarus Arena

The working title **"Paragon Arena"** used the trademark the listings forbid ("You may not use the trademark PARAGON
to advertise or name your game"). On 2026-10-02 the owner chose the public name **Tartarus Arena**. The rename covered:

| Where | Status |
|---|---|
| the GitHub repository's name and description | the public repository is created under the new name |
| the README's title and the docs' headings | done (the design doc and QA 24 record the working title as history) |
| the in-game title (main menu, hero select, loading screen, the prototypes' back button), the window title and the project name, the teasers' logo card | done in code, config and `Tools/Teaser/titles.py`. Packaged builds and teaser videos made before 2026-10-02 still show the old title until they are rebuilt or re-rendered |
| the project and module names (`ParagonArena.uproject`, the `ParagonArena` C++ module) | kept as internal identifiers (the owner's choice). They are not used to name or advertise the game, and the README explains them |

What stays fine:

- **Factual credit:** "built with Epic Games' free Paragon assets", plus the disclaimer that the project is unofficial
  and not affiliated with or endorsed by Epic Games. This describes the source of the content; it does not name the game.
- **The heroes' names** (Greystone, Countess…): the listings do not mention them, and Predecessor, a commercial game
  built on the Paragon assets under its own name, kept them. Community guidance on Epic's forums adds the sensible
  limit: do not present the game as Paragon, a Paragon sequel or a continuation of the Paragon IP.

## 5. AI and the "NoAI" flag

The Paragon listings are flagged **NoAI**: their content may not go into datasets for generative AI, into developing
generative AI programs, or into training them. How this project relates to that:

- Claude (Anthropic's Claude Code) wrote the code, the tools, the data and the docs. It did not train, fine-tune or
  build a dataset with the Paragon content, and no generative AI produced or altered the assets.
- During verification Claude looked at screenshots of the running game, which show Paragon characters, to check its
  own work (labs, UI screens, teaser frames). That is reviewing output, not training.
- **Safeguard for the account owner:** make sure the Claude account used does not allow your conversations to be used
  for model training (on consumer plans this is a privacy setting; Anthropic's commercial and API terms do not train
  on inputs by default). Then screenshots that show the content cannot become training input.
- Do not publish frames, renders or screenshots of the assets as an image collection or dataset. The repository
  contains none; its icons are game-icons.net drawings.

## 6. Checklist before the repository goes public

- [x] A new name for the game: **Tartarus Arena**, in the README, the docs, the in-game title and the teaser script (section 4).
- [x] A `LICENSE` that is not copyleft: **MIT**, scoped to the project's own work (section 3).
- [x] The git history reviewed: no Epic content and no secrets in any revision. The public repository starts from one
      initial commit (the owner's choice); the full development history stays in the private repository.
- [ ] The Claude account's model-training setting checked (section 5).
- [x] The README's disclaimer kept: *Paragon is a trademark of Epic Games, Inc.; this project is not affiliated with
      or endorsed by Epic Games.*
- [ ] Packaged builds and teasers published, if at all, only under the new name.

## Sources

- Fab listing *Paragon: Morigesh* (publisher Epic Games, license: Standard License): https://www.fab.com/listings/29e67175-fa08-448f-822b-37f411530749
- Fab End User License Agreement and the Standard License summary (last updated 2024-10-01): https://www.fab.com/eula
- Epic Games' announcement of the free Paragon assets (2018): https://www.unrealengine.com/fr/blog/epic-games-releases-12-million-worth-of-paragon-assets-for-free
- The Marketplace-era wording ("Licensed for use only with and under the same terms as the Unreal Engine. You may not
  use the trademark PARAGON"), quoted on Epic's forums: https://forums.unrealengine.com/t/fab-ue-only-content-licensing/2082870
- Community guidance on names and commercial use (not an official Epic statement): https://forums.unrealengine.com/t/can-you-use-the-paragon-assets-for-your-own-commercial-game/2077124
- Predecessor, built on the Paragon assets by Omeda Studios: https://en.wikipedia.org/wiki/Predecessor_(video_game)
