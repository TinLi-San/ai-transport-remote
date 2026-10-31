<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Fork assets — AI Transport

Fork-specific material for this application. Upstream keeps `docs/assets/` empty on
`main`; the files here exist only on this branch and are never synced back upstream.

## Contents

| Path | What it is |
| --- | --- |
| `cover/` | Source of the community cover: `cover.html` + `tokens.css` + `cover.css`. |
| `images/cover-ai-transport.png` | Rendered 1200×630 cover used by the root README and the community listing. |

## Re-render the cover

The cover is a static HTML page; the PNG is a headless-browser screenshot. No build
step and no dependencies beyond a Chromium-based browser.

```powershell
msedge --headless=new --disable-gpu --hide-scrollbars --force-device-scale-factor=1 `
  --window-size=1200,630 --screenshot=images\cover-ai-transport.png `
  cover\cover.html
```

Open `cover/cover.html` directly in a browser to preview it. Edit `cover/tokens.css`
for palette/type changes and `cover/cover.css` for layout. The table in the HTML is the
real key map; keep it in sync with `main/media_cmd.c`.

## Boundary

Do not move this directory back to upstream `main`; it is fork-only by design (see
`docs/fork-guide.md`).
