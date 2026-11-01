# Mazeline Super Overrider Material

Epic 설치형 **Unreal Engine 5.8.4**, **Visual Studio 2022**용 플러그인이다. Default Lit 머티리얼의 연결된 Custom Expression에 `MAZELINE_SHADER_OVERRIDE`를 정의하고, 별도 HLSL에서 광원마다 DiffuseColor와 Roughness를 계산한다. Base Color와 Metallic은 머티리얼 핀에서 전달된다. 엔진 C++ 바이너리를 다시 빌드하지 않으며, 엔진 셰이더 세 파일을 직접 패치한다.

실제 렌더링 검증 범위는 **Windows DX11/SM5의 Opaque Default Lit, Forward 및 Deferred**다. 두 경로 모두 에디터 렌더링, Win64 Development 쿠킹·패키징·실행을 검증했다. Base Color/Metallic/Roughness 입력 보존, HLSL의 Roughness 출력 변경, 패치를 사용하지 않는 일반 머티리얼의 원본 엔진 대비 비교도 통과했다. Substrate, Nanite, 모바일, 경로 추적, 반투명 및 특수 렌더링 경로는 지원 범위에 포함하지 않는다.

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

Deferred 광원 셰이더는 머티리얼 그래프를 다시 실행하지 않는다. 등록된 HLSL 함수가 위 입력을 사용해 실행된다. 현재 텍스처·샘플러·머티리얼 전용 유니폼 바인딩과 중첩 `#include`, 머티리얼 함수 내부의 Custom 등록은 지원하지 않는다. 전역 helper 이름과 include guard는 다른 등록 파일과 충돌하지 않게 작성한다. HLSL 문법의 최종 판정은 실제 엔진 셰이더 컴파일 결과로 확인한다.

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

원본·적용본·트랜잭션 기록은 **엔진의 `Engine/Saved/MazelineShaderOverride`**에 저장된다. 기존 Python 관리자가 만든 format 1 백업과 활성 상태를 그대로 읽으며, 최초 원본을 덮어쓰지 않는다. 이 폴더를 지우면 자동 복구할 수 없다. 외부 변경이나 손상된 백업이 발견되면 덮어쓰지 않고 중단한다. 한 엔진에는 한 프로젝트의 레지스트리만 활성화할 수 있으며, 다른 프로젝트에서 교체하려면 기존 프로젝트로 복원해야 한다. 엔진 파일 패치는 같은 설치를 사용하는 다른 프로젝트에도 영향을 준다.

## 구현 위치와 제한

- `Source/MazelineSuperOverriderMaterial/Private/MazelineShaderPatchManager.cpp`: 함수 수집, 세 셰이더의 삽입 코드와 패치·복원·복구 로직. `Generate()`에 실제 패치 내용을 담았다.
- `SMazelineMaterialSettings.cpp`: 프로젝트 설정 화면과 버튼.
- `MazelineSuperOverriderMaterialModule.cpp`: 설정 등록과 메뉴·콘솔 명령.
- `MazelineShaderPatchCommandlet.cpp`: Python 없이 실행하는 명령줄 진입점.

Deferred에서는 shading-model ID 14와 기존 GBuffer의 per-object 2비트를 사용한다. 동시에 등록 가능한 함수는 **3개**이고, 선택한 픽셀은 기존 contact-shadow / dynamic-indirect-shadow 플래그를 보존하지 못한다. Base Color·Metallic·Roughness 채널은 유지한다. 다른 셰이더 플러그인과 ID/채널이 충돌할 수 있으며, 지원되지 않는 엔진 버전이나 맞지 않는 삽입 지점은 패치를 거부한다.

`Tools/*.py`, `Content/Python/build_deferred_registry.py`, 기존 `Saved/Mazeline/ManagePatch.ps1`은 0.2 개발 이력이다. 0.3 플러그인은 이들을 호출하지 않으며 배포 필터에서도 이전 Python 도구를 제외한다. 현재 패치 조작에는 위 C++ 설정 화면이나 커맨드릿을 사용한다. 이전 `Saved/MazelineForwardPatch`, `Saved/MazelineDeferredTwoFunctions`도 이력 보존용이며 현재 엔진 복원에 사용하지 않는다.
