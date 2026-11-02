# Unreal C++ Rules

## Implementation Contract
- Before editing, identify the owning type/module, callers, lifetime, public/native/Blueprint API, initialization and teardown points, required dependencies, and verification method.
- Add replication, persistence, async loading, Gameplay Tags, Data Assets, or Blueprint exposure only when the requested behavior requires them.
- New Unreal classes normally include the required `.h` and `.cpp` together. Existing classes change only the necessary files, but signature, reflection, member, override, or helper changes require checking declaration and implementation separately.

## Pointer And Dependency Checks
- Every function that dereferences a pointer or UObject reference must make the null contract explicit inside that function, even when a caller already checked it.
- For a required dependency, use a project-appropriate assertion or diagnostic such as `checkf` or `ensureMsgf`, then stop the affected local operation if continuing would be invalid.
- For an optional dependency, use an explicit null branch and the declared fallback behavior.
- Do not let a null guard silently convert a required setup error into apparent success. Include the affected object, operation, and missing dependency in diagnostics.

## Missing Tags And Configuration
- Every missing tag, setting, class, or asset reference must have an explicit fallback outcome.
- Prefer a domain-safe default when one is defined. Otherwise reject only the affected operation while preserving authoritative state; this rejection is the fallback.
- Emit actionable diagnostics whenever a fallback is used. The caller or test must be able to distinguish the primary path from fallback execution.
- A fallback must not invent durable identity, progression, replicated authority, or save data.

## Ambiguous Ownership
- Inspect callers, lifetime, dependencies, serialization, and Blueprint use before deciding ownership.
- If evidence still does not distinguish a better owner, place the initial implementation in the project's runtime module and record a TODO containing the unresolved decision, current reason, and concrete condition that will trigger relocation.
- Do not create a shared/common module solely to avoid making the temporary ownership decision.
- Resolve ownership before proceeding when the choice would establish a public ABI, Blueprint class path, save schema, cross-module dependency cycle, or shipped plugin contract.

## Unreal Type And Module Boundaries
- Use reflection macros only for types and members that require reflection, serialization, editor exposure, Blueprint access, or networking.
- Choose `TObjectPtr`, raw, weak, soft, or class references from ownership, GC, load, and lifetime requirements rather than applying one pointer form universally.
- Forward-declare pointer/reference types in headers when UHT and inline code allow it. Include concrete headers in `.cpp` files that use the type API.
- Add the smallest correct public/private `Build.cs` dependency and verify the owning module from official docs or engine source when uncertain.
- Prefer event-driven work over `Tick`; enable ticking only when continuous updates are part of the actual behavior.

## Blueprint Boundary
- Keep core rules and durable state in C++.
- Expose the smallest stable Blueprint surface needed for authored behavior and presentation.
- Treat `UPROPERTY`, `UFUNCTION`, reflected enum/struct, class path, category, and metadata changes as asset-contract changes and inspect affected assets.
- Use `BlueprintNativeEvent` only when authored override behavior is intended, not as a default extension mechanism.

## Test-Only Code
- 테스트 검증만을 위한 필드, 함수, 보조 코드, 통계 수집에는 `테스트 전용` 주석과 검증 목적을 명시한다. 런타임 동작에 필요한 코드와 구분한다.
- 자동화 테스트와 그 전용 데이터·수집·호출 코드는 `#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS`로 감싸 Editor 자동화 테스트 빌드에서만 컴파일한다. 테스트의 `EditorContext` 플래그만으로 컴파일 범위를 제한했다고 간주하지 않는다.
- 일반 Editor 코드에는 `WITH_EDITOR`, 리플렉션되는 Editor 전용 데이터에는 `WITH_EDITORONLY_DATA`를 용도에 맞게 사용한다. 테스트 검증을 위해 게임·클라이언트·서버 빌드에 저장 공간이나 실행 비용을 추가하지 않는다.

## Verification
- Compile with the established project-owned command; never invent a direct Engine/UBT command.
- Run the smallest relevant automation test after the build.
- For reflection or Blueprint API changes, verify UHT/generated code and affected Blueprint compilation.
- Review the final diff for unused includes, orphaned declarations, duplicated fallback paths, unexpected module dependencies, and code unrelated to the request.
