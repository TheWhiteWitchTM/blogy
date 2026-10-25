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
- Markdown editor on a black page: highlighting for front matter, headings, emphasis, code, links, Liquid tags. Lists continue on Enter. Ctrl+B, Ctrl+I, Ctrl+K, Ctrl+` for bold, italic, link, code.
- Live preview (Ctrl+Shift+V) with the front matter shown as a header.
- Front matter form built from the post's own keys: text, yes/no, and list fields; add or remove keys. Edits go straight into the file. MDX posts use `export const metadata = { ... }`.
- New Post (Ctrl+N): pick site and collection, type a title; the slug, file name (`2026-10-25-my-title.md`, or `my_title/page.mdx` for MDX), front matter, and future URL come from the site config.
- Publish (Ctrl+Shift+P): saves, then `git add <post>`, `git commit -m`, `git push <remote> HEAD:<branch>` in the site's clone. Output is in the Publish tab, followed by the post URL. Your own git credentials (ssh agent, credential helper, `gh auth`) do the push; Blogy never handles a GitHub token. `GIT_TERMINAL_PROMPT=0` keeps git from hanging on a prompt.
- After a publish: Post to Bluesky? The dialog is prefilled with the title and URL, counts graphemes against the 300 limit, and adds a link card. Also under File → Post to Bluesky (Ctrl+Shift+B).
- Talk to AI (Ctrl+J): a Grok chat that sees the post. One-click jobs: polish selection, front matter, title + tags, Bluesky blurb. Apply to editor replaces the selection (or the front matter block for the front matter job).

## Config

`~/.config/blogy/config.json`, written on first start from `config.example.json` with empty secrets, mode 0600. It is outside every repo, and `.gitignore` excludes `config.json`, `*.local.json` and `.env` in case one lands in the tree. `BLOGY_CONFIG=/path/to/file.json` or `--config file.json` uses another file. Window layout lives next to it in `blogy.conf`.

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
- Sites can be edited in Settings (one JSON block per site) or in the file.

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

`POST https://api.x.ai/v1/chat/completions` (OpenAI-compatible) with `Authorization: Bearer <key>`, the model from the config (`grok-4` by default; any model name xAI offers works), a system prompt, the conversation, and the post text for the quick jobs.

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
src/grok.*                   xAI chat client
src/ui/                      main window, front matter form, chat panel, dialogs
resources/                   blogy.qss (Cinder's black + ember theme), icon SVG/PNGs
scripts/build.sh             build into build/
scripts/bundle-dist.sh       build dist/ with Qt libs + plugins
install.sh                   per-user install / --uninstall
share/blogy.desktop          desktop entry
```

## Keys

Ctrl+N new post. Ctrl+O open. Ctrl+S save. Ctrl+Shift+P publish. Ctrl+Shift+B post to Bluesky. Ctrl+J Talk to AI. Ctrl+M front matter. Ctrl+Shift+V preview. Ctrl+, settings. Ctrl+B / Ctrl+I / Ctrl+K / Ctrl+` bold, italic, link, code. Ctrl+Enter sends a chat message.

Blogy is MIT licensed. See `LICENSE`.
