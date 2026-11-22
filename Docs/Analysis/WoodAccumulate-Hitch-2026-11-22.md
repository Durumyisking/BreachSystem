# 나무 벽 누적 파손 히칭 분석

## 결론

LV_Test의 맨 왼쪽 나무 벽(`B_BreachableWall`)에 큰 누적 파손이 한 번 일어나면 게임 스레드에서 **47.5ms**가 걸린다. 같은 실행에서 대기 프레임 중앙값은 14.9ms였고, 피격이 든 프레임은 51.7ms였다.

- 약 **37ms(78%)** 는 나무결(`GrainGuides`) 계산이다. 결 찢김 생성 33.2ms와, 같은 결과를 한 번 더 검사하는 3.9ms를 합한 값이다.
- 나머지 약 **10ms** 는 재질과 관계없이 생기는 반영 비용이다. 그중 Deco가 5.3ms로 가장 크다.
- 나무결 비용은 구멍 크기보다 훨씬 빠르게 늘어난다. 결 선을 커터보다 넓이 기준 약 22배 넓은 범위에서 모으고(Padding 20에서는 약 32배), 그 범위 전체로 레인과 띠를 만들기 때문이다.
- `AccumulationShape.Padding = 100cm`는 크게 부수려는 의도된 설정이며 원인으로 보지 않는다. 원인은 큰 파손일수록 과도하게 비싸지는 계산 구조다.

## 측정 조건

- 코드: `0731e5f` 위에 측정 구간을 추가한 작업 트리. 측정 구간과 측정 테스트는 이 문서를 쓸 때 커밋되지 않은 상태였다.
- 실행: `UnrealEditor-Cmd` Development, `-nullrhi`, LV_Test를 별도 프로세스 메모리에서만 열고 Simulate PIE로 실행했다. 맵은 저장하지 않았다.
- 벽: `B_BreachableWall`, Cube 메시, 스케일 약 `(2.34, 0.05, 3.12)`, 크기 약 234×312cm, PlayerStart 기준 왼쪽 240cm.
- 재질: `SurfaceMaterial001_Wood`, 패턴 `BPAT_Wood`(GrainGuides, 결 선 10개, 점 90개), `GrainInfluence=1.0`, `bUseWorldSizeUV=True`, `TileSizeCm=100`, 누적 판정 창 40×40cm, `BreachHP=1`, `Padding=100`.
- 찢김 설정: `TearLengthMin=-0.05`, `TearLengthMax=0.2`, `MinTearLength=0.1`, `MaxSplits=1`, `MaxSpikes=4`.
- 충돌 설정: `bAsyncComplexCollision=False`, `MaxSimpleConvexCount=4`.
- 무기: 누적 피해가 있는 무기는 `BP_ShooterWeapon_HitScanPistol`뿐이다. `BreachDamage=0.17`, 원형 커터 반지름 1cm, 8분할. `BP_ShooterProjectile_Bullet`은 `BreachDamage`가 0이라 누적에 들어가지 않는다.
- 사격: 벽 앞면 중앙을 6cm 반경 안에서 무작위(시드 7)로 0.15초마다 40발 쐈다. 6번째 발에서 누적 파손이 일어나 기록 5개가 모두 소모된다. 이후 메시는 삼각형 1,864개다.
- 같은 PC에서 다른 프로세스 부하 때문에 대기 프레임이 실행마다 12~15ms로 달랐다. 절대값보다 단계별 비율을 기준으로 본다.

## 단계별 시간 (Padding 100, 게임 스레드)

```text
PlanarBreach | Total                                47.52ms
  PlanarBreach | Accumulated cut                    38.89ms
    Breach Pattern | Generate cutter                33.44ms
      Generate contour                               0.10ms
      Gather grain guides                            0.11ms
      Build grain tear                              33.21ms
        Join grain lines                            13.50ms
        Clip tear strip (x120)                       7.53ms
        Merge tear pieces                            7.86ms
        Validate regions                             3.88ms
    PlanarBreach | Cut regions                       5.34ms
      Validate regions (같은 결과를 다시 검사)       3.86ms
      Subtract cutters                               0.14ms
      Rebuild wall region mesh (x30)                 1.20ms
      RebuildHoleCache                               0.12ms
  PlanarBreach | Publish wall update                 7.58ms
    Build mesh normals and UVs                       0.31ms
    Set proxy mesh                                   0.07ms
    Complex sync cook and apply                      1.84ms
      FChaosDerivedDataCooker::Build                 1.43ms
    Update decorations                               5.34ms
      Clip cut patches                               0.48ms
      Update feature bindings                        0.68ms
      Build displays                                 4.17ms
        Triangulate region (x22)                     2.18ms
        (측정 구간 없음)                             1.90ms
  PlanarBreach | Spawn debris                        0.95ms
  PlanarBreach | Breached event                      0.09ms
```

피격 뒤 Simple 충돌은 워커 스레드에서 `BuildConvexFromCuts` 32.5ms가 걸렸다. 게임 스레드로 돌아와서 쓰는 시간은 `Complete build` 0.06ms, `Complete cook and publish` 0.10ms뿐이라 두 번째 히칭은 없다.

## 크기별 비교

같은 벽과 같은 탄착에서 Padding만 바꿨다. 에셋은 그대로 두고 PIE 벽에만 복제 재질을 썼다.

| Padding | 피격 처리 | 결 찢김 | 결 선 잇기 | 자른 띠 수 | 대기 프레임 |
|---|---|---|---|---|---|
| 100 | 47.52ms | 33.21ms | 13.50ms | 120 | 14.9ms |
| 50 | 13.92ms | 6.04ms | 0.66ms | 61 | 14.7ms |
| 20 | 4.72ms | 1.26ms | 0.04ms | 26 | 11.9ms |
| 10 | 3.03ms | 0.58ms | 0.02ms | 10 | 14.7ms |

Padding을 50에서 100으로 올려 커터가 약 2배가 되자, 전체는 3.4배, 결 선 잇기는 약 20배로 늘었다.

## 결 찢김 내부 개수

임시 로그로 한 번 측정하고 로그는 되돌렸다.

| 항목 | Padding 100 | Padding 20 |
|---|---|---|
| 커터 크기 / 넓이 | 215×218cm / 32,280cm² | 54×47cm / 1,617cm² |
| 결 선 수집 범위 | 900×782cm (커터 넓이의 21.8배) | 300×173cm (32.1배) |
| 결 선 / 이은 줄 / 레인 | 711개(변 5,688) / 79줄 / 78개 | 54개(변 432) / 18줄 / 17개 |
| 레인 정점 / 레인 넓이 합 | 22,464개 / 701,985cm² | 1,632개 / 51,449cm² |
| 조각이 나온 띠 / 남은 조각 / 결과 정점 | 31 / 158 / 601 | 7 / 11 / 116 |

## 원인

### 1. 결 선을 너무 넓게 모은다

`BreachPatternGeometry::Generate`는 결 선을 모을 때 커터 양쪽에 `커터 크기 × (1 + 2 × TearLengthMax)` 만큼 여유를 둔다. `TearLengthMax=0.2`이면 한쪽에 1.4배이고, 수집 범위는 커터의 약 3.8배가 된다. 범위에 걸치는 타일은 통째로 들어가므로 실제로는 더 넓어진다.

실제 찢김은 띠 끝마다 구멍 길이의 `TearLengthMax × GrainInfluence` 배까지만 뻗는다. 따라서 필요한 여유는 0.2배에 윤곽 흔들림을 더한 정도다. 끝 하나당 여유를 써야 할 자리에 전체 길이 배율을 쓴 것으로 보이지만, 이는 코드를 읽고 한 해석이다.

이 범위의 대부분은 커터 밖이고, 이번 벽에서는 벽 밖이기도 하다. 그 결과는 모두 버려진다.

### 2. 결 선 잇기가 개수의 세제곱에 가깝게 늘어난다

`BuildChains`는 끝점 하나를 이을 때마다 처음부터 다시 모든 줄의 시작점과 거리를 비교한다. 결 선 711개를 79줄로 이으려면 이 비교를 수백 번 반복한다.

### 3. 커터와 겹치지 않는 띠까지 자른다

레인은 이어 붙인 결 줄의 전체 길이(약 782cm)로 만들어진다. 레인 하나에 평균 정점이 288개다. 모든 레인의 모든 띠를 커터와 교차 연산하는데, 120개 중 89개(74%)는 결과가 비어 있다.

### 4. 조각 합치기에 전체 레인을 넣는다

`PolygonsDifference(커터, 레인 78개)`에 레인 정점 22,464개가 들어간다. 그 뒤 띠 조각 31개와 남은 조각 158개를 `PolygonsUnion`으로 합친다. 남은 조각 158개는 레인 경계 사이의 가는 틈에서 생긴 자투리로 추정하지만, 확인하지는 않았다.

### 5. 영역 검증이 정점 수의 제곱이고, 두 번 한다

`ValidateRegions`의 `SimplePath`는 외곽의 모든 변 쌍을 검사한다. 결과 정점이 601개면 약 18만 쌍이다. 같은 결과를 결 찢김 끝과 `CutRegions` 시작에서 두 번 검사한다.

### 6. Deco가 벽의 삼각분할을 다시 한다

절단 장식 패치는 `발자국 ∩ 벽 − 구멍`으로 만들어진다. 큰 구멍에서는 이것이 방금 다시 만든 벽의 남은 영역과 거의 같다. 그 22개 영역을 다시 삼각분할하는 데 2.18ms가 걸린다.

그중 한 영역(1.24ms)은 경계 변을 다 넣지 못해 감싸는 횟수 대체 판정까지 돌았다. 엔진 `FDelaunay2::GetFilledTrianglesGeneralizedWinding`의 ParallelFor 0.92ms가 여기 들어 있다. `Build displays`의 나머지 1.90ms는 측정 구간이 없어 원인을 확인하지 못했다.

### 7. Complex 충돌을 피격 프레임에 동기로 굽는다

`bAsyncComplexCollision=False`라서 삼각형 1,864개의 Chaos 쿠킹(1.43ms)과 물리 상태 재생성을 피격 프레임에서 한다.

### 8. 벽 메시와 파편

벽 메시는 남은 영역 30개를 매번 모두 다시 삼각분할한다(1.20ms). 노멀과 UV에 0.31ms가 더 든다. 파편은 액터를 매번 새로 생성한다(0.95ms).

## 최적화 후보

효과는 아직 측정하지 않은 추정이다. 하나씩 적용하고 같은 측정 테스트로 다시 잰다.

### 나무결

1. **수집 범위 줄이기.** 결 선을 실제로 닿는 범위(커터 + 찢김 최대 길이 + 윤곽 흔들림)에서만 모으고, 벽 경계로도 자른다. 결 선 잇기, 띠 자르기, 조각 합치기가 함께 줄어든다. 다만 레인 번호가 바뀌어 같은 탄착이어도 무작위 결 모양이 한 번 달라진다.
2. **결 선 잇기를 한 번에.** 시작점을 위치로 정렬하거나 격자에 넣어, 다시 처음부터 훑지 않고 이어 간다. 모호한 이음을 거르는 기존 규칙(가장 가까운 후보가 두 번째보다 4배 가까울 것)과 결과가 같은지 확인해야 한다.
3. **커터와 겹치는 레인과 띠만 만들기.** 레인 정점도 필요한 길이 구간만 만든다. 찢김 끝을 자르는 레인의 실제 끝(`Low`, `High`)은 그대로 써야 결과가 달라지지 않는다.
4. **검증을 한 번만, 빠르게.** 결 찢김 끝에서 검증한 결과는 `CutRegions`에서 다시 검사하지 않는다. 변 쌍 전체 대신 격자로 가까운 변끼리만 비교한다.
5. **구조 변경: 워커 스레드로 옮기기.** 패턴 생성은 UObject를 바꾸지 않는 순수 기하 계산이라 워커로 옮길 수 있다. 피격 프레임의 히칭은 사라지지만 구멍이 1~2프레임 늦게 생기고, 그 사이 들어온 피격을 어떻게 처리할지 정해야 한다. 게임 동작이 바뀌므로 설계 결정이 필요하다.

### 나머지

1. **Deco가 벽 삼각분할 재사용.** 장식 패치 영역이 벽의 남은 영역과 같을 때는 벽에서 만든 삼각형을 다시 쓴다. 중복 계산과 대체 판정을 함께 피한다.
2. **Complex 충돌.** 이미 있는 비동기 설정을 켜면 피격 프레임에서 빠진다. 대신 쿠킹이 끝날 때까지 몇 프레임 동안 이전 충돌이 남는다. 삼각형 수를 줄이는 방법도 있다.
3. **벽 메시는 바뀐 영역만 다시.** 작은 구멍에서 효과가 크고, 큰 구멍에서는 대부분이 바뀌어 효과가 작다.
4. **파편 풀링.** 파편 액터를 미리 만들어 두고 다시 쓴다.
5. **Deco의 측정 구간 없는 1.90ms**에 구간을 추가해 원인부터 확인한다.

### 공통

결 찢김 결과의 외곽 정점(601개)을 줄이면 검증, 벽 삼각분할, Complex 쿠킹, Deco가 함께 가벼워진다.

## 나무결 적용 후 (2026-11-22)

나무결 후보 중 2, 3(띠 부분), 4(검증 가속)를 적용했다. 수집 범위(후보 1)는 그대로 두었고, 워커 이전(후보 5)과 레인 정점 자르기는 하지 않았다. 같은 측정 테스트, Padding 100 기준이다.

| 구간 | 적용 전 | 적용 후 |
|---|---|---|
| 피격 처리 | 48.03ms | 10.29ms |
| 결 찢김 (`Build grain tear`) | 33.79ms | 4.12ms |
| 결 선 잇기 | 13.70ms | 0.60ms |
| 띠 자르기 | 7.72ms (x120) | 1.55ms (x33) |
| 조각 합치기 (끝 검증 포함) | 7.77ms | 0.91ms |
| 영역 검증 (두 번 합계) | 7.85ms | 0.13ms |

Padding 20은 피격 처리 5.03ms → 4.81ms, 결 찢김 1.65ms → 0.73ms다. 벽 삼각형 수는 Padding 100에서 1,864개, Padding 20에서 480개로 그대로다.

Deco와 메시 반영도 30~45% 줄었다. 하지만 손대지 않은 구간(벽 메시 재생성, Complex 쿠킹)도 같은 비율로 줄어서, 측정 환경 차이로 본다.

### 바꾼 것

1. **윤곽과 겹치지 않는 레인 건너뛰기.** 레인의 결 좌표 범위가 윤곽 범위와 떨어져 있으면 띠를 만들기 전에 넘어간다. 레인마다 난수를 따로 시드하므로 남은 레인의 찢김은 그대로다.
2. **레인을 서로 살짝 겹치기.** 이웃 레인이 공유 결 선을 서로 다른 표본에서 계산해서, `윤곽 − 레인`에 폭 1e-13cm 수준의 자투리가 생겼다. 위 "남은 조각 158개"가 모두 이것이었다. 레인 폭을 조각 겹침의 절반(`Size × 5e-6`)만큼 넓혀 자투리를 없앴다.
3. **결 선 잇기.** 시작점을 격자에 넣고, 다시 볼 줄만 최소 힙에 넣는다. "번호가 가장 작고 이을 수 있는 줄부터 잇는다"는 기존 결정 순서와 동점 처리를 그대로 재현한다.
4. **`SimplePath` 변 쌍 검사.** tolerance만큼 넓힌 변 상자가 겹치는 쌍만 기존 판정식으로 검사한다. 상자가 안 겹치면 기존 판정도 거짓이라 결과가 같다. 이중 검증은 그대로 두었다.

### 분석 정정

- **원인 1은 절반만 맞다.** 띠 끝의 가시는 바깥쪽으로 띠 길이의 최대 0.45배까지 뻗는다. 결이 대각선이면 결 방향 구멍 길이가 `Size`의 √2배까지 된다. 따라서 "필요한 여유는 0.2배 + 윤곽 흔들림"은 부족하다. 윤곽은 수집 전에 이미 있으므로 흔들림 여유는 필요 없다.
- **자투리는 이미 있던 거부 버그였다.** 자투리 하나가 떨어진 두 조각을 폭 0인 다리로 이으면, 결과가 `Pattern path intersects or touches itself`로 거부되고 피해는 남는다(구멍이 안 생김). 입력 범위가 바뀌어 양자화가 1e-13cm만 달라져도 걸리는 경우가 바뀐다. 1번만 적용했을 때 `BreachSystem.Pattern.Editor.WoodPreview`의 한 경우(중심 0.5, 시드 3, 영향 1)가 이렇게 실패했다. 2번을 적용한 뒤에는 통과한다.
- **조각 합치기 7.86ms에는 끝의 영역 검증 3.88ms가 들어 있었다.** 측정 구간이 함수 끝까지 이어지기 때문이다. 단계별 시간 표의 두 줄은 형제가 아니라 부모와 자식이다.
- **"띠 89개가 비어 있다"는 확인되지 않은 수치다.** 로그는 조각이 나온 띠만 셌다. 그래서 윤곽과 겹쳤지만 `MinTearLength`로 건너뛴 띠도 89개에 섞여 있을 수 있다.

### 검증

- 회귀 테스트 `BreachSystem.Pattern.LineTear.LargeCut`: 타일 여러 장에 걸친 큰 구멍(직선 매핑, 회전 매핑, 작은 구멍)의 영역 수, 정점 수, 넓이, 경계를 적용 전 값으로 고정했다. 적용 후에도 비트 단위로 같다.
- `BreachSystem` 자동화 테스트 122개가 모두 통과한다. 경고 14개는 적용 전과 같은 목록이다. `-BreachPatternDisposablePIE`, `-BreachDisposablePIE`로 Pattern·Decoration PIE도 실제로 돌려 통과했다.

### 남은 나무결 비용

결 찢김 4.1ms 중 띠 자르기가 1.6ms, 조각 합치기가 0.9ms, 결 선 잇기가 0.6ms다. 나머지 약 1ms는 레인 구성(`AcrossAt` 선형 탐색)이다. 레인과 띠를 윤곽의 결 방향 범위로 자르면 띠 자르기를 더 줄일 수 있다. 다만 boolean 입력이 바뀌므로 위 양자화 민감도를 다시 확인해야 한다.

## 비동기 브리칭 (2026-11-22)

피격의 절단·메시·Deco 계산을 작업 스레드로 옮겼다. 게임 스레드는 피격 순간 입력만 담고, 계산이 끝나면 메시·충돌·Deco·파편·`OnBreached`를 한 번에 반영한다. Project Settings > Breach System > Performance > **Async Breach**(기본 켬)로 끌 수 있고, 끄면 같은 계산을 피격한 프레임에 실행한다.

같은 측정 테스트, Padding 100, 가장 큰 누적 파손 한 번이다. 이 실행에서는 모든 구간이 최적화 직후보다 약 1.5배 느렸다(대기 프레임 14ms). 그래서 같은 실행 안의 동기 결과와 비교한다.

| 구간 | 동기 | 비동기 | 비동기 + Async Complex |
|---|---|---|---|
| 피격 프레임 (게임 스레드) | 16.00ms | 0.12ms | 0.12ms |
| 작업 스레드 계산 | 없음 | 18.84ms | 13.54ms |
| 반영 (게임 스레드) | 위에 포함 | 2.96ms | 1.27ms |
| 피격 시작 → 반영 끝 | 16.0ms | 31.5ms (+3프레임) | 15.7ms (+1프레임) |

- 반영 2.96ms 중 Complex 쿠킹이 1.84ms, 파편 생성이 0.81ms다. Async Complex를 함께 켜면 쿠킹이 0.08ms로 빠지고, 파편 0.87ms가 가장 크다.
- 작업 스레드 계산 시간은 실행마다 13~19ms로 흔들렸다. 벤치는 `-nullrhi`라 프레임이 짧게 돌아서 프레임 수는 60fps 기준보다 많게 나온다. 60fps(16.7ms)로 환산하면 1~2프레임이다.
- Padding 20은 피격 프레임 0.12ms, 반영 1.76ms, 지연 13.9ms(+1프레임)다.
- 삼각형 수는 세 설정 모두 1,864개로 같다.

### 동작 규칙

- 표면마다 작업은 하나만 실행한다. 실행 중 들어온 피격은 다음 작업 하나에 모아, 반영된 상태에 이어서 순서대로 처리한다. 결과는 동기로 하나씩 처리한 것과 같다(`BreachSystem.Surface.AsyncBreach.MatchesSync`).
- 반영 전에는 이전 메시와 충돌이 남는다. 그 사이 총알은 이전 벽에 맞고, 그 피격은 반영된 상태 기준으로 다시 판정된다.
- `ResetSurface`나 등록 해제 뒤 도착한 결과는 버린다. 파괴할 때는 계산이 끝날 때까지 기다린다(`BreachSystem.Surface.AsyncBreach.Lifetime`).
- `ApplyCutterPolygon`(동기 API)은 기다리던 피격을 먼저 반영한다.
- 계산 중 끝난 Simple 충돌 결과는 반영 때 `FBreachSimpleCollision::CommitBuild`로 합쳐 잃지 않는다.
- 피해 직후 결과를 확인하는 기존 테스트는 `FBreachSyncBreachGuard`로 동기 처리에 고정했다.

### 남은 게임 스레드 비용

반영 프레임의 파편 생성(약 0.8ms)과, Async Complex를 끈 경우의 Complex 쿠킹(약 1.8ms)이 크다. 파편 풀링과 Async Complex 기본값은 별도로 결정한다.

## 측정하지 못한 것

- `-nullrhi`라 렌더링과 GPU 비용은 빠져 있다. 표시 메시 재생성, Deco 표시, 벽 Blueprint가 붙이는 Niagara와 데칼이 여기에 해당한다.
- Padding 100에서는 누적 파손 한 번에 벽이 거의 사라져, 실행마다 표본이 하나뿐이다.
- 콘크리트(패턴 없음)와 벽돌(Regions 패턴)의 큰 구멍 비용은 측정하지 않았다.

## 재현 방법

빌드는 저장소 루트의 빌드 스크립트로 한다. 측정 테스트는 `-BreachWoodBench`가 있을 때만 돈다.

```powershell
& "<UE>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "D:\Project\BreachSystem\BreachTest.uproject" '-ExecCmds=Automation RunTests BreachSystem.Bench.WoodAccumulate; Quit' '-testexit=Automation Test Queue Empty' -unattended -nullrhi -nosplash -nosound -BreachWoodBench -BreachBenchLabel=<이름> '-trace=cpu,frame,bookmark,counters' '-tracefile=<경로>\wood.utrace' '-ReportExportPath=<경로>'
```

- 선택 인자: `-BreachWoodPadding=<cm>`, `-BreachWoodShots=<발 수>`, `-BreachWoodInterval=<초>`, `-BreachWoodSpread=<cm>`, `-BreachWoodAimX=<cm>`, `-BreachWoodAimZ=<cm>`, `-BreachWoodWall=<액터 라벨>`.
- 발별 기록은 `Saved/Benchmarks/WoodAccumulate-<이름>.csv`에 저장된다.
- 단계별 이벤트는 Insights로 내보낸다. `-ExecOnAnalysisCompleteCmd`의 값은 `=` 바로 뒤에 따옴표가 와야 한다.

```text
UnrealInsights.exe -OpenTraceFile="<경로>\wood.utrace" -AutoQuit -NoUI -ExecOnAnalysisCompleteCmd="TimingInsights.ExportTimingEvents <경로>\events.csv -columns=ThreadName,TimerName,StartTime,EndTime,Duration,Depth -timers=*Breach*"
```

## 관련 파일

- 측정 테스트: `Plugins/BreachSystem/Source/BreachSystem/Private/Tests/BreachWoodAccumulateBenchTest.cpp`
- 결 찢김: `Plugins/BreachSystem/Source/BreachSystem/Private/Surface/BreachGrainFracture.cpp`
- 결 선 수집과 검증: `Plugins/BreachSystem/Source/BreachSystem/Private/Surface/BreachPatternGeometry.cpp`
- 절단과 반영: `Plugins/BreachSystem/Source/BreachSystem/Private/Components/BreachableSurfaceComponent.cpp`
- Deco: `Plugins/BreachSystem/Source/BreachSystem/Private/Components/BreachDecorationComponent.cpp`, `Private/Surface/BreachDecorationGeometry.cpp`
- 원시 자료: `Saved/Diagnostics/WoodAccumulate20261122/`
  - `breach-events-p100a.csv`, `-p50`, `-p20`, `-p10`: Padding별 `*Breach*` 이벤트
  - `gamethread-hit-p100a.csv`: Padding 100 피격 구간의 게임 스레드 전체 이벤트
  - `report-*.json`: 자동화 테스트 결과와 벽·재질·무기 설정
  - `grain-counts.txt`: 결 찢김 내부 개수 임시 로그
