# Stylized / Toon / HybridToon 구성

기존 비교 맵 `/Game/MazelineTests/L_ForwardComparison`의 세 머티리얼을 UE 에셋 API로 이름 변경하고 그래프를 수정했다. 세 구의 위치·광원·카메라와 기존 색상 그래프는 유지한다.

| 이전 에셋 | 현재 에셋 | Define 값 | Include File Paths |
| --- | --- | --- | --- |
| M_ForwardControl | /Game/MazelineTests/M_Stylized | 1 | /Project/Private/Stylized.ush |
| M_ForwardOverride | /Game/MazelineTests/M_Toon | 2 | /Project/Private/Toon.ush |
| M_SecondOverride | /Game/MazelineTests/M_HybridToon | 3 | /Project/Private/HybridToon.ush |

Define 이름은 모두 `MAZELINE_SHADER_OVERRIDE`다. 파일은 프로젝트의 `Shaders/Private`에 있으며 함수 이름은 모두 `MazelineEvaluateShaderOverride`다.

## 그래프

- Stylized: 기존 색상 Constant → Tint 입력을 가진 Custom → Base Color. Custom Code는 `return Tint;`다.
- Toon: 기존 Custom의 색상 반환 코드를 보존하고 2번 / Toon.ush로 연결했다.
- HybridToon: 기존 Custom → Multiply 및 색상 노드를 보존하고 3번 / HybridToon.ush로 연결했다. 사용자가 만든 색상 곱셈을 유지한다.
- 세 머티리얼 모두 비어 있던 Metallic·Roughness 핀에 같은 이름의 Scalar Parameter를 연결했다. 기본값은 각각 0과 0.5다.
- Custom의 Description에 번호와 스타일을 표시했다. 맵의 구 이름도 Mazeline Stylized / Mazeline Toon / Mazeline HybridToon으로 정리했다.

## 예제 HLSL의 차이

- Stylized: 광원 방향에 따라 부드럽게 변화하는 diffuse와 어두운 구간의 푸른 색조. Roughness 입력을 유지한다.
- Toon: 0.25 / 0.65 경계를 사용하는 세 단계 diffuse. Roughness는 최소 0.75로 만든다.
- HybridToon: 부드러운 변화와 Toon 단계를 혼합한다. 파일 안의 `ToonBlend=0.55`를 수정할 수 있으며 Roughness도 혼합한다.

세 함수는 스타일 출발점인 예제다. 최종 화면색을 직접 출력하지 않으며 기존 Default Lit의 BRDF, 광원 감쇠·그림자·specular 계산이 계속 적용된다. HLSL 변경 후 Project Settings → Plugins → Mazeline Super Override Material → Apply / Update Patch를 실행하고 재시작한다.

## 기존 회귀 검증 자료

원본 Content/MazelineTests와 Shaders/Private, 함수 등록 JSON을 `Saved/StyleMigration/20261102-015858`에 보관했다. 기존 세 ush는 해당 백업의 `RetiredShaderFiles`로 이동했으며 활성 Shaders/Private에는 새 세 파일만 남겼다.

별도 InputValidation의 M_Override와 M_RoughnessOverride는 이전 함수 3/2를 사용하므로, 새 스타일 등록과 충돌하지 않게 MAZELINE_SHADER_OVERRIDE와 include를 해제했다. 에셋과 재질 입력은 남아 있지만 **현재는 과거 오버라이드 회귀 테스트를 실행하는 구성은 아니다.** 기존 GPU 검증 보고서는 당시 소스에 대한 역사적 결과다. 회귀 캡처 스크립트는 이전 등록이 비활성인 경우 명시적으로 중단한다.

복구하려면 에디터를 종료하고 백업된 에셋·셰이더와 그 등록 구성을 복원한 다음 C++ 관리자로 다시 Apply한다. 엔진 원본을 복원하는 Restore 버튼만으로 프로젝트 머티리얼의 이름과 그래프가 돌아가지는 않는다. 엔진 최초 원본 백업은 별도로 Engine/Saved/MazelineShaderOverride에 계속 보존한다.

## 확인 자료

- `Saved/StyleMigration/migration-result.json`: 이름 변경, 함수 번호와 include, 비교 맵 연결.
- `Saved/StyleMigration/verification.json`: 저장 후 다시 로드해 Custom 연결, 세 입력 핀, 기존 Multiply 보존, 세 구의 재질 할당을 확인한 결과.
- `Saved/Mazeline/deferred_registry.json`: 현재 C++ 관리자가 수집한 1=Stylized / 2=Toon / 3=HybridToon.
- `Saved/Logs/StylePatchApply.log`: Python을 비활성화한 C++ 커맨드릿의 엔진 패치 성공 기록.

머티리얼 에셋 변경에는 프로젝트의 UE Python 에디터 API를 사용했다. 엔진 패치는 기존대로 플러그인 내부 C++이 수행했다.

## 새 세 함수의 실제 렌더링 확인

DX11/SM5의 Deferred와 Forward에서 비교 맵을 각각 실행하고 GPU 캡처를 확인했다. 두 로그에 셰이더 컴파일 오류가 없고, 세 구의 부드러운 변화 / Toon 단계 / 혼합 결과를 확인했다. 이번 캡처는 UnrealEditor의 `-game` 실행이며 새 게임 패키징 결과는 아니다.

- `Saved/StyleMigration/Deferred.png`, `Saved/Logs/StyleRenderDeferred.log` (`r.ForwardShading=0`).
- `Saved/StyleMigration/Forward.png`, `Saved/Logs/StyleRenderForward.log` (`r.ForwardShading=1`).
- `Saved/Logs/StylePatchStatus.log`: `state=applied matches=yes`.

위 캡처 당시에는 검증 실행에 DX11/SM5와 Forward 임시 옵션을 지정했다.

## 현재 Windows 실행 설정: DX12 / SM6

사용자 요청에 따라 Windows 기본 RHI는 `DefaultGraphicsRHI_DX12`, D3D12 타깃은 `PCD3D_SM6`로 사용한다. D3D11의 `PCD3D_SM5` 타깃은 해제했다. 에디터는 `-d3d11 -sm5` 없이 프로젝트 기본 설정으로 재시작했다. Visual Studio 2022와 기존 Ray Tracing 등 다른 렌더링 설정은 유지한다.

실행 로그 `Saved/Logs/StyleEditorDX12.log`에서 `rhiname="D3D12"`, `shaderplatform="PCD3D_SM6"`를 확인했다. 앞의 DX11/SM5 캡처와 패키징 기록은 해당 환경에서 수행한 과거 검증이며 DX12 패키징 검증을 뜻하지 않는다.

SM6 셰이더 컴파일과 에디터 초기화가 끝난 뒤 비교 맵의 세 스타일이 표시되는 것을 확인했다. 해당 시작 로그에 셰이더 컴파일 오류는 없다. 이번 확인은 기본 Deferred 에디터 실행이며 DX12 Forward나 패키징의 추가 검증은 포함하지 않는다.
