"""Refresh the embedded app/CSS. Python 3, no extra packages required."""
from pathlib import Path
import re

root = Path(__file__).resolve().parent
page = root/'visualization.html'
text = page.read_text(encoding='utf-8')
text = re.sub(r'<style data-lab-style>.*?</style>',lambda _: '<style data-lab-style>'+ (root/'lab.css').read_text(encoding='utf-8')+'</style>',text,flags=re.S)
text = re.sub(r'<script data-app>.*?</script>',lambda _: '<script data-app>\n'+(root/'lab.js').read_text(encoding='utf-8')+'\n</script>',text,flags=re.S)
page.write_text(text,encoding='utf-8')
page = root/'explanation.html'
text = page.read_text(encoding='utf-8')
text = re.sub(r'<style>.*?</style>',lambda _: '<style>'+(root/'explanation.css').read_text(encoding='utf-8')+'</style>',text,count=1,flags=re.S)
page.write_text(text,encoding='utf-8')
print('Updated both HTML files. Reload the browser to see the changes.')
