<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# Fork 素材 —— AI Transport

本应用 fork 专属的素材目录。上游 `main` 上 `docs/assets/` 保持为空；这里的文件只存在于
本分支，不会同步回上游。

## 内容

| 路径 | 说明 |
| --- | --- |
| `cover/` | 社区封面源文件：`cover.html` + `tokens.css` + `cover.css`。 |
| `images/cover-ai-transport.png` | 渲染好的 1200×630 封面，根 README 与社区上架共用。 |

## 重新渲染封面

封面是一个静态 HTML 页面，PNG 是无头浏览器截图。没有构建步骤，只依赖 Chromium 系浏览器。

```powershell
$root = (Resolve-Path .).Path   # 在本目录执行
msedge --headless=new --disable-gpu --hide-scrollbars --force-device-scale-factor=1 `
  --window-size=1200,630 `
  --screenshot="$root\images\cover-ai-transport.png" `
  "$root\cover\cover.html"
```

页面参数必须用绝对路径：无头 Edge 会把相对参数当成 URL（`cover\cover.html` 会变成
`http://cover/cover.html`，跑完不生成文件）。

直接用浏览器打开 `cover/cover.html` 可以预览。改配色/字体改 `cover/tokens.css`，改版式改
`cover/cover.css`。

封面按键表不是另编的说明，而是机身屏幕上的那张图例：4 行、同一组词（`FF` / `NEXT` /
`PLAY` / `FULLSCR` / `VOL+` …），逐格补上中文词。图例行来自 `main/media_remote.c` 的
`add_guide_row()`，键位映射以 `main/media_cmd.c` 为准；改键位时三处一起改。

## 边界

不要把本目录搬回上游 `main`；按设计它只属于 fork（见 `docs/fork-guide.md`）。
