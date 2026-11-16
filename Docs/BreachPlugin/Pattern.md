# 파괴 패턴 작성

`UBreachSurfaceMaterial.Pattern`은 누적 피해로 생기는 큰 파손에 적용한다. 개별 총알 구멍은 기존 무기 커터를 사용한다.

## 현재 프로젝트 에셋

새 BSM을 만들지 않고 `/BreachSystem/SurfaceMaterial`의 기존 에셋을 사용한다.

| 재질 | SurfaceMaterial | 표시 인스턴스 | 파괴 데이터 |
|---|---|---|---|
| 벽돌 | SurfaceMaterial003_Brick | MI_WallBrick | 실제 줄눈에 맞춘 36개 영역 |
| 나무 | SurfaceMaterial001_Wood | MI_WallWood | 실제 가로결을 따르는 열린 선 10개 |
| 콘크리트 | SurfaceMaterial002_Concrete | MI_WallConcrete | None, 기존 일반 윤곽 |

`M_WallBrick`, `M_WallWood`, `M_WallConcrete`는 각각 부모 머티리얼이다. 원래 BaseColor·Normal 텍스처와 Roughness를 유지하고 자식 인스턴스에서 텍스처·타일 크기를 조정할 수 있다. `SurfaceMaterial002_Wood`는 실제로 Concrete를 가리키는 기존 리다이렉터이므로 나무 설정에 사용하지 않는다.

패턴은 기존 `/BreachSystem/PatternExamples/BPAT_Brick`, `BPAT_Wood`, `BPAT_Concrete`에 작성했다. 배경은 실제 `T_Wall*_BaseColor`다. 벽돌 규칙 텍스처는 `/BreachSystem/SurfaceSet/Textures/T_WallBrick_RegionIDs`다.

## 편집기

패턴 에셋을 열고 프리뷰의 SurfaceMaterial을 선택한다. 선택기는 위 SurfaceMaterial 폴더의 에셋만 표시한다. 큰 캔버스는 패턴 작성용, 오른쪽 작은 캔버스는 파손 시험용이다. 프리뷰 Contour·결 영향도·Padding·타일 크기는 선택한 SurfaceMaterial에서 읽으며 이 창의 별도 설정값은 없다. 프리뷰는 클릭 위치 한 점으로 큰 절단 형상을 계산하며, 피해 누적 횟수를 재현하는 PIE 테스트와는 구분한다.

### 점·선 편집

- `영역 그리기`: 클릭해 점을 찍고 시작점을 다시 클릭하거나 Enter로 닫는다.
- `결선 그리기`: 클릭해 점을 이어 열린 선을 만들고 Enter로 끝낸다.
- `점·선 선택 / 이동`: 점을 드래그하면 공유하는 경계도 함께 움직인다. 선을 드래그하면 양 끝을 함께 옮긴다.
- `선택 삭제` 또는 Delete: 선택한 점을 삭제한다. 선을 선택했다면 해당 경계를 삭제한다.
- Escape는 작성 중인 점을 취소한다. Undo/Redo를 지원한다. 자기 교차·최소 정점 수 위반 등 잘못된 편집은 거부한다.

한 패턴에는 닫힌 영역과 열린 결선을 섞지 않는다. 전체 경계를 지워 None이 된 상태에서 다른 종류를 작성할 수 있다. Save는 편집 중인 패턴만 저장하며 원본 텍스처나 SurfaceMaterial을 저장하지 않는다.

### 텍스처 기반

프로젝트 규칙은 **색상 ID 맵 한 형식**이다. 같은 영역은 하나의 단색, 다른 영역은 서로 다른 RGB 값을 사용한다. 색의 밝기는 강도가 아니다. 그라데이션·색 보간·일반 사진을 사용하지 않는다.

규칙 PNG를 Content Browser로 임포트한 뒤 `규칙 텍스처`에 지정하면 원본 소스 픽셀에서 영역을 읽는다. 저장할 때도 다시 검증한다. 소스 이미지와 압축 런타임 밉은 변경하지 않는다. 예제는 무압축·최근접 샘플링·밉 없음으로 설정해 표시 중에도 색상을 보존한다.

ID 맵은 닫힌 영역을 저장하므로 Regions용이다. GrainGuides를 닫힌 조각으로 바꾸지 않는다. 열린 결선은 점·선 편집에서 작성한다. 해당 패턴에서 ID 맵을 적용하려 하면 원인을 안내하고 거부한다.

## 물리 크기와 타일링

기존 에셋은 `bUseWorldSizeUV=false`로 원본 UV0를 유지한다. 실제 Wall 3종은 이 옵션을 켜고 `TileSizeCm=(100,100)`으로 설정했다. 벽을 늘리면 한 타일의 물리 크기가 유지되어 반복 횟수가 늘어난다.

표시와 파괴는 같은 좌표식을 사용한다.

```
U = 0.5 + LocalX * abs(WorldScaleX) / TileSizeCm.X
V = 0.5 - LocalZ * abs(WorldScaleZ) / TileSizeCm.Y
```

원점은 컴포넌트 중심이다. 회전·이동과 독립적이며 양면은 동일한 기준을 쓴다. 재생 중에는 컴포넌트 소유 MID가 SurfaceMaterial의 타일 파라미터를 적용하고 파손 Proxy도 같은 MID를 사용한다. 에디터에는 저장 가능한 MI를 연결한다. 원본 메시 UV0 자체는 변경하지 않는다.

Regions는 기본 범위와 겹치거나 접촉하는 영역 전체를 선택하며 연쇄 선택하지 않는다. GrainGuides는 일반 Contour와 열린 결선을 함께 사용하고, 일반 노이즈를 꺼도 결 변위는 남는다. 최대 변위 비율이 0이면 변위하지 않는다. None은 기존 일반 Contour를 사용한다.

## 설정 도구와 검증

루트의 `BuildProject_Editor.bat`로 빌드한다. 에셋 구성 도구는 `Plugins/BreachSystem/Tools/configure_wall_patterns.py`다. 지정 Wall 부모·자식 MI·기존 SurfaceMaterial·패턴·벽돌 ID 맵만 저장하며 맵·무기 에셋은 저장하지 않는다. 일반 배치 도구로 임의 에셋에 적용하지 않는다.

읽기 전용 검증은 같은 스크립트 실행에 `-BreachWallPatternsVerify`를 추가한다. 결과는 `Saved/Automation/WallMaterialPatterns/verified.json`이다.

자동화 필터는 `BreachSystem.Pattern`이며 전체 회귀는 `BreachSystem`이다. 실제 Wall PIE는 별도 프로세스에서 `-unattended -BreachPatternDisposablePIE -BreachPatternCapture`와 `Automation RunTests BreachSystem.Pattern.PIE`를 사용한다. PIE는 저장하지 않는 임시 맵에서 기존 SurfaceMaterial 3종을 읽고, 복제본에만 시험 피해 조건을 적용한다. 경계 관찰을 위해 복제본의 장식만 끈다. 정면 및 회전·음수·비균일 스케일 벽의 누적 피해·즉시 Complex 충돌·원본 UV 보존·MID 공유를 검사하고 양면 화면을 `Saved/Automation/Pattern`에 저장한다.
