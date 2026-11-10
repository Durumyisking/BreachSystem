
### 😡 SimpleCollision에서 Cutter와 Detach 병합후 각 정점의 미세 오차때문에 아래의 문제가 발생한다. 

![[Pasted image 20261109171206.png]]
초록색 표면을 따라 Cut을했고, Detach가 발생한 프레임이다.
이때 Cutter의 정점과, Detach 정점이 미세하게 일치하지 않아 빨간색의 정점이 남아 버린다.

### 😊 새로 생긴 병합 구멍에 ClearHole함수 호출해서 Cutter의 내부의 Hole들을 제거한다.

![[Pasted image 20261109172253.png]]

