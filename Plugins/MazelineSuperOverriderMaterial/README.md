# Mazeline Super Overrider Material

Epic 설치형 **Unreal Engine 5.8.4**, **Visual Studio 2022**용 플러그인이다. 연결된 Custom Expression에 `MAZELINE_SHADER_OVERRIDE`를 정의하고, 별도 HLSL에서 광원마다 DiffuseColor와 Roughness를 계산한다. 머티리얼별 GGX 파라미터와 Directional Ramp LUT를 사용하는 Toon도 Default Lit을 유지한다. 전용 출력 노드가 Sharpness·Threshold·RampRow를 전달한다. 엔진 C++ 바이너리를 다시 빌드하지 않으며, 엔진 셰이더 다섯 파일을 직접 패치한다.

현재 프로젝트는 **Windows DX12/SM6**를 사용한다. Toon의 머티리얼별 Sharpness·Threshold는 Forward와 Deferred에서 런타임 인스턴스 값을 바꾸는 8개 렌더링 사례로 검증했다. 이번 파라미터 확장의 패키징 검증은 수행하지 않았다. 초기 0.2 버전은 DX11/SM5의 Opaque Default Lit에서 두 경로의 에디터 렌더링과 Win64 Development 쿠킹·패키징·실행, 입력 보존 및 일반 머티리얼 회귀를 검증했으며 그 결과는 과거 이력이다. Substrate, Nanite, 모바일, 경로 추적, 반투명 및 특수 렌더링 경로는 지원 범위에 포함하지 않는다.

## 머티리얼 작성

1. 프로젝트의 `Shaders/Private` 아래에 아래 시그니처의 `.ush` 파일을 만든다. 파일은 독립적으로 컴파일할 수 있어야 한다.
2. Opaque / Default Lit 머티리얼의 Custom Expression에 **Additional Defines** 항목을 추가한다. Name은 `MAZELINE_SHADER_OVERRIDE`, Value는 `1`, `2`, `3` 중 하나다. 같은 HLSL을 사용하는 머티리얼끼리는 같은 번호를 공유한다. 서로 다른 함수에는 서로 다른 번호를 사용한다.
3. 해당 Custom Expression의 **Include File Paths**에 `/Project/Private/MyOverride.ush`를 추가한다. Custom의 일반 Code는 Base Color 등의 머티리얼 값을 반환하도록 작성하고 실제 Base Color, Roughness, Metallic 핀으로 연결되는 그래프에서 사용한다. 연결되지 않은 Custom은 등록되지 않는다.
4. Roughness와 Metallic 핀에도 원하는 그래프 값을 연결한다. 이 값과 Base Color가 아래 함수에 전달된다. 광원 방향은 일반 머티리얼 그래프 실행 시점이 아니라 광원 셰이딩 시점에 전달된다.

```hlsl
void MazelineEvaluateShaderOverride(
    float3 BaseColor, float Metallic, float3 N, float3 V, float3 L,
    inout float3 DiffuseColor, inout float Roughness)
{
    float band = dot(normalize(N), normalize(L)) > 0.5 ? 1.0 : 0.25;
    DiffuseColor = (BaseColor - BaseColor * Metallic) * band;
    Roughness = saturate(Roughness);
}
```

N, V, L은 엔진 광원 계산에서 얻은 월드 공간 방향이며 L은 `AreaLight.DiffuseL`이다. DiffuseColor의 초기값은 엔진이 계산한 diffuse albedo다. 함수가 끝난 뒤 기존 DefaultLit BRDF, 광원 세기·감쇠·그림자가 적용된다. 따라서 DiffuseColor는 최종 화면 색상이 아니다. Metallic은 기존 specular 계산에도 사용되며 이 함수의 출력 인자가 아니다. Roughness 출력은 0~1로 제한된다.

Deferred 광원 셰이더는 머티리얼 그래프를 다시 실행하지 않는다. 등록된 HLSL 함수가 위 입력을 사용해 실행된다. Curve Atlas는 아래의 전용 RampLUT 경로로 바인딩한다. 그 외 임의 텍스처·샘플러·머티리얼 전용 유니폼 바인딩과 중첩 `#include`, 머티리얼 함수 내부의 Custom 등록은 지원하지 않는다. 전역 helper 이름과 include guard는 다른 등록 파일과 충돌하지 않게 작성한다. HLSL 문법의 최종 판정은 실제 엔진 셰이더 컴파일 결과로 확인한다.

## Directional Ramp LUT

현재 예제는 Stylized(1) / Toon(2)이며 HybridToon은 제거했다. 함수 번호 3은 새 사용자 함수 등록에 사용할 수 있다. Toon은 Curve Editor의 RGB 커브로 Directional Light diffuse를 제어한다. `Custom.RampLUT`에는 Curve Atlas의 Texture Object, `Custom.RampRow`에는 행 번호 Scalar Parameter를 연결한다. `RampIndex = RampRow;` 추가 출력을 Mazeline Ramp Output에 연결한다. `MAZELINE_DIRECTIONAL_RAMP=1`이 광원 셰이더의 LUT 처리를 활성화한다.

함수 하나당 공유 Atlas 하나를 등록하고 행은 인스턴스별로 고른다. Custom에서 미리 샘플링하지 않으며 광원 방향으로 조명 패스에서 샘플링한다. Point·Spot·Rect diffuse와 GGX specular에는 램프를 적용하지 않는다. 커브 키·RampRow 변경은 재패치가 필요 없고, Atlas 에셋 교체는 Apply / Update와 재시작이 필요하다. 자세한 그래프·데이터 흐름·제한은 [Directional Ramp 문서](../../Docs/Directional-Ramp.md)를 참조한다.

## GGX 분포의 선명도 조절

`Shaders/Private/Toon.ush`는 별도의 원형 하이라이트를 합성하지 않고 GGX의 normal distribution 항 D를 재조정한다. `#define MAZELINE_HAS_GGX_DISTRIBUTION_OVERRIDE 1`과 아래 콜백을 함께 선언한다.

```hlsl
// Material parameters: also declare MAZELINE_GGX_MATERIAL_PARAMETERS 1.
float MazelineEvaluateGGXDistribution(float Distribution, float PeakDistribution,
                                     float Sharpness, float Threshold)
```

`Generate()`는 설치된 엔진의 isotropic `SpecularGGX()`에서 변형 함수 `MazelineSpecularGGX()`를 생성한다. `D_GGX(a2, NoH)`를 피크 `D_GGX(a2, 1)`로 정규화해 대조도를 바꾸고 다시 원래 크기로 복원한다. 엔진의 `EnergyNormalization`, `Vis_SmithJointApprox`, `F_Schlick`와 후속 에너지 보정·광원·그림자 처리는 유지한다. D 계산 삽입 지점이 예상과 다르면 패치를 거부한다.

| 조절값 | 위치와 의미 |
| --- | --- |
| Roughness | 기존 머티리얼 핀·파라미터. GGX 분포의 폭과 피크를 결정 |
| Sharpness | M_Toon의 Scalar Parameter, 기본 0.5. 0은 D 그대로 반환, 중간값은 대조도 증가, 1은 선명한 단계 경계 |
| Threshold | M_Toon의 Scalar Parameter, 기본 0.25. GGX 피크 대비 상대 경계값. 낮추면 밝은 영역 확장 |

두 하이라이트의 혼합이나 `ToonBlend`는 없다. 중간 구간은 정규화된 D의 log-odds에 대조도를 적용하는 단일 분포다. 종전의 고정 `HighlightStrength=8`과 Roughness 최소 0.75 보정도 사용하지 않는다. `/Game/MazelineTests/MI_Toon`의 **Toon Highlight** 그룹에서 Roughness·Sharpness·Threshold를 조절한다. Scalar Parameter 값 변경에는 엔진 재패치·재시작이 필요 없다. HLSL 함수 자체를 바꿀 때만 Apply / Update Patch와 재시작이 필요하다.

`MAZELINE_GGX_MATERIAL_PARAMETERS=1`을 사용하는 머티리얼도 **Opaque / Default Lit**으로 작성한다. 그래프에 **Mazeline GGX Output** 노드 하나를 추가하고 Sharpness·Threshold 입력에 Scalar Parameter 또는 계산식을 연결한다. 이 노드는 별도 출력 노드이므로 메인 머티리얼 노드에 다시 연결하지 않는다. 기존 Custom Expression의 `MAZELINE_SHADER_OVERRIDE`와 Include 설정은 유지한다. Base Color·Metallic·Roughness·Specular·AO는 원래 핀을 그대로 사용한다.

패치는 이 옵션을 사용하는 Default Lit의 Base Pass에서만 CustomData 출력과 GBufferD의 MRT 쓰기를 활성화한다. 기존 GBufferD를 사용하므로 추가 렌더 타깃을 신설하지 않는다. 컨트롤은 각각 8비트 UNORM으로 저장되며, Forward의 float 전달과 양자화 차이가 있을 수 있다. 작성용 Shading Model은 Default Lit이고 Deferred 내부의 함수 식별용 ID 14는 기존대로 유지한다. 엔진 패치를 복원하면 Shading Model 변경 없이 일반 Default Lit로 돌아간다. 현재 노드 구현은 기존 HLSL Material Translator 경로를 지원하며 새 MIR 생성기 지원은 포함하지 않는다.

기존의 두 인자 GGX 콜백도 지원한다. 머티리얼 파라미터 플래그 없이 이 콜백을 사용하는 스타일은 Default Lit으로 작성한다. 한 등록 함수에서 파라미터 방식과 고정 설정 방식을 혼용하지 않는다. 일반 Clear Coat 머티리얼은 오버라이드 정의가 없으면 원래 엔진 동작을 유지한다.

Forward는 머티리얼의 분포 콜백을, Deferred는 번호별 `MazelineDeferredGGXFunction2()`를 호출한다. 기존 `MAZELINE_SHADER_OVERRIDE=2`와 Include 경로는 유지한다. 미참여 재질과 콜백 없는 스타일, Rect Light의 LTC 및 이방성 specular는 원래 함수를 사용한다. 환경 반사·간접광은 이 경로에 포함되지 않는다. 원래 에너지 보정을 유지하지만 변형된 D 자체가 물리적인 에너지 보존을 보장하지는 않는다.

이전 전체 교체 API인 `MAZELINE_HAS_SPECULAR_OVERRIDE` / `MazelineEvaluateSpecularOverride()`는 호환성을 위해 유지한다. 같은 등록 함수에서 GGX 분포 콜백과 동시에 사용하면 패치를 거부한다. 현재 Toon 예제는 GGX 분포 콜백만 사용한다.

## 적용과 갱신 — 0.3.0 C++ 관리자

1. 머티리얼과 HLSL 변경을 저장한다. 같은 엔진을 사용하는 다른 에디터는 종료한다.
2. **Edit → Project Settings → Plugins → Mazeline Super Override Material**을 연다. **Tools → Mazeline: Shader Patch Settings**도 같은 화면을 연다.
3. **Apply / Update Patch**를 누른다. 플러그인 C++이 연결된 Custom Expression을 수집하고, 레지스트리 생성과 Forward/Deferred 패치를 한 번에 처리한다.
4. 상태가 `applied`이며 SHA-256 일치로 표시되는지 확인한다. 변경됐다면 **Restart Editor**를 누르고 셰이더 컴파일 완료를 기다린다. 변경이 없는 중복 적용은 파일을 다시 쓰지 않는다.

| 화면 항목 | 기능 |
| --- | --- |
| Patch state | 미관리 `unmanaged`, 적용 `applied`, 원본 복원 `restored`, 중단 `interrupted`, 충돌 `conflict` 등과 파일 해시 일치 여부 |
| Engine / Project owner / Backups | 대상 엔진, 활성 패치 소유 프로젝트, 백업 경로 |
| Apply / Update Patch | 함수 수집부터 패치 적용·갱신까지 실행 |
| Restore Original | 최초 백업의 세 엔진 셰이더를 복원하고 백업은 보존 |
| Recover Interrupted Operation | 중단된 트랜잭션을 작업 직전 상태로 복구 |
| Refresh Status | 상태를 즉시 갱신; 화면은 3초마다 자동 갱신 |
| Open Backups / Restart Editor | 백업 폴더 열기 / 변경 후 에디터 재시작 |

`Restart required`는 디스크의 셰이더 변경을 현재 에디터에 반영하려면 재시작이 필요하다는 뜻이다. 실행 중인 PIE에서는 패치 작업을 막는다. 현재 에디터의 셰이더 컴파일과 렌더 명령을 완료한 뒤 파일을 변경하며, 같은 엔진의 다른 에디터·셰이더 작업자가 있으면 거부한다. 파일 쓰기 권한이 없으면 오류를 표시한다.

플러그인은 PythonScriptPlugin에 의존하지 않는다. 머티리얼 수집, 패치 코드 생성, SHA-256 검증, 백업, 복원 및 중단 복구를 모두 C++으로 실행한다. 이 테스트 프로젝트의 Python 플러그인은 기존 테스트 에셋 생성 스크립트용으로 남아 있다.

## 복구와 명령줄

일반 복원은 설정 화면의 **Restore Original**, 중단 복구는 **Recover Interrupted Operation**으로 실행한 뒤 재시작한다. 파일 수정으로 에디터의 정상 기동이 어려우면 에디터를 종료하고 C++ 커맨드릿을 실행한다.

```powershell
& 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' `
  'D:/UnrealProject/Projects/MazeLineMaterialPlugin/MazeLineMaterialTest.uproject' `
  -run=MazelineShaderPatch -Action=recover -nullrhi -unattended -DisablePython
```

`-Action=`에는 `status`, `prepare`, `apply`, `restore`, `recover`를 지정한다. `prepare`는 `Saved/Mazeline/deferred_registry.json`만 생성하고, `apply`는 이를 자동으로 다시 수집한다. 콘솔 명령 `Mazeline.ShaderOverride.Status`, `.Prepare`, `.Apply`, `.Restore`, `.Recover`, `.Settings`도 같은 C++ 구현을 사용한다. 명령줄의 엔진·프로젝트 경로는 실제 위치에 맞춘다.

원본·적용본·트랜잭션 기록은 **엔진의 `Engine/Saved/MazelineShaderOverride`**에 저장된다. 기존 format 1의 3·4파일 백업은 수정하지 않는다. `legacy-extension.json`은 DeferredShadingCommon 원본을, `ramp-extension.json`은 DeferredLightingCommon 원본을 포함한 추가 스냅샷을 가리킨다. 현재 적용·복원·중단 복구는 다섯 파일을 관리한다. 이 폴더를 지우면 자동 복구할 수 없다. 외부 변경이나 손상된 백업이 발견되면 덮어쓰지 않고 중단한다. 확장 후에는 현재 C++ 관리자를 사용하며 이전 Python 관리자로 복원하지 않는다. 한 엔진에는 한 프로젝트의 레지스트리만 활성화할 수 있다.

## 구현 위치와 제한

- `Source/MazelineSuperOverriderMaterial/Private/MazelineShaderPatchManager.cpp`: 함수·Atlas 수집, 다섯 셰이더의 삽입 코드와 패치·복원·복구 로직. `Generate()`에 실제 패치 내용을 담았다. 대상은 ShadingCommon, ShadingModelsMaterial, ShadingModels, DeferredShadingCommon, DeferredLightingCommon의 `.ush` 파일이다.
- `MazelineMaterialRuntime/Private/MaterialExpressionMazelineGGXOutput.cpp`: Default Lit용 Sharpness·Threshold 출력 노드. 게임 실행에서도 로드된다.
- `MazelineMaterialRuntime/Private/MaterialExpressionMazelineRampOutput.cpp`, `MazelineRampSettings.cpp`: RampRow 출력, 런타임 Atlas 보존, Scene View Extension의 View 텍스처 바인딩. Substrate 비활성 상태에서 비어 있는 Toon 텍스처 슬롯을 사용하며 엔진 5.8.4의 내부 View 헤더에 의존한다. Runtime 모듈을 PostConfigInit에 로드해 플러그인 Compute Shader를 등록한다.
- `MazelineMaterialLibrary.cpp`: 기존 Clear Coat 전달 방식에서 전용 출력 노드로 옮기는 에디터 API.
- `SMazelineMaterialSettings.cpp`: 프로젝트 설정 화면과 버튼.
- `MazelineSuperOverriderMaterialModule.cpp`: 설정 등록과 메뉴·콘솔 명령.
- `MazelineShaderPatchCommandlet.cpp`: Python 없이 실행하는 명령줄 진입점.

Deferred에서는 shading-model ID 14와 기존 GBuffer의 per-object 2비트를 사용한다. 동시에 등록 가능한 함수는 **3개**이고, 선택한 픽셀은 기존 contact-shadow / dynamic-indirect-shadow 플래그를 보존하지 못한다. Base Color·Metallic·Roughness 채널은 유지한다. 다른 셰이더 플러그인과 ID/채널이 충돌할 수 있으며, 지원되지 않는 엔진 버전이나 맞지 않는 삽입 지점은 패치를 거부한다.

이전 Python 레지스트리 생성기와 `Saved/Mazeline/ManagePatch.ps1`은 0.2 개발 이력이다. 0.3 플러그인은 이들을 호출하지 않으며 배포 필터에서도 이전 Python 도구를 제외한다. 프로젝트의 `Tools/*.py`에는 과거 스크립트와 최근 에셋 구성·렌더링 검증 도구가 함께 있다. 이들은 개발 보조 도구이고, 현재 패치 조작은 위 C++ 설정 화면이나 커맨드릿이 수행한다. 이전 `Saved/MazelineForwardPatch`, `Saved/MazelineDeferredTwoFunctions`도 이력 보존용이며 현재 엔진 복원에 사용하지 않는다.
