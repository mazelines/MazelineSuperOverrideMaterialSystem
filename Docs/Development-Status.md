# 개발 상태 (2026-11-03)

현재 Toon은 설치된 엔진의 `SpecularGGX()`에서 D 항만 조절한다. `ToonBlend` 없이 `Sharpness` 0은 원래 GGX, 중간값은 분포 대조도 증가, 1은 선명한 경계를 만든다. **Roughness·Sharpness·Threshold는 M_Toon의 Scalar Parameter이며 MI_Toon에서 조절한다.** 기본값은 각각 0.5 / 0.5 / 0.25다. 기존 Base Color·Metallic·Roughness·Specular·AO 입력은 보존한다.

M_Toon의 Shading Model은 Default Lit이다. Mazeline GGX Output 노드의 두 입력으로 Sharpness·Threshold를 전달하고, 패치가 이 머티리얼의 Deferred GBufferD 출력을 활성화한다. Clear Coat 핀을 사용하던 초기 구조는 전용 노드로 전환했다. 네이티브 관리자는 DeferredLightingCommon.ush까지 다섯 파일을 관리하며 기존 원본 백업은 그대로 보존한다. 인스턴스 파라미터 변경에는 엔진 재패치가 필요 없다.

Directional Light의 diffuse에는 Curve Atlas 기반 Ramp LUT를 추가했다. Custom의 RampLUT에 연결한 Atlas를 함수별로 등록하고, RampRow는 Mazeline Ramp Output을 통해 CustomData.z로 전달한다. 실제 LUT 샘플링은 광원 계산에서 수행한다. Point·Spot·Rect diffuse와 기존 specular는 RampRow의 영향을 받지 않는다. Linear / Soft / Steps 세 커브를 제공하며 기본값은 Steps(2)다. 데이터 흐름과 사용법은 [Directional Ramp](Directional-Ramp.md)에 정리했다.

GGX 추가 단계에서 VS 2022 Editor·Win64 Development 게임 타깃 빌드와 DX12/SM6 Forward·Deferred의 파라미터 변경 8개 사례를 검증했다. 당시 Default Lit 전환 후 8개 캡처는 Clear Coat 전달 방식의 합격 캡처와 모두 픽셀 단위로 일치했다. Example의 Mazeline Toon에는 MI_Toon을 할당했다. 이 이력은 [스타일 구성](Style-Material-Setup.md)의 “Default Lit 전환 검증” 절에 보관한다. Ramp 버전의 결과는 [Directional Ramp](Directional-Ramp.md)를 참조한다. 이번 버전의 쿠킹·패키징 검증은 수행하지 않았다.

현재 활성 예제는 Stylized(1) / Toon(2)이다. HybridToon 머티리얼과 HLSL은 제거하고 Example의 해당 슬롯을 MI_Toon으로 교체했다. 함수 번호 3은 미등록 상태로 남겨 사용자 함수 등록 기능을 유지한다. 삭제 전 상태는 `Saved/HybridRetirement/latest-backup.txt`의 폴더에 보관한다. [스타일 구성](Style-Material-Setup.md)을 참조한다. 아래 C++ 전환 시점의 바이트 일치·GPU 회귀 기록은 이전 예제 함수에 대한 이력이며, InputValidation 오버라이드는 현재 비활성 상태다.

플러그인 **0.3.0**에서 엔진 패치 관리와 Custom Expression 함수 수집을 플러그인 C++으로 통합했다. **Project Settings → Plugins → Mazeline Super Override Material**에서 상태 확인, 적용·갱신, 원본 복원, 중단 복구와 재시작을 수행한다. PythonScriptPlugin 의존성은 제거했다.

## 초기 C++ 패치 관리자 전환 검증 이력

- VS 2022 / MSVC 14.44.35228 에디터 빌드 성공: `Saved/Logs/NativePatchBuildFinal.log`.
- Python 비활성 상태의 네이티브 자동 테스트 5개 통과: 적용·갱신·복원, 작업 중단 후 복구, 외부 변경·손상·소유권 충돌 거부, 기존 Python 백업 호환, 설정 페이지 위젯 등록. `Saved/Automation/NativePatchReportFinal/index.json`.
- 실제 UE 5.8.4 설치에서 C++ 커맨드릿의 상태 확인 → 원본 복원 → 재적용 성공. `Saved/Logs/NativeCommandlet-*.log`.
- 실제 Project Settings에서 Restore와 Apply를 클릭하여 파일 변경과 상태 전환 확인. `Saved/Logs/NativeSettingsUIFinal.log`.
- C++ 생성 셰이더 세 파일은 0.2에서 GPU 검증한 적용본과 바이트 단위로 같다. 상세 근거는 [C++ 전환 검증](Native-Patch-Validation.md)에 있다.
- 기존 Forward/Deferred 입력 보존·Roughness 변경·일반 머티리얼 회귀·Win64 Development 패키징 결과는 [0.2 렌더링 검증 결과](Validation-Results.md)에 보존했다. 이번 변경은 에디터 패치 관리자이며 렌더링 기능을 확장하지 않았다.

## 현재 엔진과 복원

`C:/Program Files/Epic Games/UE_5.8`에는 Forward/Deferred용 Stylized(1)·Toon(2) 패치가 적용돼 있다. 최초 원본과 세대별 적용본, 트랜잭션은 엔진의 `Engine/Saved/MazelineShaderOverride`에 보관한다. 기존 Python 관리자의 format 1을 유지하며 백업을 다시 초기화하지 않는다. 추가된 파일의 원본은 legacy-extension.json과 ramp-extension.json이 가리키는 스냅샷에 보관한다.

원본 복원은 설정 화면의 **Restore Original → Restart Editor**를 사용한다. 중단 상태에서는 **Recover Interrupted Operation**으로 작업 직전 상태로 돌아간다. 에디터를 열 수 없을 때는 `-run=MazelineShaderPatch -Action=recover -nullrhi` C++ 커맨드릿을 사용한다. 정확한 실행 예는 [플러그인 사용법](../Plugins/MazelineSuperOverriderMaterial/README.md)에 있다.

## 지원 범위

- Epic 설치형 UE 5.8.4, Windows DX12/SM6, Opaque Default Lit, Forward/Deferred. 이전 DX11/SM5 결과는 과거 검증 이력이다.
- Deferred ID 1~3, shading-model ID 14. 선택 픽셀의 접촉 그림자·동적 간접 그림자 플래그를 사용한다. Substrate, Nanite, 경로 추적, 모바일 및 특수 렌더링 경로는 지원 범위 밖이다.
- HLSL은 연결된 Custom Expression의 `/Project/` include에서 수집한다. RampLUT에는 함수당 하나의 Curve Atlas 바인딩을 지원한다. 중첩 include, 머티리얼 함수 내부 검색, 그 밖의 임의 텍스처·샘플러 바인딩 및 머티리얼 그래프의 광원별 재실행은 지원하지 않는다.
- 현재 에디터에서 패치 후 재시작해야 한다. PIE나 같은 엔진의 다른 에디터가 실행 중이면 적용을 막는다. 동일 엔진 설치를 쓰는 모든 프로젝트가 패치된 전역 셰이더를 읽는다.
- `Tools/*.py`는 과거 이력 및 프로젝트 에셋·렌더링 검증용 개발 도구다. 기존 Python `ManagePatch.ps1`은 이전 구현 이력이며 현재 플러그인은 Python 도구를 호출하지 않는다.
