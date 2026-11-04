# Mazeline Super Overrider Material 테스트 프로젝트

Unreal Engine의 **Opaque / Default Lit** 머티리얼에 광원별 HLSL 함수를 등록하는 플러그인과 테스트 프로젝트다. Forward와 Deferred에서 DiffuseColor·Roughness를 조절하고, Toon 예제는 **Curve Atlas 기반 Directional Ramp LUT**와 **GGX 하이라이트 파라미터**를 제공한다. 엔진 C++ 바이너리 재빌드 없이 엔진 셰이더 다섯 파일을 패치한다.

현재 플러그인 버전은 **0.3.0**이다. 패치 적용·갱신·복원·중단 복구는 모두 플러그인 내부 C++ 관리자가 수행한다. 자세한 머티리얼 작성법과 복구 명령은 [플러그인 README](Plugins/MazelineSuperOverriderMaterial/README.md)에 있다.

## 실행 환경

| 항목 | 현재 구성 |
| --- | --- |
| 엔진 | Epic 설치형 Unreal Engine **5.8.4** (`EngineAssociation: 5.8`) |
| 플랫폼 / RHI | Windows / **DX12·SM6** |
| 빌드 도구 | **Visual Studio 2022 / MSVC 14.44.35228**, Windows SDK 22621 |
| 기본 렌더러 | **Deferred** (`r.ForwardShading=False`), Forward 경로도 지원 |
| 머티리얼 | Opaque / Default Lit, Substrate 비활성 |
| 플러그인 | `Plugins/MazelineSuperOverriderMaterial`, 프로젝트에서 활성화 |

DX11 타깃은 해제되어 있으므로 `-d3d11 -sm5` 실행 옵션을 사용하지 않는다. 현재 지원·검증 범위는 Windows DX12/SM6이며, 다른 플랫폼이나 RHI의 설정 항목이 남아 있어도 지원을 뜻하지 않는다.

## 시작하기

Git LFS를 설치한 뒤 다음 명령으로 프로젝트를 받는다. `.uasset`과 `.umap`은 Git LFS로 관리한다.

```powershell
git lfs install
git clone https://github.com/mazelines/MazelineSuperOverrideMaterialSystem.git
cd MazelineSuperOverrideMaterialSystem
git lfs pull
```

1. [.vsconfig](.vsconfig)의 Visual Studio 구성 요소를 설치한다. [BuildConfiguration.xml](Saved/UnrealBuildTool/BuildConfiguration.xml)과 C++ 타깃은 위 컴파일러 버전을 사용한다.
2. `MazeLineMaterialTest.uproject`에서 Visual Studio 프로젝트 파일을 생성하고 **Development Editor / Win64**로 빌드한 뒤 에디터를 연다.
3. 같은 엔진을 사용하는 다른 에디터를 종료하고 **Edit → Project Settings → Plugins → Mazeline Super Override Material**을 연다. **Tools → Mazeline: Shader Patch Settings**에서도 열 수 있다.
4. **Apply / Update Patch**를 누른다. 상태가 `applied`이고 파일 해시가 일치하는지 확인한다.
5. 변경이 적용됐다면 **Restart Editor**를 누르고 셰이더 컴파일 완료를 기다린다.

패치 조작에는 Python 플러그인이나 PowerShell 런처가 필요 없다. 테스트 프로젝트의 PythonScriptPlugin은 `Tools/*.py`의 에셋 구성·검증 작업을 위해 에디터에서만 활성화되어 있다.

## 현재 예제와 Toon 조절

| 예제 | 함수 번호 | 머티리얼 / HLSL | 동작 |
| --- | --- | --- | --- |
| Stylized | 1 | `M_Stylized` / [Stylized.ush](Shaders/Private/Stylized.ush) | 부드러운 diffuse 변화와 어두운 구간의 푸른 색조 |
| Toon | 2 | `M_Toon`, `MI_Toon` / [Toon.ush](Shaders/Private/Toon.ush) | 방향광 diffuse 커브와 GGX 분포의 선명도 조절 |

머티리얼 경로는 `/Game/MazelineTests/` 아래다. **HybridToon은 Toon으로 통합되어 제거됐고, 함수 번호 3은 사용자 함수용으로 비어 있다.** 함수 등록은 연결된 Custom Expression의 Additional Define `MAZELINE_SHADER_OVERRIDE=1~3`과 Include File Paths를 사용한다.

시작 맵은 `/Game/MazelineTests/L_ForwardComparison`이다. 맵 이름과 별개로 기본 렌더러는 Deferred다. `/Game/MazelineTests/Example`에는 `MI_Toon`을 할당한 Toon 예제가 있고, `/Game/MazelineTests/GGXValidation/L_GGXValidation`은 고정 직접광 검증용 맵이다. 과거 입력 보존 테스트 맵 `InputValidation/L_InputValidation`의 오버라이드 등록은 현재 비활성 상태다.

`MI_Toon`에서 다음 파라미터를 조절한다.

| 그룹 / 파라미터 | 기본값 | 의미 |
| --- | --- | --- |
| Toon Highlight / Roughness | 0.5 | GGX 분포의 폭과 피크 |
| Toon Highlight / Sharpness | 0.5 | 0은 원래 GGX, 1은 선명한 단계 경계 |
| Toon Highlight / Threshold | 0.25 | GGX 피크 대비 경계값. 낮출수록 밝은 영역 확장 |
| Toon Diffuse / RampRow | 2 | 0=Linear, 1=Soft, 2=Steps. 함수 번호와 별개 |

`/Game/MazelineTests/Ramps/CA_ToonDiffuse`에 연결된 `CR_ToonLinear`, `CR_ToonSoft`, `CR_ToonSteps`의 RGB 커브를 Curve Editor에서 편집한다. Ramp는 **Directional Light의 diffuse에만** 적용되고 Point·Spot·Rect diffuse와 specular에는 적용되지 않는다. Sharpness·Threshold는 **Mazeline GGX Output**, RampRow는 Custom의 `RampIndex` 출력과 **Mazeline Ramp Output**으로 전달한다. Base Color·Metallic·Roughness·Specular·AO는 기존 입력을 유지한다.

**파라미터 값과 커브 키 변경은 재패치 없이 반영된다.** HLSL 함수·등록 구성·Atlas 에셋을 바꾸면 저장 후 **Apply / Update Patch → Restart Editor**를 실행한다. 그래프 구성과 제한은 [스타일 구성](Docs/Style-Material-Setup.md), [Directional Ramp](Docs/Directional-Ramp.md)를 참조한다.

## 엔진 패치와 복원

패치 대상은 엔진의 `ShadingCommon.ush`, `ShadingModelsMaterial.ush`, `ShadingModels.ush`, `DeferredShadingCommon.ush`, `DeferredLightingCommon.ush` 다섯 파일이다. 같은 엔진 설치를 사용하는 모든 프로젝트가 이 파일을 공유하며, 한 엔진에는 한 프로젝트의 등록 구성만 활성화할 수 있다.

원본·적용본·복구 기록은 **엔진의 `Engine/Saved/MazelineShaderOverride`**에 저장한다. 프로젝트의 `Saved`와 다른 위치이며 이 백업 폴더를 지우지 않는다. 일반 복원은 **Restore Original → Restart Editor**, 작업 중단 복구는 **Recover Interrupted Operation**을 사용한다. 에디터가 열리지 않을 때의 C++ 커맨드릿과 기존 백업 호환성은 [복구와 명령줄](Plugins/MazelineSuperOverriderMaterial/README.md#복구와-명령줄)에 정리했다.

Deferred에서는 shading-model ID 14와 기존 GBuffer의 per-object 2비트를 사용해 최대 **3개 함수**를 구분한다. 선택 픽셀의 contact-shadow / dynamic-indirect-shadow 플래그는 보존하지 못한다. Substrate, Nanite, 모바일, 경로 추적, 반투명, 새 MIR 생성기와 특수 렌더링 경로는 지원 범위에 포함하지 않는다.

## 검증 기록과 문서

아래는 저장소에 기록된 검증 결과다. 문서 업데이트 과정에서 렌더링·자동 테스트를 다시 실행한 결과는 아니다.

| 범위 | 기록된 결과 | 상세 |
| --- | --- | --- |
| 최신 Directional Ramp | Editor·Win64 Development 빌드, DX12/SM6 Forward·Deferred 유효 캡처 22개, 커브 키 수정 후 방향광 12개 재검증 | [Directional Ramp](Docs/Directional-Ramp.md#검증-2026-11-03) |
| GGX 파라미터 | Ramp 추가 후 두 렌더링 경로의 파라미터 변경 8개 캡처 회귀 통과 | [Directional Ramp](Docs/Directional-Ramp.md), [스타일 구성](Docs/Style-Material-Setup.md) |
| 패치 관리자·노드 미리보기 | 최종 네이티브 테스트 10개 통과, 엔진 다섯 파일 복원·재적용 및 미리보기 수정 전후 렌더링 일치 확인 | [Directional Ramp](Docs/Directional-Ramp.md) |
| 과거 0.2 패키징 | DX11/SM5 Forward·Deferred Win64 Development 쿠킹·패키징·실행 성공 | [0.2 검증 이력](Docs/Validation-Results.md) |

**현재 Ramp·GGX 확장 버전의 쿠킹·패키징은 미검증이다.** Opaque Forward의 Rect Light는 실제 조명이 생성되지 않아 Ramp 격리 검증의 합격 범위에서 제외했다. 게임 타깃 빌드나 `UnrealEditor -game` 캡처를 패키징 검증으로 간주하지 않는다.

- [플러그인 사용법](Plugins/MazelineSuperOverriderMaterial/README.md): Custom Expression, 출력 노드, 패치 UI, 복구 명령.
- [개발 상태](Docs/Development-Status.md): 현재 기능과 지원 범위.
- [구현 계획](Docs/Implementation-Plan.md), [BxDF 통합](Docs/UE58-BxDF-Integration.md): 설계와 구현 배경.
- [C++ 전환 검증](Docs/Native-Patch-Validation.md): 초기 네이티브 관리자 전환 이력.

문서에서 참조하는 `Saved` 아래 로그·캡처·백업·패키지는 로컬 검증 산출물로 저장소 복제본에 포함되지 않는다. 예를 들어 `Saved/Packaged/Forward/Windows`, `Saved/Packaged/Deferred/Windows`와 `Saved/PackagedValidation/*.png`는 과거 패키징 결과다.

## 저장소 관리

작업은 `git switch -c feature/작업명`으로 브랜치를 만든 뒤 변경 파일을 확인하고 커밋한다. `.uasset`과 `.umap`은 Git LFS로 관리하며 빌드 결과, 캐시, 생성된 IDE 프로젝트, RiderLink는 추적하지 않는다. `.vsconfig`와 `Saved/UnrealBuildTool/BuildConfiguration.xml`은 빌드 환경 재현을 위해 추적한다.

Android File Server 인증 토큰은 추적되는 `Config/DefaultEngine.ini`에 저장하지 않고 로컬 `Saved/Config/WindowsEditor/Engine.ini`에서 관리한다.
