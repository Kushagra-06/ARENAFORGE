#!/usr/bin/env python3
"""Inline the compiled modules into web/index.template.html and write one self-contained
web/arenaforge.html:
  build/arena.js     C + C++ compiled to WebAssembly (wasm embedded as base64)
  build/arena_js.js  the same C + C++ compiled to plain JavaScript (used if the page blocks WebAssembly)"""
import pathlib, sys

root = pathlib.Path(__file__).resolve().parent.parent
out = (root / "web" / "index.template.html").read_text(encoding="utf-8")

def safe(js):
    # keep the HTML parser from ending / confusing an inline <script>
    return js.replace("</script", "<\\/script").replace("<!--", "<\\!--")

for marker, name in (("/*__ARENA_WASM_JS__*/", "arena.js"), ("/*__ARENA_JS_FALLBACK__*/", "arena_js.js")):
    if marker not in out:
        sys.exit("marker not found in template: " + marker)
    out = out.replace(marker, safe((root / "build" / name).read_text(encoding="utf-8")))

dest = root / "web" / "arenaforge.html"
dest.write_text(out, encoding="utf-8")
print(f"wrote {dest} ({len(out)/1024:.0f} KiB)")
