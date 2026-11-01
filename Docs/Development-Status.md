# 개발 상태 (2026-11-02)

현재 예제는 Stylized(1) / Toon(2) / HybridToon(3)으로 전환했다. 기존 비교 맵의 세 머티리얼과 연결 그래프를 수정하고 두 렌더링 경로의 새 캡처를 확인했다. [세 스타일 구성](Style-Material-Setup.md)을 참조한다. 아래 C++ 전환 시점의 바이트 일치·GPU 회귀 기록은 이전 예제 함수에 대한 이력이며, InputValidation 오버라이드는 현재 비활성 상태다.

플러그인 **0.3.0**에서 엔진 패치 관리와 Custom Expression 함수 수집을 플러그인 C++으로 통합했다. **Project Settings → Plugins → Mazeline Super Override Material**에서 상태 확인, 적용·갱신, 원본 복원, 중단 복구와 재시작을 수행한다. PythonScriptPlugin 의존성은 제거했다.

## 검증

- VS 2022 / MSVC 14.44.35228 에디터 빌드 성공: `Saved/Logs/NativePatchBuildFinal.log`.
- Python 비활성 상태의 네이티브 자동 테스트 5개 통과: 적용·갱신·복원, 작업 중단 후 복구, 외부 변경·손상·소유권 충돌 거부, 기존 Python 백업 호환, 설정 페이지 위젯 등록. `Saved/Automation/NativePatchReportFinal/index.json`.
- 실제 UE 5.8.4 설치에서 C++ 커맨드릿의 상태 확인 → 원본 복원 → 재적용 성공. `Saved/Logs/NativeCommandlet-*.log`.
- 실제 Project Settings에서 Restore와 Apply를 클릭하여 파일 변경과 상태 전환 확인. `Saved/Logs/NativeSettingsUIFinal.log`.
- C++ 생성 셰이더 세 파일은 0.2에서 GPU 검증한 적용본과 바이트 단위로 같다. 상세 근거는 [C++ 전환 검증](Native-Patch-Validation.md)에 있다.
- 기존 Forward/Deferred 입력 보존·Roughness 변경·일반 머티리얼 회귀·Win64 Development 패키징 결과는 [0.2 렌더링 검증 결과](Validation-Results.md)에 보존했다. 이번 변경은 에디터 패치 관리자이며 렌더링 기능을 확장하지 않았다.

## 현재 엔진과 복원

`C:/Program Files/Epic Games/UE_5.8`에는 Forward/Deferred 3함수 패치가 적용돼 있다. 최초 원본과 세대별 적용본, 트랜잭션은 엔진의 `Engine/Saved/MazelineShaderOverride`에 보관한다. 기존 Python 관리자의 format 1을 유지하며 백업을 다시 초기화하지 않는다.

원본 복원은 설정 화면의 **Restore Original → Restart Editor**를 사용한다. 중단 상태에서는 **Recover Interrupted Operation**으로 작업 직전 상태로 돌아간다. 에디터를 열 수 없을 때는 `-run=MazelineShaderPatch -Action=recover -nullrhi` C++ 커맨드릿을 사용한다. 정확한 실행 예는 [플러그인 사용법](../Plugins/MazelineSuperOverriderMaterial/README.md)에 있다.

## 지원 범위

- Epic 설치형 UE 5.8.4, Windows DX11/SM5, Opaque Default Lit, Forward/Deferred.
- Deferred ID 1~3, shading-model ID 14. 선택 픽셀의 접촉 그림자·동적 간접 그림자 플래그를 사용한다. Substrate, Nanite, 경로 추적, 모바일 및 특수 렌더링 경로는 지원 범위 밖이다.
- HLSL은 연결된 Custom Expression의 `/Project/` include에서 수집한다. 중첩 include, 머티리얼 함수 내부 검색, 텍스처·샘플러 바인딩 및 임의 머티리얼 그래프의 광원별 재실행은 지원하지 않는다.
- 현재 에디터에서 패치 후 재시작해야 한다. PIE나 같은 엔진의 다른 에디터가 실행 중이면 적용을 막는다. 동일 엔진 설치를 쓰는 모든 프로젝트가 패치된 전역 셰이더를 읽는다.
- `Tools/*.py`와 기존 `ManagePatch.ps1`은 이전 구현 이력이며 현재 플러그인은 호출하지 않는다.
