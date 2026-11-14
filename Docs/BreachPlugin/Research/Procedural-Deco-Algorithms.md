# 절차적 Deco 알고리즘 선택

2026-11-14. 승인된 HTML 프로토타입을 Unreal의 Feature Deco 경로로 옮기기 위한 조사와 구현 기준이다. R6의 비공개 알고리즘을 확인한 문서가 아니다.

## 배치: 경계 길이를 따라 군집과 공백 생성

[Bridson의 Poisson-disk 논문](https://www.cs.ubc.ca/~rbridson/docs/bridson-siggraph07-poissondisk.pdf)은 최소 거리 r 이상으로 샘플을 분리하는 방법이다. 이번 목표인 촘촘한 군집과 큰 공백을 만드는 주 알고리즘으로 선택하지 않는다.

[spatstat의 Matérn 군집 분포 문서](https://search.r-project.org/CRAN/refmans/spatstat.random/html/rMatClust.html)는 군집 중심과 그 주변 샘플을 분리하는 모델을 설명한다. 군집형 배치의 비교 근거이며, 2D 통계 시뮬레이터를 런타임에 도입하지 않는다.

구현은 HTML에서 확인한 짧은 간격/긴 간격의 혼합을 유지한다. 군집 내부에는 평균 간격의 0.2~0.75배 간격을, 군집 사이에는 1.6~3.8배 공백을 둔다. 최초 군집은 2~5개, 이후 군집은 2~6개 조각으로 구성한다. 각 군집과 조각의 폭·길이도 변형한다. 샘플은 개별 변의 개수가 아닌 경계의 월드 길이에 놓는다.

## 형상: 고정된 연결 구조의 얇은 판

각 조각은 경계에 부착된 뿌리와 구멍 안쪽으로 뻗은 자유단을 가진다. 뿌리의 부착 구간을 저장하고, 7개의 뿌리/끝 표본으로 앞·뒷면과 옆면을 연결한다. 큰 실루엣은 코드로, 세밀한 섬유 틈은 머티리얼로 표현한다. 범용 3D Boolean이나 Voronoi 분할은 필요하지 않다.

짧은 변이 많은 윤곽에도 뿌리가 따라가도록 부착 구간의 실제 폴리라인을 기록한다. 날카로운 모서리에서는 폭을 제한하고, 안쪽으로 접히는 판은 길이를 줄여 유효한 면을 유지한다. 부착 구간 일부가 없어지면 그 조각을 통째로 제거한다. 살아 있는 조각의 시드와 형상은 재파괴 때 유지한다.

크기는 월드 cm로 계산하고 로컬 좌표로 변환한다. 같은 설정과 입력 순서에서 재현되도록 [Unreal Random Streams](https://dev.epicgames.com/documentation/en-us/unreal-engine/random-streams-in-unreal-engine?application_version=5.8)를 사용한다. HTML의 JavaScript 난수열과 비트 단위로 같은 결과를 보장하지는 않는다.

## 표면: Masked 머티리얼과 UV

[Unreal Material Inputs](https://dev.epicgames.com/documentation/unreal-engine/material-inputs-in-unreal-engine?application_version=5.8)와 [Blend Modes](https://dev.epicgames.com/documentation/unreal-engine/material-blend-modes-in-unreal-engine?application_version=5.8)는 Opacity Mask로 픽셀을 완전히 표시하거나 버리는 동작을 설명한다. 반투명 혼합 대신 Masked를 사용해 섬유 사이의 빈 부분을 만든다.

[GPU Gems의 풀 렌더링 사례](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-7-rendering-countless-blades-waving-grass)는 적은 폴리곤과 알파 텍스처를 결합해 복잡한 실루엣을 표현하는 선례다. 풀의 교차 카드 배치나 애니메이션을 Deco에 도입한다는 의미는 아니다.

UV의 U는 조각 폭, V는 뿌리에서 끝 방향이다. 앞·뒷면의 마스크가 맞도록 같은 UV를 쓴다. 아틀라스 크기는 기본 1×1이며 필요하면 HTML의 4×4 마스크처럼 설정할 수 있다. 재질만 바꿀 때는 조각의 시드·정점·UV를 유지한다. 충돌은 계속 실제 벽만 소유하며, 마스크의 빈 픽셀로 이동·탄환 판정을 바꾸지 않는다.

## 소유권과 기본값

기존 BreachSystem 모듈의 DecorationComponent가 기록과 표시 수명을 계속 소유한다. Feature의 Static Mesh 목록과 인스턴스 표시를 절차적 Dynamic Mesh 하나와 Material 설정으로 교체한다. Cut Deco와 절삭·충돌 경로는 유지한다.

최소 구멍 면적 400cm²는 유지한다. 새 형상의 기본값은 HTML 콘크리트형의 폭 22cm, 길이 10cm, 두께 1.4cm, 간격 19cm, 불규칙도 0.7, 시드 42이다. 재질은 사용자가 지정하며, 기존 사용자 에셋을 임의로 수정하지 않는다.
