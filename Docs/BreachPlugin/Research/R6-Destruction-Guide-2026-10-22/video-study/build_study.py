"""Build a video-frame and Korean-transcript reading companion."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import hashlib
import html
import json
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parent
TOOLS = Path('C:/Users/csh/AppData/Local/Temp/codex-r6-video-tools')
sys.path.insert(0, str(TOOLS))
import imageio_ffmpeg
from PIL import Image

VIDEO = TOOLS / 'SjkQxowsL0I.mp4'
FFMPEG = imageio_ffmpeg.get_ffmpeg_exe()
SOURCE = ROOT.parent / 'Rainbow_Six_Siege_Destruction_KO_full.txt'

# First observed sample of each slide state, reviewed in the contact sheets.
# Samples are five seconds apart; these are video scenes, not PDF page numbers.
STATES = '''
1|GDC 오프닝
2|The Art of Destruction in Rainbow Six: Siege
8|The Context
14|What is Procedural Destruction?
22|A Brief History: Procedural Glass
24|Procedural Glass Prototype
25|유리 파괴 시연
29|Procedural Glass Prototype: 시연 후
31|A Brief History: Concept Tech Demo
32|Concept Tech Demo
34|기술 데모: 절차적 파괴
37|기술 데모: 재질의 반응
42|기술 데모: 공간의 변화
48|A Brief History: Siege Release
52|Presentation Outline
58|RealBlast Overview
59|RealBlast 팀
60|RealBlast 팀: 협업
62|Technology Group
68|RealBlast: 솔루션과 도구
79|Collaborations
84|Destruction in Rainbow Six: Siege
85|공격팀
87|공격팀과 방어팀
89|Dynamic Environments in R6:S
96|Dynamic Environments: Trapdoors
97|Dynamic Environments: Barricades
99|Dynamic Environments: LOS Floors
100|Dynamic Environments: Breachable Walls
103|Sounds simple!
104|Reality Check: 제작 비용
114|Reality Check: 제작 방식과 성능
120|The R6:S Game Ecosystem
122|Destruction Event System
130|AI Navigation: Navlink Update
132|AI Visibility / Sound Propagation
140|Gameplay Elements
146|RealBlast Tech
150|Destruction Model Concept
157|Destruction Model: 계층 분해
161|Destruction Model: 연결 그래프
169|Destruction Model: 상태 관리
172|Procedural Integration in the Model
177|Surface Procedural Destruction Overview
178|Surface Procedural Destruction
188|Creating a 2D Model
190|Generating a Cut Pattern
193|Polygon Intersection
199|Triangulation
204|Creating a 3D Model
207|Cutters
212|Texture Cutter
220|So, what do we have now?
222|표면 파괴 결과
224|Improving the Visual Look
238|Cut Decorations
241|Feature-Bound Decorations
246|장식 결합 결과
250|파괴된 벽의 게임 화면
251|Destruction & Performance
255|Impact on Other Systems
264|Collision Update
268|Collision Update: Our Solution
275|Debris in R6:S: 재사용
284|Debris in R6:S: 충돌과 효과
288|Destruction Performance
290|Making it Real(time)
295|Destruction Budgets
301|Memory
303|Performance
308|Multithreading
316|Asynchronicity
325|Asynchro-How?
334|Asynchronicity Trick
344|Time Slicing
352|Time Slicing: 작업 상태 보존
362|Slicing in Practice
369|Time Slicing, or not
380|Optimization
385|Measure Performance & Optimize
393|Limit Degenerate Cases
400|Self-Destruction
401|Self-Destruction: 작은 구멍
403|Self-Destruction: 큰 구멍으로 병합
405|Limit Runtime Allocations
416|Benchmarks
430|Online
431|Determinism and Replication
439|Physics Replication, Debris & Determinism
447|Determinism
450|Determinism Requirements: 같은 입력
459|Determinism Requirements: 같은 순서
465|Randomness vs. Determinism
476|Different States vs. Determinism
484|Instant Feedback vs. Determinism
496|Instant Feedback: 처리 흐름
498|Instant Feedback: 발사자와 호스트
501|Instant Feedback: 이벤트 분기
505|Caveats
510|Other Solutions: Rollback
524|Future Development & Conclusion
525|Future Development
535|Takeaways: 팀과 제작 지원
544|Takeaways: 초기에 시작하기
547|Conclusion
551|P.S.: 관련 발표
553|P.P.S.: 연락처
555|Questions?
558|질의응답
574|영상 마무리
'''


def seconds(value):
    h, m, s = map(int, value.split(':'))
    return h * 3600 + m * 60 + s


def stamp(value):
    return f'{int(value) // 60:02d}:{int(value) % 60:02d}'


def main():
    captions = []
    pattern = r'\[(\d\d:\d\d:\d\d)[–\-](\d\d:\d\d:\d\d)\]\s*(.*?)(?=\n\s*\[\d\d:\d\d:\d\d|\Z)'
    for i, match in enumerate(re.finditer(pattern, SOURCE.read_text(encoding='utf-8'), re.S)):
        start, end = seconds(match[1]), seconds(match[2])
        captions.append(dict(id=i, start=start, end=end, text=match[3].strip(), shot=round(start + min(2, (end-start)/2), 1)))
    assert len(captions) == 178, len(captions)
    scenes = []
    for line in STATES.strip().splitlines():
        index, title = line.split('|', 1)
        start = (int(index) - 1) * 5
        scenes.append(dict(id=len(scenes), title=title, start=start, shot=min(start + 2.5, 2871)))
    for i, scene in enumerate(scenes):
        scene['end'] = scenes[i+1]['start'] if i+1 < len(scenes) else 2873
        scene['captions'] = [c['id'] for c in captions if c['start'] < scene['end'] and c['end'] > scene['start']]
    for caption in captions:
        caption['scene'] = max(i for i, scene in enumerate(scenes) if scene['start'] <= caption['shot'])
    times = sorted({x['shot'] for x in scenes + captions})
    (ROOT / 'frames').mkdir(exist_ok=True)

    def filename(at):
        return f'frames/frame-{int(at*10):05d}.jpg'

    def capture(at):
        out = ROOT / filename(at)
        if not out.exists():
            subprocess.run([FFMPEG, '-hide_banner', '-loglevel', 'error', '-threads', '2', '-ss', str(at), '-i', str(VIDEO), '-frames:v', '1', '-q:v', '2', '-threads', '2', str(out)], check=True)
        with Image.open(out) as image:
            assert image.size == (1280, 720), (out, image.size)
        return dict(time=at, file=filename(at), sha256=hashlib.sha256(out.read_bytes()).hexdigest())

    with ThreadPoolExecutor(max_workers=4) as pool:
        frames = list(pool.map(capture, times))
    for item in scenes + captions:
        item['image'] = filename(item['shot'])
    data = dict(video='SjkQxowsL0I', duration=2873, scenes=scenes, captions=captions)
    (ROOT / 'study-data.json').write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding='utf-8')
    template = (ROOT / 'viewer-template.html').read_text(encoding='utf-8')
    content = template.replace('/*STUDY_DATA*/', json.dumps(data, ensure_ascii=False).replace('</', '<\\/'))
    (ROOT / 'index.html').write_text(content, encoding='utf-8')
    (ROOT / 'capture-manifest.json').write_text(json.dumps(dict(
        video_url='https://www.youtube.com/watch?v=SjkQxowsL0I',
        video_sha256=hashlib.sha256(VIDEO.read_bytes()).hexdigest(),
        transcript_sha256=hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
        capture='Actual decoded video frames, complete 1280x720 frame; no crop or slide reconstruction',
        scene_boundaries='Manually reviewed five-second contact sheets; approximate to five seconds',
        subtitle_timing='User-provided Korean translation, approximately 15-second segments; unchanged',
        frames=frames), ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(dict(scenes=len(scenes), captions=len(captions), frames=len(frames), resolution='1280x720'), ensure_ascii=False), flush=True)


if __name__ == '__main__':
    main()
