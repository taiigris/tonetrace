# Tonetrace website

Static product page, published to GitHub Pages at https://taiigris.github.io/tonetrance/.
`index.html` plus `assets/`. No build step, no external fonts, scripts or trackers.

## Preview locally

```bash
cd site && python3 -m http.server 8765
# open http://localhost:8765
```

## Publishing

`.github/workflows/pages.yml` deploys this folder whenever a push to `main` changes `site/`
(or by hand: Actions → *Deploy website* → *Run workflow*).
One-time setup: repo Settings → Pages → Source: **GitHub Actions**.

## Release a download

1. Build: `./build.sh --release-zip` (creates `dist/Tonetrace-<version>-macOS.zip`).
2. On GitHub: Releases → *Draft a new release*, tag `v<version>` (e.g. `v5.1.0`),
   attach the ZIP and paste its checksum (`shasum -a 256 dist/Tonetrace-*.zip`) into the notes.
3. Update `RELEASE.version` at the bottom of `index.html` and push.
   The download buttons, version labels and install commands follow it.

The download link has the form
`https://github.com/taiigris/tonetrance/releases/download/v<version>/Tonetrace-<version>-macOS.zip`,
so the tag and file name must match exactly.

## Updating screenshots

`./build.sh` regenerates the screenshots in `docs/`. Copy the ones used here:

```bash
cp docs/screenshot.png docs/screenshot-instruments.png docs/screenshot-done.png site/assets/
```
