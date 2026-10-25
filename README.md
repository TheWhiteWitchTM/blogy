# Blogy

A black Markdown editor for GitHub Pages blogs. Write a post, watch the preview, fix the front matter in a form, publish with git, tell Bluesky, and ask Grok for help. Built with Qt 6, in the same shape as Cinder and Vesper.

Blogy is generic: any Jekyll site (collections or `_posts`) or a Next.js MDX blog works once it is described in the config. The example config ships with three sites filled in (`whitewitch`, `mira`, `TheWhiteWitchTM.github.io`) that you can keep, edit, or delete.

## Open it

```bash
cd ~/Projects/blogy
scripts/build.sh
./blogy
```

`./blogy` runs the bundled `dist/` copy when it exists, else `build/blogy` (building it first if needed). Pass a post to open it:

```bash
./blogy ~/Projects/whitewitch/collections/_blog/2026-10-22-retro-center.md
./blogy --self-test
```

## Install (no sudo)

```bash
cd ~/Projects/blogy
./install.sh             # builds + bundles dist/ if missing, then installs
./install.sh --uninstall # removes it again
```

`install.sh` copies `dist/` to `~/.local/share/blogy`, writes a `~/.local/bin/blogy` launcher, installs `blogy.desktop` into `~/.local/share/applications` and the icon into `~/.local/share/icons/hicolor`. Blogy then shows up in the COSMIC app grid. If `~/.local/bin` is not on your PATH (Pop!_OS normally has it), the script says what to add to `~/.profile`. Uninstall keeps `~/.config/blogy`.

The same `install.sh` sits inside `dist/`, so a copied `dist/` folder installs on another machine without Qt.

## Build dependencies (Pop!_OS 22.04 / 24.04, Ubuntu)

```bash
sudo apt install build-essential qt6-base-dev qt6-base-dev-tools qt6-wayland \
    libgl1-mesa-dev git librsvg2-bin
# optional: cmake (CMakeLists.txt is an alternative to qmake)
```

No WebEngine: the preview uses Qt's built-in Markdown renderer. `librsvg2-bin` is only needed to re-export the icon PNGs from `resources/blogy.svg`.

`scripts/build.sh` uses `~/Qt/6.12.0/gcc_64/bin/qmake` when present, else `qmake6`, else `qmake`. With CMake instead:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
```

## dist/

```bash
scripts/bundle-dist.sh
./dist/blogy
```

Same layout as Cinder: `dist/blogy` is the launcher, `dist/bin` the binary (+ `qt.conf`), `dist/lib` the Qt libraries it links, `dist/plugins` the platform (xcb, wayland, offscreen), image, icon, input, and TLS plugins (HTTPS for Grok and Bluesky), and `dist/share` the icons and desktop file. It runs on a machine that has no Qt installed.

## What it does

- Sites in the sidebar, posts grouped by collection, newest first.
- Markdown editor on a black page. It opens as the editor: Preview and Tools stay closed until you ask for them. The posts list on the left hides with the arrow at its top, and that arrow brings it back. Highlighting covers front matter, headings, emphasis, code, links, and Liquid tags. A fenced code block is colored for the language on its opening line. Lists continue on Enter. The top row is words, each button sized to its label: New Post, Open, Save, Publish, Post to Bluesky, Preview, Tools, and Dictate. Preview fills the text area. Tools opens the side pane and stays lit while that pane is open. The pane has three tabs: Chat, Front matter, and Publish. Settings is in the File menu. Help is in the Help menu. The format row under it is symbols: B, I, strike, lists, link, picture, an H menu (and a jump list of the headings in the post), and a code-block menu that inserts a fenced block tagged with the language. Hover a symbol for its name. Ctrl+B, Ctrl+I, Ctrl+K, Ctrl+` for bold, italic, link, code. Ctrl+1 through Ctrl+6 set the heading.
- Spelling and Tab completion, in the LibreSky shape. After two letters a list opens under the caret and the rest of the best word is shown in grey. Tab takes it. A lowercase word the dictionary does not know is underlined once you leave it. Right-click for another spelling, or add the word. Names and code are left alone.
- Dictate (Ctrl+Shift+D) is a toggle on the top row. Click to listen, click again to stop. It listens through the xAI speech API and writes Markdown: “heading two”, “bullet”, “bold … end bold”, “code block python”, “new paragraph”, “scratch that”, “finish the post”. “Finish the post” (also “write the post”) takes the outline in the editor and the site’s tune and writes the full post. The prompt box can change that; leave it empty to write from what is already there. Dictate uses the signed-in Grok account, or the xAI key. The microphone audio goes to xAI only while Dictate is on.
- Find (Ctrl+F) and Replace (Ctrl+H). Help (F1) is the writing guide.
- Live preview (Ctrl+Shift+V) fills the text area, with the front matter shown as a header. Close it and the editor is back. Tools (Ctrl+J, and Ctrl+Shift+M) opens the side pane, and the same key closes it. The button stays lit on every tab. Front matter (Ctrl+M) opens that pane on the form. The Publish tab shows the repo, branch, file, and the git command.
- Front matter form built from the post's own keys: text, yes/no, and list fields; add or remove keys. Edits go straight into the file. MDX posts use `export const metadata = { ... }`.
- New Post (Ctrl+N): pick site and collection, type a title; the slug, file name (`2026-10-25-my-title.md`, or `my_title/page.mdx` for MDX), front matter, and future URL come from the site config.
- Save names the file from the title, using the collection's pattern. The date already in the file name stays. A collection that lives in `_posts` is GitHub Pages' default Jekyll engine, so the name is prefixed with the date (`2026-10-25-my-title.md`) even when the pattern forgot `{date}`.
- Publish (Ctrl+Shift+P): saves, then runs `git -C <repo> add -- <post>`, `git -C <repo> commit -m`, and `git -C <repo> push <remote> HEAD:<branch>` in the site's clone. The Publish tab shows that command, filled in from the site, before and while it runs. A rename is committed with the old path removed. Output is in the Publish tab, followed by the post URL. Your own git credentials (ssh agent, credential helper, `gh auth`) do the push; Blogy never handles a GitHub token. `GIT_TERMINAL_PROMPT=0` keeps git from hanging on a prompt.
- Each site can have a signature in Settings. Publish appends it to the end of the post, once. Each site also has a tune: the voice Grok uses on every request for that site, including Finish the post.
- After a publish: Post to Bluesky? The dialog is prefilled with the title and URL, counts graphemes against the 300 limit, and adds a link card. Also under File → Post to Bluesky (Ctrl+Shift+B).
- Chat (inside Tools, Ctrl+J): a Grok chat that sees the post. Type in the prompt at the bottom (a large field; Ctrl+Enter sends). Finish the post reads the draft, uses the site tune, and streams the file into the editor. Leave the prompt empty to write from what is already there, or type how to change it. Other jobs: polish selection, front matter, title + tags, Bluesky blurb. A question stays in the chat; Apply to editor puts that reply in the post. The site's tune is sent with every request.

## Config

`~/.config/blogy/config.json`, written on first start from `config.example.json` with empty secrets, mode 0600. Window layout lives next to it in `blogy.conf`. Both stay out of every repo: Blogy refuses to write a config file inside a git work tree, and a `BLOGY_CONFIG` or `--config` path that points into one is ignored in favor of `~/.config/blogy/config.json`. `.gitignore` also excludes `config.json`, `blogy.conf`, `*.local.json` and `.env` in case one lands in the tree.

```json
{
  "editor":  { "fontSize": 13, "wrap": true, "previewDelayMs": 250 },
  "grok":    { "endpoint": "https://api.x.ai/v1/chat/completions", "model": "grok-4", "apiKey": "" },
  "bluesky": { "service": "https://bsky.social", "handle": "", "appPassword": "" },
  "sites": [
    {
      "name": "My blog",
      "type": "jekyll",
      "path": "~/Projects/my-blog",
      "branch": "main",
      "remote": "origin",
      "url": "https://username.github.io/my-blog",
      "slugSeparator": "-",
      "commitMessage": "Post: {title}",
      "signature": "— Your name\nhttps://username.github.io",
      "collections": [
        { "name": "posts", "dir": "_posts", "file": "{date}-{slug}.md", "url": "/{year}/{month}/{day}/{slug}.html" }
      ],
      "frontMatter": { "layout": "post", "title": "", "date": "{date}", "description": "", "tags": [] },
      "template": "## {title}\n\n"
    }
  ]
}
```

- `type`: `jekyll` (YAML between `---`) or `mdx` (`export const metadata`).
- `url`: the public base URL including `baseurl`, e.g. `https://thewhitewitchtm.github.io/whitewitch`.
- `collections[].dir` is relative to the clone. For Jekyll collections with `collections_dir: collections` that is `collections/_blog`. `file` and `url` take `{date} {year} {month} {day} {slug} {category}` (`{category}` is the collection name). Jekyll's default collection URL is `/<collection>/<file name>.html`; `_posts` defaults to `/{year}/{month}/{day}/{slug}.html` without categories. Match your `permalink:` if you set one.
- `collections[].frontMatter` adds keys for one collection (e.g. `video` for videos).
- `signature`: Markdown appended to the post on publish. Set it in Settings, one signature per site. Empty means nothing is added.
- Sites can be edited in Settings (one JSON block per site, plus the signature box) or in the file.

### Secrets

| What | Config key | Environment (wins) |
| --- | --- | --- |
| xAI API key | `grok.apiKey` | `XAI_API_KEY` |
| Bluesky handle | `bluesky.handle` | `BLUESKY_HANDLE` |
| Bluesky app password | `bluesky.appPassword` | `BLUESKY_APP_PASSWORD` |

Use a Bluesky **app password** (Settings → Privacy and security → App passwords), never the account password. Secrets are only sent in request bodies or headers to the configured endpoints and are never logged or shown; the Settings fields are masked.

## How Bluesky posting works

1. `POST {service}/xrpc/com.atproto.server.createSession` with `identifier` + app password → `accessJwt`, `did`.
2. `POST {service}/xrpc/com.atproto.repo.createRecord` with `repo: did`, `collection: app.bsky.feed.post`, and a record holding the text, `createdAt`, link and hashtag facets (UTF-8 byte offsets), and an `app.bsky.embed.external` card (URL, title, description).
3. The dialog shows the `bsky.app` link of the new post.

## How Grok works

With the Grok CLI signed in, Blogy runs that (`--prompt-file`, tools turned off, streaming text). A request to write the post streams into the editor. Other replies stay in the AI pane until you apply them. Otherwise `POST https://api.x.ai/v1/chat/completions` (OpenAI-compatible) with `Authorization: Bearer <key>`, the model from the config (`grok-4.7` by default; any model name xAI offers works), a system prompt, the conversation, and the post text for the quick jobs. The site tune is part of the system prompt.

## Layout

```
blogy.pro / CMakeLists.txt   qmake (used by scripts/build.sh) or CMake
src/main.cpp                 theme, command line, --self-test
src/config.*                 config.json, sites, secrets + env overrides
src/settings.*               window state (QSettings)
src/frontmatter.*            YAML / MDX metadata parse + write
src/post.*                   slugs, file names, URLs, new-post template
src/highlighter.* editor.*   Markdown highlighting and editing
src/preview.*                live preview
src/git.*                    git add / commit / push via QProcess
src/bluesky.*                AT Protocol client
src/grok.* grokauth.*       xAI chat client, and the signed-in Grok token used for dictation
src/ui/                      main window, front matter form, chat panel, dialogs
resources/                   blogy.qss (Cinder's black + ember theme), icon SVG/PNGs
scripts/build.sh             build into build/
scripts/bundle-dist.sh       build dist/ with Qt libs + plugins
install.sh                   per-user install / --uninstall
share/blogy.desktop          desktop entry
```

## Keys

Ctrl+N new post. Ctrl+O open. Ctrl+S save. Ctrl+Shift+P publish. Ctrl+Shift+B post to Bluesky. Ctrl+J Tools (Ctrl+Shift+M too). Ctrl+M front matter. Ctrl+Shift+D dictate. Ctrl+Shift+V preview. Ctrl+, settings. Ctrl+B / Ctrl+I / Ctrl+K / Ctrl+` bold, italic, link, code. Ctrl+Enter sends a chat message.

Blogy is MIT licensed. See `LICENSE`.
