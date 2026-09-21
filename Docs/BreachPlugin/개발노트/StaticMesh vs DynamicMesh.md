
아무래도 StaticMeshComponent의 메시를 가져다가 DynamicMeshComponent에 적용하는 방식은
사용법도 어렵고 실수가 나올수도 있다.
AI는 DynamicMesh의 실시간 부하가 심할 수 있으니
StaticMeshComponent, DynamicMeshComponent 둘 다 깔아두고 충돌 발생 이후에만 Dynamic으로 교체하는걸 추천했는데
난 안믿기니까 확인해보자
***

통상

**Static**
![[Pasted image 20260921221049.png|546]]

**Dynamic**
![[Pasted image 20260921221012.png|556]]

AI말은 듣고살아야한다

#### 이유
![[Pasted image 20260921223949.png]]


**여러 컴포넌트가 같은 StaticMesh를 사용하면**

```
SMC 1 ─┐
SMC 2 ─┼─> 동일한 FStaticMeshRenderData
SMC 3 ─┘       ├─ VertexBuffer 하나
                └─ IndexBuffer 하나
```
DrawIndexedInstanced()로 한번에 처리가 가능하다.

**DynamicMeshComponent** 같은경우

```
DMC 1 → FDynamicMesh3_1 → VertexBuffer_1 / IndexBuffer_1
DMC 2 → FDynamicMesh3_2 → VertexBuffer_2 / IndexBuffer_2
DMC 3 → FDynamicMesh3_3 → VertexBuffer_3 / IndexBuffer_3
```
각 컴포넌트가 독립적으로 메시 데이터가 수정될 수 있기 때문에 각 인스턴싱을 보장 할 수가 없다.
따라서 DrawCall이 계속 늘어나게 되는 것이다.

```
Static

Mesh
 ↓
FMeshBatch 생성
 ↓
FMeshDrawCommand 생성
 ↓
캐시
 ↓
다음 프레임
캐시된 DrawCommand 사용
```

```
Dynamic

Mesh
 ↓
매 프레임 GetDynamicMeshElements()
 ↓
FMeshBatch 생성
 ↓
FMeshDrawCommand 준비
 ↓
Draw
 ↓
다음 프레임 또 반복
```

Epic 문서도 Dynamic Path는 매 프레임 `FMeshBatch`를 재생성하고, Static/Cached Path는 이전에 만들어 둔 것을 재사용한다고 설명한다.
https://dev.epicgames.com/documentation/unreal-engine/mesh-drawing-pipeline-in-unreal-engine

결국 내가 SceneProxy만들고 렌더하는 방식을 StaticMesh방식을 따라서 만들어야한다.
- 공유 `FRenderResource`에 Vertex/Index Buffer 보관
- 각 컴포넌트의 커스텀 `FPrimitiveSceneProxy`가 그 버퍼 참조
- 동일 버퍼와 Material을 하나의 Instance Draw로 제출
- 수정 시 해당 컴포넌트만 버퍼를 복사하는 Copy-on-Write
- 렌더 스레드 수명과 버퍼 해제 동기화
- Shadow, Ray Tracing Geometry, Material Section 처리
- Bounds와 Collision은 컴포넌트별 관리

문제는 이걸 만들면 사실상 **StaticMesh 렌더링 시스템을 다시 만드는 것**과 비슷해져. 게다가 StaticMesh가 이미 제공하는 LOD, Nanite, Cached Draw Command 같은 최적화까지 자동으로 얻지는 못한다.

**그러면?**
=> StaticMeshComponent로 렌더링하다가 Breachable로 변경하는 방식을 사용하면서 구현 방식을 사용자가 신경 쓰지 않게 만들어야 한다.

![[Pasted image 20260922010508.png|812]]
이런 흐름으로, BreachMeshComponent에서 자체적으로 StaticMeshComponent를 추가해서 캐시하는 방식으로 사용한다.




