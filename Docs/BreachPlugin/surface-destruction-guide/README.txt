Surface Procedural Destruction, GDC 2016 PDF 34–49페이지

시작
1. explanation.html을 Chrome 또는 Edge에서 엽니다.
2. 각 절의 녹색 링크를 누르면 해당 주제의 visualization.html이 열립니다.
3. 두 HTML 파일은 같은 폴더에 두세요. 별도 설치나 인터넷은 필요 없습니다.
   외부 원문/공식 문서 링크를 열 때만 인터넷이 필요합니다.

파일
- explanation.html: 페이지별 한국어 해설, 원본 도식, Unreal 구현 제안과 비교.
- visualization.html: React 기반 독립형 실험실. 필요한 라이브러리 내장.
- lab.js: 수정 가능한 React 소스. JSX 없이 createElement 사용.
- lab.css: 시각화 스타일.
- explanation.css: 설명 문서 스타일.
- rebuild.py: 수정한 JS/CSS를 기존 독립형 HTML에 반영. Python 3 표준 라이브러리만 사용.
- THIRD_PARTY_LICENSES.txt: 내장 라이브러리 라이선스.

수정
lab.js 또는 CSS 수정 후 같은 폴더에서 python rebuild.py 실행.
본문은 explanation.html에서 직접 수정할 수 있습니다.
브라우저의 페이지를 새로고침해야 변경이 보입니다.
rebuild.py는 기존 visualization.html 안의 라이브러리를 그대로 유지합니다.

실험
- 기하 파이프라인: 재생/단계 선택, 모양, 반경, 두께, 위치, 누적 절단.
- 재료와 UV: 반복 배율, 오프셋, 영향 반경.
- 장식의 수명: 첫 절단/재절단, 면 장식/경계 장식 표시.
- 세 기술 비교: 2D 절단, Mesh Boolean 개념, Chaos Destruction 개념.

범위
2D Boolean과 Earcut 삼각분할은 실제 계산합니다.
2.5D 투영/돌출과 비교 장면은 교육용 도식입니다.
Chaos 비교는 실제 물리 solver가 아닌 CSS 모션입니다.
Unreal 프로젝트/플러그인은 포함하지 않으며 의사코드는 엔진에서 빌드 검증하지 않았습니다.
Ubisoft RealBlast 내부 코드를 재현한 것이 아닙니다.
원본 PDF 및 발췌 슬라이드의 권리는 원저작권자에게 있습니다.

내장 라이브러리
React / ReactDOM 18.3.1 (MIT)
Mapbox Earcut 2.2.4 (ISC)
polygon-clipping 0.15.7 (MIT, 배포물의 의존성 고지 포함)
배포 원본: https://cdn.jsdelivr.net/npm/

기준 발표
Julien L’Heureux, Ubisoft Montreal,
The Art of Destruction in Rainbow Six: Siege, GDC 2016.
https://media.gdcvault.com/gdc2016/Presentations/LHeureux_Julien_Art_Of_Destruction.pdf

문서 확인 날짜: 2026-10-12
