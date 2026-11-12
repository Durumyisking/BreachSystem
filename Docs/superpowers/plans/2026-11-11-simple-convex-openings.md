# Convex 통과 영역 구현 계획

**Goal:** PDF에서 정한 내부 Convex 탐색으로 이동용 통과 영역을 선택한다.

**Architecture:** BreachSystem 런타임의 `SimplifyInside`가 여러 볼록 다각형을 반환한다. 구멍 캐시와 RemainingRegions 대체 경로가 같은 선택 함수를 사용하고, 원본 벽에서 선택 영역을 빼는 후속 메시 생성은 유지한다.

**Spec:** 대화에 첨부된 `BreachSystem_Convex_Collision_Chat.pdf` 및 사용자의 구현 시작 요청.

## 범위와 판단

- 기존 `ComplexHoleSizeThreshold`를 각 후보의 벽 내부 월드 면적 하한으로 사용한다.
- 연결된 개구부마다 `MaxSimpleConvexCount`를 적용한다. 기본값 3, 지원 범위 1~16.
- 격자 16×16, 시작점 최대 32회, 4방향/8방향 반평면 확장과 유한 이분 탐색으로 계산량을 제한한다. 최대 면적의 수학적 보장은 없다.
- 이미 볼록하고 내부 링이 없는 개구부는 원형을 그대로 사용한다. 완전 제거는 기존 예외를 보존한다.
- 후보는 원본 제거 영역 안에 있어야 하며 남아 있는 내부 섬을 침범하지 않는다. 겹침을 제외한 추가 벽 내부 면적이 큰 후보부터 선택한다.
- 면적 기준, 스케일, 개수 설정이 바뀌면 파생 캐시를 다시 만든다.
- 현재 체크아웃을 사용하고 사용자 소스/에셋 변경, 커밋, 푸시는 건드리지 않는다.

## 실행

- [x] 회귀 테스트: 전체 개구부 면적이 충분해도 각 Convex가 최소 면적보다 작으면 벽 충돌을 유지해야 한다. 지정 빌드 후 기존 구현에서 실패 확인. 기존 자동화 결과도 보존.
- [x] 탐색, 다중 결과 캐시, 개수 설정, 두 호출 경로 연결 구현. 내부 포함, 내부 섬, Convex, 개수 제한, 스케일/설정 캐시 무효화, 완전 제거 테스트 추가.
- [x] `BuildProject_Editor.bat` 실행. 좁은 테스트와 BreachSystem 자동화 실행 및 diff 검토. 실패는 새 회귀와 기존 실패를 구분하여 기록.

## 진행

- RED: 지정 빌드 성공. 새 SimpleConvex 테스트가 개별 Convex 최소 면적 판정 부재로 실패했다. 전체 75개 중 새 실패 1개와 기존 실패 8개를 `Saved/Automation/SimpleConvex-Red`에 기록했다.
- 최초 구현의 새 회귀 테스트 통과. 확장 끝점 검사와 이분 탐색 0.01 로컬 단위 종료를 추가했다. 대칭 방이 작은 마름모 해에 머무는 회귀를 확인해 4/8방향 탐색을 번갈아 사용하도록 변경했다.
- 별도 코드 검토에서 벽 밖의 오목한 팔 때문에 캐시/대체 경로의 임계 면적 판정이 달라지는 입력을 발견했다. 두 경로 모두 벽 안의 연결 개구부로 정규화하고 기존 경계 겹침 정리를 적용했다. 해당 회귀 입력도 SimpleConvex에 포함했다.
- Suite2에서 새 회귀 테스트와 450회 연속 절단 캐시 스트레스 통과. 새 면적 정책과 충돌하는 기존 기대값 3개(OutsideOverlap, SimpleRemainingRegions, IntegratedSnapshots)를 갱신했다. 스냅샷 검사는 남은 실제 벽 전체의 공간적 충돌 커버리지를 검사한다.
- 예제의 재계산 2회는 Suite2에서 합계 4.748ms. 일반적인 게임 프레임 비용이나 최악 시간 보장은 아니다.
- 최종 `BuildProject_Editor.bat`: 성공. 전체 75개 중 68개 성공(경고 포함), 7개 실패. 새 실패 없음. 새 SimpleConvex, 기존 450회 절단 Stress, IntegratedSnapshots, SimpleRemainingRegions 모두 성공. `Saved/Automation/SimpleConvex-Verified/index.json`에 결과를 보존했다.
- 남은 실패는 변경 전부터 있던 `BreachSystem.Surface.SimpleCollision.Connected`, `.FullRemoval`, `BreachSystem.Surface.SimpleCollisionThreshold.Below`, `.Detached`, `.Equal`, `.FullRemoval`, `BreachSystem.Surface.SimpleCollisionCache`이다. 임계값 동일 값/완전 제거 기대와 비활성화된 충돌 조각 병합 기대에 해당하며 이번 작업에서 바꾸지 않았다.
- 최종 예제 재계산 2회 합계 4.475ms. 이 측정은 해당 Editor Development 예제의 충돌 생성 비용이며 PIE 성능이나 최악 시간 보장이 아니다. PIE/멀티플레이/쿠킹은 실행하지 않았다.
- 별도 최종 검토에서 추가 수정 사항 없음. `git diff --check` 성공. 사용자 에셋/컴포넌트 소스 변경은 보존했으며 커밋/푸시하지 않았다.
- 위 Verified 보고서의 경고를 RED와 비교해 MirroredScale/ComplexCut에서 새 Chaos hull 실패 경고를 발견했다. 단순 float 넓이 검사는 통과하지만 Chaos와 동일한 `TConvexHull3<float>` 검사는 실패하는 회귀를 재현했다(삼각형 cross 제곱 1.44e-15). 성공 집계만으로 완료 처리하지 않았다.
- `BuildPrisms`에서 Chaos가 평면으로 판정하는 조각만 공유 변을 가진 이웃과 볼록 합집합으로 병합한다. 병합 후 hull 검사와 면적 보존 검사를 통과해야 하며, 복구 불가 시 false/명시적 오류로 기존 충돌 보존 경로를 따른다. 일반적인 충돌 조각 병합은 활성화하지 않는다.
- 복구 코드 포함 지정 빌드 성공. 새 테스트는 U자/톱니 개구부에서 실제 선택 영역을 뺀 벽 면적이 보존되는지와 모든 출력 prism의 단정밀도 hull 성공을 검사한다.
- 최종 확정 보고서 `Saved/Automation/SimpleConvex-Final/index.json`: 총 75개, 성공 58개, 경고 포함 성공 10개, 기존 실패 7개. 새 실패 없음. 새 Chaos zero-particle/geometry 누락/복구 불가 경고 없음. 예제 재계산 2회 합계 4.696ms. 별도 최종 코드 검토에서도 복구의 면적 보존·단일 소비·실패 시 미반영 계약에 추가 문제 없음.
