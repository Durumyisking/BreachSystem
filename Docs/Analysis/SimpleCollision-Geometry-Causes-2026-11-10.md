# SimpleCollision 기하 오류 원인 분석

## 확인한 결론

현재 코드의 연속 절단 재현 입력에서 세 현상을 모두 확인했다.

1. 기존 `RemainingRegions`의 구멍 경계와 캐시된 `Holes`의 경계가 수치적으로 조금 달라진 상태에서, 이미 제거된 영역을 포함한 구멍을 다시 빼면 극도로 가까운 정점과 얇은 조각이 생긴다.
2. Clipper의 정수 결과를 Unreal의 `UnpackVector`가 `double`로 복원하는 단계에서, 서로 다른 정수 정점이 같은 좌표로 합쳐지거나, 교차하지 않던 변이 교차하는 사례가 있다.
3. `double`에서는 서로 다른 정점으로 남은 작은 삼각형도 Chaos의 `float` 변환에서 퇴화할 수 있다.

따라서 조사한 사례에서 `bExpanded` 루프 자체가 정점 좌표를 망가뜨린 것은 아니다. 이 루프가 캐시된 구멍을 다시 포함하면서, 현재 벽의 구멍 경계와 캐시 경계 사이의 작은 수치 차이를 후속 Boolean 연산이 처리하게 되는 것이 재현 조건이다.

이 문서는 **현재 소스에 대한 재현 실험**의 결과다. 첨부된 플레이 로그에는 Chaos의 `float` 입력만 있으므로, 당시의 모든 `double` 입력과 발사 순서까지 복원한 결과로 간주하지 않는다.

## 실험 조건과 증거

- 설치 엔진: `F:/UE_5.8.2`.
- 벽 XZ 범위: `[-50,50] × [-50,50]`, 두께 방향 Y: `[-50,50]`.
- Simple 면적 임계값: `1000 cm²`.
- 실제 `FBreachSimpleCollision::ApplyCut` → `ApplyCutDebug` 실행. 연산을 대체한 모형으로 Simple 결과를 만들지 않았다.
- `bExpanded=true`, `ClearHoles` 실행, `Simplify` 비활성화, 두 `PolygonsIntersection` 실행, Convex 병합 비활성화인 현재 소스 기준.
- 테스트 전용 observer로 ClippedCutter, IncomingWithDetached, IncomingWithCachedHoles, Union, ClearHoles, SafeIntersection, Difference를 보존했다.
- Detached는 Surface의 사각 벽 경계 판정과 같은 조건으로 별도 계산했다. 전체 gameplay의 Surface 메시 재구성·물리·네트워크를 재생한 실험은 아니다.
- 입력: 32각형 원형/거친 원형 연속 절단. 원형 링 2개 시나리오 각 48회, 난수 시드가 다른 8개 시나리오 각 128회, 총 1120회.
- 난수 시드: 시나리오 0~2는 71, 이후 `71 + (Scenario - 2) * 97`. 시나리오 6은 459.
- `MissingEdges`가 발생한 단계 125회, 성공한 삼각분할 중 `float` 변환 시 면적 0인 삼각형이 관찰된 단계 745회. 이는 독립 오류 개수가 아니라 연속 실행에서 해당 상태가 관찰된 단계 수다.

소스: `Plugins/BreachSystem/Source/BreachSystem/Private/Tests/BreachGeometryDiagnosisTest.cpp`.

원시 증거는 `Saved/Diagnostics/GeometryCauses20261110/`에 있다.

- `report.txt`: 각 대조 실험, 누락 경계, `double`/`float` 삼각형, Hull 차원.
- `scenario*_step*.txt`: 각 연산 단계의 좌표를 `%.17g`로 저장한 입력/출력.
- `exact_analysis.json`: 저장된 double 값을 정확한 유리수로 취급한 변 교차 검사.
- `integer_analysis.json`: Clipper 정수 출력과 Unreal double 복원 결과 비교.
- `clipper_probe.cpp`, `build_clipper_probe.bat`, `analyze_integer.py`: 정수 출력 분리 실험.

## 1. 왜 기존 구멍을 다시 처리할 때 문제가 나타나는가

현재 경로는 다음 두 상태를 별도로 보관한다.

- `Holes`: Union 등으로 가공한 제거 영역의 캐시.
- `RemainingRegions`: 최종 Difference를 거쳐 남은 벽. 구멍 경계는 이 결과 안에 들어 있다.

이상적인 실수 계산에서는 같은 경계라도, 서로 다른 Boolean 연산을 거치면 좌표가 반드시 같은 비트로 유지되지는 않는다.

설치 엔진의 `GeometryAlgorithms/Private/Curve/PolygonIntersectionUtils.cpp`는 Boolean 호출마다 입력 전체의 최소 좌표와 최대 범위를 다시 구한다. `ClipperUtils.inl`은 다음 방식으로 정수 좌표에 넣고 결과를 복원한다.

```text
정수화: int64((좌표 - InputMin) / InputRange * 9007199254740991)
복원:   double(정수) / 9007199254740991 * InputRange + InputMin
```

캐시 구멍을 만드는 연산과 벽을 잘라내는 연산의 입력 범위가 다르므로, 같은 경계가 서로 다른 정규화·복원 과정을 거친다. 또한 반복적인 연산에서 이미 복원된 좌표가 다시 정수화된다.

### 실제 관찰: 시나리오 0, step 21

동일 경계에 대응하는 두 좌표가 다음처럼 달랐다.

```text
RemainingBefore의 경계:
(-10.999997725501096, -23.552560165458310)

재사용되는 제거 영역의 경계:
(-10.999997725501085, -23.552560165458303)
```

벽 기준 `InputMin=(-50,-50)`, `InputRange=100`으로 정수화하면:

```text
기존 경계: (3512807914217634, 2382173603674911)
재사용 경계: (3512807914217635, 2382173603674911)
```

의도상 같은 경계가 정수 공간에서는 1단위 차이 난다. 이때 1단위는 약 `1.1102230246251567e-14 cm`다. 이런 경계들을 다시 Difference하면 아주 짧은 변과 좁은 영역이 생길 수 있다.

### 동일 입력 대조 실험

같은 `RemainingBefore`를 출발점으로 했다.

| 뺄 영역 | float에서 면적 0이 된 삼각형 |
|---|---:|
| 새 Cutter만 | 0 |
| 새 Cutter + 기존 캐시 구멍, Union 생략 | 3 |
| Union 결과 | 3 |
| SafeIntersection 결과 | 3 |

같은 최종 SafeIntersection 결과를 **기존 Remaining이 아닌 원본 벽에서 한 번 빼면 0개**였다.

이 사례에서는 `Union` 또는 뒤쪽 `Intersection` 하나만 제거해도 문제가 남는다. 캐시 구멍을 포함해 이미 잘린 벽을 다시 자르는 입력 조합만으로 재현되기 때문이다. 이것이 앞서 개별 함수를 꺼도 오류가 남을 수 있었던 구체적인 재현 사례다.

이 대조 실험은 발생 조건을 분리하기 위한 것이다. 원본 벽에서 매번 재구축하는 구현을 해결책으로 확정한 것은 아니다.

## 2. 정확히 어디에서 정점이 같아지는가

서로 다른 두 변환 단계에서 좌표가 합쳐졌다.

### A. Clipper 정수 → Unreal double

시나리오 2, step 58의 Clipper 출력에는 아래 두 정점이 서로 달랐다.

```text
정점 195: (1899043987485508, 6863021301612701)
정점 196: (1899043987485508, 6863021301612700)
```

`UnpackVector`로 복원한 결과는 둘 다 다음 좌표였다.

```text
(-28.916376403176212, 26.194842675433321)
```

정수 출력에는 중복 정점이 없었지만 복원 결과에는 인접한 중복 정점이 1쌍 생겼다. 복원식의 나눗셈·곱셈·덧셈 반올림 때문에 이 매핑은 모든 정수 정점에 대해 일대일이 아니다.

검증 방법: 설치된 Clipper 엔진 소스를 수정하지 않고 별도 실행 파일로 컴파일했다. Unreal 의존성 중 `int64` 타입 정의만 최소 shim으로 제공했다. 같은 저장 입력과 같은 정수화·복원식을 적용했을 때 **출력 루프 순서와 모든 double 좌표가 실제 Unreal 실행의 저장 결과와 정확히 일치함을 assert**했다.

### B. Unreal double → Chaos float

시나리오 0, step 21에서 다음 두 정점은 double로는 서로 달랐다.

```text
(-5.6940178237418309, -16.750368544167699)
(-5.6940178237418237, -16.750368544167699)
```

float 변환 후에는 둘 다 같아진다.

```text
(-5.69401789, -16.7503681)
```

`FKConvexElem::VertexData`에는 double `FVector`로 넣지만, `Engine/Private/PhysicsEngine/Experimental/ChaosCooking.cpp`의 `BuildConvexMeshes`가 `FConvex::FVec3Type`으로 변환한다. 설치본 Convex 빌더의 `FRealType`은 `FRealSingle`이다.

따라서 Chaos 로그에 중복이 있다고 해서 원래 double 폴리곤에도 동일한 중복이 있었다고 판단할 수 없다.

## 3. 왜 작은 삼각형이 만들어지는가

시나리오 0, step 21의 입력 Cutter, 기존 벽, Union, SafeIntersection에는 같은 폴리곤 안에서 서로 다른 정점 간 최소 거리가 약 `0.176597246 cm` 이상이었다. 마지막 Difference 결과에는 `7.105427357601002e-15 cm` 간격의 정점 쌍이 생겼다.

FDelaunay2는 전달된 경계 정점과 변을 반영해 삼각형을 만든다. 이 사례에서는 double로 계산한 삼각형 면적이 `3.1086244689504383e-15 cm²`였다. 작지만 0은 아니므로 삼각분할 자체는 성공했다.

이후 경로:

```text
극도로 가까운 double 경계 정점
→ 이를 포함하는 작은 삼각형
→ Y=-50, +50으로 확장한 6개 정점
→ Chaos용 float 변환에서 밑면 정점 합쳐짐
→ 6개 정점이 평면 또는 선으로 퇴화
→ TConvexHull3::Solve 실패, Dimension=1 또는 2
```

현재 `TriangulateRegion`은 개별 삼각형 면적에 대해 유한수인지만 검사한다. 전체 면적 비교의 절대 허용치는 최소 `1e-6 cm²`다. 극소 삼각형이 통과하는 이유지만, 이 검사가 극소 삼각형을 생성하는 원인은 아니다.

### InflateApplied=1인데 왜 변화가 없는가

설치 엔진 `FConvexBuilder::IsPlanarShape`는 첫 세 정점으로 `TPlane`을 만든다. 첫 세 정점이 겹치거나 극도로 가깝다면 평면 법선이 0이 될 수 있다. `TPlane` 생성자는 외적에 `GetSafeNormal()`을 적용한다.

이때 Inflate는 `Source + Normal * Thickness`를 계산하므로, 법선이 0이면 Thickness가 `0.1`이어도 기존 좌표를 복제할 뿐이다. 관찰된 진단 입력에서 `plane_normal=(0,0,0)`과 Hull 실패를 확인했다. 따라서 로그의 `Used=12`는 유효한 입체 정점 12개가 생겼다는 뜻이 아니다.

## 4. 왜 MissingEdges가 발생하는가

재현 입력에서 서로 다른 두 경우를 확인했다.

### A. 복원 중 합쳐진 인접 정점이 길이 0인 경계선을 만듦

시나리오 2, step 58:

```text
HasDuplicates=true
누락 경계: (194,195), (195,196)
195와 196의 double 좌표가 완전히 같음
```

입력은 여전히 서로 다른 인덱스 사이의 경계선을 요청한다. FDelaunay2는 중복 정점을 삼각분할에 모두 삽입하지 않으며 기본 `bAutomaticallyFixEdgesToDuplicateVertices=false`다. 따라서 해당 인덱스의 경계선이 결과에 존재하지 않을 수 있다. 인덱스를 합치는 옵션을 별도 대조 실험에서 켜도 이 사례는 성공하지 않았다. 합쳐진 두 끝점 사이의 길이 0인 경계선 요청 자체가 남기 때문이다.

이는 옵션을 런타임 해결책으로 추가한 것이 아니라 별도 진단용 FDelaunay2 인스턴스로 비교한 결과다.

### B. 중복 정점 없이도 double 복원 후 미세한 자기 교차가 생김

시나리오 6, step 54:

```text
HasDuplicates=false
MissingEdges: (184,185)
```

저장된 double 값의 경계:

```text
184 = (-19.701397417179329, 23.070325900514590)
185 = (-19.513894934338488, 23.172633592219313)
186 = (-19.513894934338520, 23.172633592219300)
187 = (-19.513894934338477, 23.172633592219313)
```

변 `184→185`와 `186→187`이 실제로 교차한다. 후자는 약 `4.49e-14 cm` 길이다. Python `Fraction`으로 각 double 값을 정확한 유리수로 변환해 교차를 검증했다. 반올림된 외적이 우연히 0으로 계산된 것을 교차로 오인한 결과가 아니다.

단계별 검사에서는 `RemainingBefore`, Cutter, Union, SafeIntersection에 교차가 없고, 최종 Difference 출력에 1개가 생겼다.

추가 분리 실험:

| 출력 표현 | 중복 정점 | 실제 변 교차 |
|---|---:|---:|
| Clipper 정수 출력 | 0 | 0 |
| Unreal식 double 복원 출력 | 0 | 1 |

즉 이 사례의 자기 교차는 정수 결과를 double로 복원하는 단계에서 도입됐다. FDelaunay2는 이 교차점을 새로 삽입하고 두 경계선을 분할하는 경로가 아니므로, 모든 입력 경계선을 동시에 만족시키지 못한다. 설치본의 `ValidateEdgesResult`는 실제 결과에 모든 요청 경계선이 존재하는지 확인하고, 없으면 `EResult::MissingEdges`를 설정한다.

따라서 `HasDuplicates=false`는 정상 입력이라는 보장이 아니다. 중복 없이도 경계선 교차가 있을 수 있다.

## 검증 결과와 범위

- 지정 명령 `BuildProject_Editor.bat`: 성공.
- `BreachSystem.Diagnostics.GeometryCauses`: 1개 진단 테스트 성공. 성공의 의미는 MissingEdges와 double→float 퇴화의 재현 및 증거 저장을 확인했다는 것이다.
- 정수 출력 분리 실험: 세 저장 사례 모두 Unreal의 출력과 모든 double 좌표가 정확히 일치.
- 정수/유리수 기반 교차 검사: 중복 없는 MissingEdges 사례의 정수 출력 0개, double 출력 1개 교차 확인.
- 작은 삼각형 제거, 정점 스냅, 허용치 변경, 강제 성공, 삼각분할 대체를 적용하지 않았다.
- 프로젝트 소스 변경은 테스트 빌드 전용 observer와 진단 테스트다. 설치 엔진 소스·에셋·기존 절단 정책은 이 작업에서 변경하지 않았다.
- 첨부 플레이 당시의 전체 Cutter/Detached 이력은 아직 없다. 따라서 첨부의 특정 물리 Element 번호까지 재현 사례와 일대일 대응시키지는 않는다.

## 구현 출처

- `Plugins/BreachSystem/Source/BreachSystem/Private/Surface/BreachSimpleCollision.cpp`: 캐시 구멍 재투입, Union, SafeIntersection, Difference, 삼각형별 충돌 기둥 생성.
- `Plugins/BreachSystem/Source/BreachSystem/Private/Surface/BreachSurfaceGeometry.cpp`: FDelaunay2 호출과 면적 검사.
- `F:/UE_5.8.2/Engine/Plugins/Runtime/GeometryProcessing/Source/GeometryAlgorithms/Private/Curve/PolygonIntersectionUtils.cpp`: Boolean 호출마다 범위 계산과 Clipper 실행.
- 같은 디렉터리의 `ClipperUtils.h`, `ClipperUtils.inl`: `IntRange=2^53-1`, 정수화와 double 복원.
- `F:/UE_5.8.2/Engine/Source/Runtime/GeometryCore/Public/CompGeom/Delaunay2.h`: 중복 정점 옵션, 경계 검증 및 MissingEdges.
- `F:/UE_5.8.2/Engine/Source/Runtime/Engine/Private/PhysicsEngine/Experimental/ChaosCooking.cpp`: Convex 좌표 변환.
- `F:/UE_5.8.2/Engine/Source/Runtime/Experimental/Chaos/Public/Chaos/CollisionConvexMesh.h`: 평면 판정과 Convex 빌더 float 타입.
- `F:/UE_5.8.2/Engine/Source/Runtime/Experimental/Chaos/Private/Chaos/CollisionConvexMesh.cpp`: Inflate와 Hull 실패.
- [Clipper 공식 수치 안정성 설명](https://www.angusj.com/clipper2/Docs/Robustness.htm): 반올림에 따른 작은 형상과 교차 문제의 일반적 배경. 이 문서의 특정 발생 지점은 설치된 엔진 소스 및 실제 저장 좌표 실험을 근거로 했다.
