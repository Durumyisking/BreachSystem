# Simple RemainingRegions 단일 상태 구현

사용자가 대화에서 승인한 범위: 모든 손상은 RemainingRegions에 누적한다. 실제 충돌은 원본 벽에서 전체 제거 영역을 구하고 기존 면적 판정·가공을 적용하여 파생한다. 충돌 결과는 RemainingRegions에 덮어쓰지 않는다. Simple 외부의 Surface 알고리즘, 엔진 수치 변환, 에셋은 변경하지 않는다.

## 실행 계획

1. `BreachSystem.Surface.SimpleRemainingRegions` 회귀 테스트 추가. 임계값 미만 내부 구멍과 가장자리 파임이 원본 상태에 남는지, BuildMesh가 상태를 바꾸지 않는지, 작은 구멍들이 연결된 후 출력 충돌에 반영되는지 검사한다. 지정 빌드 후 기존 구현에서 실패를 확인한다.
2. `FBreachSimpleCollision`의 FHole/Holes 캐시와 재수집 루프를 제거한다. ApplyCutDebug는 이번 Cutter와 Detached만 RemainingRegions에서 차감한다. 기존 면적 차이 기반 변경 판정은 유지한다. BuildMesh에서 OriginalOuter - RemainingRegions로 전체 제거 영역을 얻고, 현재 면적 조건과 현재 비활성화된 Simplify 설정을 유지해 원본 벽으로부터 충돌 영역을 계산한다. 파생 결과는 지역 변수로만 보관한다.
3. dirty의 의미를 모든 손상 상태 변경으로 맞춘다. 기존 테스트의 small-cut dirty 기대만 새 의미로 수정하고, 이미 열린 영역의 재절단은 dirty로 만들지 않는 기존 동작을 보존한다. 이전 진단 테스트는 더 이상 오류 발생 자체를 성공 조건으로 요구하지 않도록 조정한다.
4. BuildProject_Editor.bat 및 Surface.Simple 관련 자동화를 실행한다. 변경 전부터 있는 면적 경계·스케일·병합 설정 관련 실패를 기록하고 새 동작의 실패와 구분한다. 진단 테스트는 현재 방식의 연속 절단 증거를 별도 폴더에 기록한다.

## 구현 판단

- Detached는 기존 native 입력 계약을 보존하여 이번 차집합의 제거 입력에 포함한다. 캐시된 과거 제거 영역은 입력에 다시 넣지 않는다.
- 면적 비교와 완전 제거 예외는 기존 코드 그대로 이동한다. 이번 작업에서 임계값 동일 값 정책, 스케일 보정, 병합 활성화, 최소 삼각형 제거를 함께 수정하지 않는다.
- 기존 작업 디렉터리의 사용자 변경을 보존하며 현재 세션에서 직접 수행한다. 커밋·푸시하지 않는다.

## 진행 기록

- RED: 지정 빌드 성공. 새 테스트에서 subthreshold Hole 저장, damage dirty, 임계값 변경 후 출력 복원, 가장자리 파임 저장 검사가 실패함을 확인했다.
- 변경 전 Simple 필터 17개 중 기존 실패 테스트 9개와 새 테스트 실패 1개를 기록했다. 기존 실패는 병합 비활성화, 단순화 비활성화, 임계값 동일 값·완전 제거 예외, 월드 스케일 처리에 해당한다.
- 상태 갱신 및 출력 파생 경로 구현. 기존 dirty 기대를 전체 손상 원본의 변경으로 맞춤. 진단의 기존 오류 재현 기록은 보존하고 새 방식은 별도 디렉터리에 기록하도록 수정했다.
- 검토 중 고리형 제거 영역 안에 남은 벽 조각을 발견했다. 파생 Openings에 ClearHoles를 적용하면 남은 조각까지 제거하므로 해당 호출을 삭제했다. Detached가 비어 있는 고리형 입력의 중심 보존·순수 제거 면적 임계값 테스트가 먼저 실패함을 확인했다.

## 최종 검증

- `BuildProject_Editor.bat`: 성공. `BreachSystem.Surface.SimpleRemainingRegions`: 성공. 작은 구멍 누적·연결, 가장자리 파임, 임계값 변경 후 출력 복원, 출력 생성 시 원본 불변, 고리 안 남은 벽 보존을 검증했다.
- `Automation RunTests BreachSystem.`: 총 57개, 경고 없는 성공 39개, 경고 포함 성공 9개, 실패 9개. 최종 보고서: `Saved/Automation/SimpleRemaining-Verified/index.json`. 실패한 이름은 변경 전 RED 기록의 기존 실패 집합과 동일하다. 새 회귀 테스트 실패는 해소됐다.
- 기존 실패: `BreachSystem.Surface.SimpleCollision.Connected`, `.FullRemoval`, `.Simplified`, `.WorldArea`; `BreachSystem.Surface.SimpleCollisionThreshold.Below`, `.Detached`, `.Equal`, `.FullRemoval`; `BreachSystem.Surface.SimpleCollisionCache`. 현재 비활성화된 병합·단순화와 기존 면적 경계·스케일 정책을 이번 작업에서 수정하지 않았다.
- 10개 합성 시나리오, 총 1120회 절단 진단: MissingEdges 0단계, float 면적 퇴화 372단계, 얇은 convex hull 실패 372단계, BuildMesh 출력 생성 실패 69단계. 출력 실패는 `Triangulation produced no faces for a remaining region.`이다. 진단은 이 잔여 오류를 경고로 명시하고 첫 실패 입력을 저장한다. 진단 성공은 모든 충돌 생성이 성공했다는 의미가 아니다.
- 첫 출력 실패 `Saved/Diagnostics/SimpleRemaining20261110/scenario3_step59.txt`: CollisionRegions에 면적 `1.4210854715202004e-14`인 별도 영역이 남고 이 영역의 삼각형 수가 0이다. 원본 손상 상태를 보존한 결과 이 수치 잔여 영역도 보존되며, ClearHoles로 지워 감추지 않는다. 수치 문제 해결은 이번 단일 상태 구현 범위 밖이다.
- 진단은 실제 Simple API와 BuildMesh를 실행하지만 Detached 입력은 사각 벽의 경계 접촉 조건으로 모델링한다. 실제 게임 플레이/PIE 재현은 수행하지 않았다. 원본 진단 증거 `Saved/Diagnostics/GeometryCauses20261110`는 보존했다.
- 정적 검토에서 남은 actionable finding 없음. 변경 소스 diff 공백 검사 통과. 커밋·푸시·에셋 수정 없음.

## 후속 승인: Surface 상태 직접 전달

사용자가 설명받고 `작업시작`으로 승인한 변경이다. 이제 손상 원본의 유일한 소유자는 `SurfaceState.RemainingRegions`다. 앞선 Simple 내부 단일화 범위를 확장하여 Component의 호출부를 함께 변경한다.

1. 변경 전 지정 빌드와 전체 BreachSystem 자동화를 기록한다. 기존 실패 9개를 이번 변경의 회귀와 구분한다.
2. `BuildMesh(TConstArrayView<UE::Geometry::FGeneralPolygon2d> RemainingRegions, FKAggregateGeom& OutCollision, FString& OutError) const` 계약을 테스트에 먼저 적용한다. 동일한 빌더에 서로 다른 입력을 번갈아 전달해 과거 상태가 출력에 영향을 주지 않는지, 입력이 변경되지 않는지 검증한다.
3. Simple의 ApplyCut/ApplyCutDebug, 별도 RemainingRegions, bDirty/IsDirty/ClearDirty를 제거한다. Initialize는 OriginalOuter와 DepthRange만 저장한다. Component의 CutPolygon은 Surface 결과만 확정하고, UpdateMesh는 Surface 영역을 직접 전달하여 매번 충돌을 생성한다. 기존 충돌 생성 실패 정책은 보존한다.
4. 기존 테스트와 진단을 직접 입력 방식으로 옮긴다. 진단은 Surface 모델 결과를 Simple에 그대로 전달하고, 이전 증거를 덮어쓰지 않도록 새 폴더를 사용한다. 지정 빌드, 전체 자동화, 최종 검토를 수행한다.

Ruling: 현재 사용자가 검토 중인 dirty checkout을 재사용한다. 다른 worktree로 옮기거나 사용자 변경을 되돌리지 않는다. 커밋·푸시는 하지 않는다.

Ruling: 면적 경계 정책, 비활성화된 Simplify/Convex 병합, 수치 보정 및 Surface의 절단·삼각분할 알고리즘은 변경하지 않는다. 실패 로그를 감추거나 작은 도형을 버리는 처리를 추가하지 않는다.

Ruling: API 전환은 기존 동작 테스트의 변경 전/후 결과를 비교하는 리팩터링이다. 새 매개변수 계약의 부재는 테스트를 먼저 전환한 지정 빌드에서 확인하고, 실행 가능한 빌드가 된 뒤 출력·입력 불변성으로 검증한다.

- 변경 전 바이너리 전체 자동화: 57개 중 성공 39개, 경고 포함 성공 9개, 실패 9개. `Saved/Automation/SurfaceShared-Baseline/index.json`에 보존했다. 첫 지정 빌드는 Live Coding 잠금으로 중단됐고, 사용자가 저장 후 에디터 종료를 확인했다.
- 새 테스트를 먼저 전환한 API 계약 검사에서 기존 2인자 BuildMesh에 대한 C2660을 확인했다. `Saved/Diagnostics/SurfaceShared20261110/api-contract-red-build.log`에 보존했다. 이어 구현·호출부를 전환한 `BuildProject_Editor.bat`는 성공했다. 결과는 같은 폴더 `green-build.log`에 있다.
- 변경 후 전체 자동화 `Saved/Automation/SurfaceShared-Green/index.json`: 57개 중 성공 39개, 경고 포함 성공 9개, 실패 9개, 미실행 0개. 실패 이름 집합과 에러는 변경 전의 기존 9개와 같다. 입력 교대·원본 불변성·연결·가장자리·남은 중심·분할·완전 제거를 검사하는 `BreachSystem.Surface.SimpleRemainingRegions`는 경고 없이 성공했다.
- 새 공유 입력 진단: 총 1120회 절단에서 MissingEdges 0단계, BuildMesh 출력 생성 실패 0단계, float 면적 퇴화 24단계, 얇은 convex hull 실패 24단계. `Saved/Diagnostics/SurfaceShared20261110/report.txt`에 남겼다. 이 수치는 합성 시나리오 안의 결과이며 실제 PIE 오류 전체가 해결됐다는 증거가 아니다. 실제 플레이 검증은 수행하지 않았다.
- 검토자가 scoped diff 6개 파일을 별도로 확인했고, 수정이 필요한 결함이나 미해결 검토 사항은 없었다. 제거한 API의 Source/Plugins 잔존 호출 없음, diff 공백 검사 통과. 원본 외곽선·두께 및 기존 충돌 생성 실패 정책은 유지했다.

## 후속 승인: 실제 플레이 실패 입력 기록

사용자가 `너가 코드짜놓으면 내가 테스트해볼게`로 승인했다. 실제 플레이는 사용자가 실행한다. 현재 사용자의 Boolean·면적·초기 CollisionRegions 설정과 주석을 유지하고 진단만 추가한다.

1. Editor 진단 전용 helper로 원본/깊이/면적 설정과 A RemainingRegions, B Holes, C SimpleCuts, D CollisionRegions를 %.17g로 직렬화한다. 각 단계의 FDelaunay2 결과와 HasEdge 기반 누락 경계를 함께 저장한다. 자동화 fixture로 파일 저장·정밀도·순서·누락 경계 수집을 검사한다.
2. BuildMesh의 삼각분할 실패 분기에서 표면별 첫 성공한 기록만 `Saved/Diagnostics/SimpleCollisionFailures/<GUID>/snapshot.txt`로 저장한다. Initialize에서 기록 여부를 초기화한다. 입력/출력/기존 실패 반환은 변경하지 않는다. 실제 Editor 진단은 WITH_EDITOR, 자동화 검증은 WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS로 구분한다.
3. plugin Tools의 오프라인 분석기로 저장된 double을 정확한 유리수로 해석하여 교차·겹침·변 위 정점·Hole 접촉/포함 관계를 검사한다. 작은 변 자체를 오류로 단정하거나 수치 보정을 수행하지 않는다. 독립적인 좌표 fixture로 분석기를 검증한다.
4. 지정 Editor 빌드와 저장 검증을 수행하고 사용자에게 실제 재현 후 확인할 경로를 안내한다. 수집 실패는 로그에 명시하고 원래 기하 처리 실패 결과를 그대로 반환한다.

### 실제 실패 기록 검증 결과

- Live Coding 잠금으로 첫 빌드가 중단된 뒤 사용자가 저장·종료를 확인했다. 저장 helper를 빈 구현으로 둔 RED 자동화에서 정밀도/단계/누락 경계/파일 저장 6개 기대가 실패함을 확인했다. `Saved/Automation/SimpleFailureCapture-Red/index.json`에 남겼다.
- 구현 후 지정 `BuildProject_Editor.bat` 성공. `BreachSystem.Diagnostics.SimpleFailureSnapshot`는 경고 없이 성공했다. 합성 교차 도형의 외곽선과 Hole 순서, `1.0000000000000002` 보존, 설정값, 실제 UTF-8 파일, 누락 경계 기록과 입력 불변성을 검증했다.
- 합성 파일을 오프라인 분석기로 읽었을 때 Original/A/B/C에서는 경계 문제를 찾지 않았고, 의도적으로 교차시킨 D에서 정확한 교차 1개와 누락 경계 1개를 확인했다. 이 fixture는 실제 사용자의 실패 형상이 아니다. `Saved/Automation/SimpleFailureSnapshotFiles/652CBCE8-42F6-2FC4-5E2A-B5926484EB9D/`에 snapshot.txt와 analysis.json을 보존했다.
- Python 분석기 독립 fixture 11개 성공: 정상 Hole, 중복 없는 교차, 이전에 관찰한 미세 교차, 인접 변 역주행 겹침, 비인접 변 위 정점, 외곽 밖 Hole, 외곽 접촉 Hole, 중첩 Hole, 영역 간 교차, 길이 0인 변, parser의 정밀도/순서 보존.
- 전체 자동화 58개: 성공 38개, 경고 포함 성공 8개, 실패 12개. 새 저장 테스트를 제외한 실패 이름과 결과는 진단 추가 전 RED의 기존 12개와 동일하다. 현재 사용자 실험의 `CollisionRegions(RemainingRegions)` 초기값과 Boolean·면적 설정을 보존했다. `Saved/Automation/SimpleFailureCapture-Green/index.json` 참고.
- 현재 코드의 기존 실패 이름: `BreachSystem.Surface.SimpleCollision.Connected`, `.Detached`, `.Direct`, `.FullRemoval`, `.Lifecycle`, `.Simplified`; `BreachSystem.Surface.SimpleCollisionThreshold.Below`, `.Detached`, `.Equal`, `.FullRemoval`; `BreachSystem.Surface.SimpleCollisionCache`; `BreachSystem.Surface.SimpleRemainingRegions`. 이 진단 작업에서 동작 정책을 수정하여 통과시키지 않았다.
- 별도 정적 검토에서 수정이 필요한 오류 없음. HasEdge(false) 인덱스와 엔진의 outer→holes 출력 순서 일치, Editor 제한, 최초 기록과 초기화 수명, 기존 입력/오류/충돌 출력 불변을 확인했다. 실제 PIE 재현은 사용자가 수행한다.
- 기존 계산 파일의 이번 변경만 별도 diff로 보존: `Saved/Diagnostics/SimpleFailureCapture20261110/runtime-instrumentation.diff`. 에셋·엔진 소스 수정, 커밋·푸시 없음.
