
정점의 수가 폴리곤이 복잡해질수록 많아지다 보니, 병렬 연산을 고려하게 된다.

적용 고려해본 함수
- 삼각분할
- 삼각분할 이후 GetFilledTriangles함수 (근데 이거는 멀티스레드 안될거같음, 순서대로 진행되어야해서)
- TouchesOriginalBorder함수


### 삼각분할

현재 삼각분할은
`Delaunay.Triangulate(RemainingRegion, &Triangles, &Vertices, false)`
이렇게 수행중이고, RemainingRegion은 `TArray<UE::Geometry::FGeneralPolygon2d>` 이다.
따라서 Region이 많으면 삼각분할에서 이득을 많이 볼 수 있을 것이라고 생각되지만..
Region이 그렇게 많이 생길까? 라고 하기에는 동의하지 않는다.
**패스**


### GetFilledTriangles
이것도 Region별로 Fill처리가 가능한데 위에서 설명한 것처럼 Region이 적을 것으로 예상되어 고려하지 않는다.
이 함수는 함수는 순서 의존적인 계산을 수행하기 때문에 병렬적으로 처리하고싶으면 기존 winding 판별 알고리즘을 삼각형마다 독립적으로 처리하도록 
변경해야한다.
**패스**

### TouchesOriginalBorder
정점에 따라서 반복문을 많이 돌게 되는 함수지만
내부 구현이 너무나도 간단하기 때문에 고려하지 않는다.


