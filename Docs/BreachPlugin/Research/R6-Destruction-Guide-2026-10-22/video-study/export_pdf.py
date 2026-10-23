"""Export the existing R6 video-study scenes, images and Korean subtitles to PDF."""
from pathlib import Path
import html
import json
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent
study = json.loads((ROOT / "study-data.json").read_text(encoding="utf-8"))
output = ROOT / "R6-Destruction-Video-Study.pdf"
printable = ROOT / "print-study.html"

def esc(value):
    return html.escape(str(value), quote=True)

def stamp(seconds):
    return f"{int(seconds)//60:02d}:{int(seconds)%60:02d}"

parts = ['''<!doctype html><html lang="ko"><head><meta charset="utf-8">
<title>R6 파괴 강연 · 화면과 자막</title>
<style>
@page {size: A4 landscape; margin: 12mm}
* {box-sizing:border-box}
body {font: 12pt/1.5 "Malgun Gothic","Apple SD Gothic Neo",sans-serif;color:#142025;margin:0}
h1 {font-size:19pt;margin:0 0 3mm}
h2 {font-size:12pt;margin:6mm 0 2mm}
p {margin:0 0 2mm;white-space:pre-wrap}
.scene {break-before:page}
.scene:first-of-type {break-before:auto}
.meta {color:#53636a;font-size:9pt;margin-bottom:4mm}
.hero {width:100%;max-height:125mm;object-fit:contain;object-position:left center;background:#101617}
.caption {border-top:1px solid #d4dedc;padding:3mm 0;break-inside:avoid;display:flex;gap:4mm}
.caption .shot {width:80mm;max-height:45mm;object-fit:contain;align-self:flex-start}
.caption .body {flex:1;min-width:0}
.time {font-weight:bold;color:#236954;font-size:9pt}
.footer {font-size:8pt;color:#667;margin-top:4mm}
@media screen {.scene{padding:14mm;max-width:1150px;margin:auto;border-bottom:2px solid #ccc}}
</style></head><body>''']
for scene in study["scenes"]:
    parts.append(f'<section class="scene"><h1>{scene["id"]+1:03d}. {esc(scene["title"])}</h1>')
    parts.append(f'<div class="meta">영상 {stamp(scene["start"])}–{stamp(scene["end"])} · 장면 캡처 {stamp(scene["shot"])}</div>')
    parts.append(f'<img class="hero" src="{esc(scene["image"])}" alt="영상 화면">')
    parts.append('<h2>이 장면의 한국어 자막</h2>')
    if not scene["captions"]:
        parts.append("<p>제공된 TXT에 이 구간의 자막이 없습니다.</p>")
    for idx in scene["captions"]:
        cap = study["captions"][idx]
        parts.append('<div class="caption">')
        parts.append(f'<img class="shot" src="{esc(cap["image"])}" alt="자막 시각의 영상 캡처">')
        parts.append(f'<div class="body"><div class="time">{stamp(cap["start"])}–{stamp(cap["end"])}</div><p>{esc(cap["text"])}</p></div></div>')
    parts.append('</section>')
parts.append('<div class="footer">원본 영상: https://www.youtube.com/watch?v=SjkQxowsL0I</div></body></html>')
printable.write_text("\n".join(parts), encoding="utf-8")

candidates = [
    Path(r"C:\Program Files\Google\Chrome\Application\chrome.exe"),
    Path(r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"),
    Path(r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe"),
    Path(r"C:\Program Files\Microsoft\Edge\Application\msedge.exe"),
]
browser = next((str(p) for p in candidates if p.is_file()), None)
browser = browser or shutil.which("chrome") or shutil.which("msedge") or shutil.which("chromium")
if not browser:
    print(f"브라우저 실행 파일을 찾지 못했습니다. {printable}을 Chrome/Edge로 열고 Ctrl+P → PDF로 저장하세요.")
else:
    command = [browser, "--headless=new", "--disable-gpu", "--no-pdf-header-footer",
               "--allow-file-access-from-files", f"--print-to-pdf={output}", printable.as_uri()]
    proc = subprocess.run(command, capture_output=True, text=True, timeout=240)
    if proc.returncode == 0 and output.exists() and output.stat().st_size > 10_000:
        print(f"PDF 생성 완료: {output} ({output.stat().st_size:,} bytes)")
    else:
        print("자동 PDF 생성 실패:", proc.stderr[-1500:])
        print(f"{printable}을 Chrome/Edge로 열고 Ctrl+P → PDF로 저장하세요.")
