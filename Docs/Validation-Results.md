# UE 5.8.4 검증 결과 — 2026-11-01

이 문서는 0.2의 렌더링·패키징 검증 이력이다. 0.3에서 패치 관리자는 C++과 Project Settings로 교체했다. 현재 사용법은 플러그인 README, 새 검증은 [C++ 전환 검증](Native-Patch-Validation.md)을 참조한다. 아래 Python 준비·관리 명령은 당시 검증 기록이다.

검증 대상은 Epic 설치형 UE 5.8.4의 Windows DX11/SM5, Opaque Default Lit, Forward 및 Deferred다. 이 범위에서 기능 구현, 복구 가능한 엔진 패치, 에디터 및 패키지 렌더링 검증을 완료했다. Substrate, Nanite, 모바일, 경로 추적, 반투명, 특수 렌더링 경로와 Shipping 구성은 이 결과의 범위에 포함하지 않는다.

## 빌드와 플러그인 동작

| 항목 | 결과 | 근거 |
| --- | --- | --- |
| VS 2022 / MSVC 14.44.35228 에디터 및 게임 빌드 | 성공 | `Saved/Logs/BuildGameValidation.log`, `Saved/Logs/FinalEditorBuild.log`, 패키지 빌드 로그 |
| 최종 플러그인 준비 명령 | 성공 | `Saved/Logs/FinalPluginSmoke.log`: `MAZELINE_PREPARE_RESULT success`, UE 5.8.4 지원 버전 확인 |
| Tools 메뉴 등록 및 공유 실행 로직 | 확인 | 같은 로그의 `Deferred registry menu entry: registered`; 준비 명령은 메뉴와 같은 Python 생성 로직 호출 |
| Forward Win64 Development 쿠킹·패키징 | 성공, 종료 코드 0 | `Saved/Logs/PackageForward.log` |
| Deferred Win64 Development 쿠킹·패키징 | 성공, 종료 코드 0 | `Saved/Logs/PackageDeferred.log` |
| 두 패키지 실행 및 GPU 캡처 | 성공 | `Saved/Logs/PackagedForwardRender.log`, `Saved/Logs/PackagedDeferredRender.log`의 완료 마커와 렌더링 CVar |

패키지는 `Saved/Packaged/Forward/Windows`와 `Saved/Packaged/Deferred/Windows`에 있다. 렌더링 원본은 `Saved/PackagedValidation/Forward.png`, `Deferred.png`다. 위쪽 세 구는 일반 머티리얼, 함수 1, 함수 2이며 두 광원에서 서로 다른 Toon 경계를 확인했다. 아래쪽 작은 회색 구는 테스트 게임의 기본 Pawn이다. 패키지 캡처는 에디터를 통해 찍은 화면이 아니라 각 패키지 실행 파일의 게임 뷰포트에서 생성했다.

## 입력, 함수 출력, 일반 머티리얼 회귀

1280×720 RGB 캡처를 같은 카메라·기하·두 광원 조건에서 비교했다. 세 Base Color, Metallic 0/0.5/1, Roughness 0.15/0.5/0.85의 9개 조합으로 입력 보존을 확인했다. Roughness 출력 검증은 Metallic=1에서 일반 머티리얼 Roughness=0.85와 **오버라이드 입력 0.15 → HLSL 출력 0.85**를 비교해 diffuse Toon 효과를 분리했다. 일반 머티리얼 회귀 검증은 실제 엔진 셰이더를 원본으로 복원한 뒤, 매크로가 없는 같은 머티리얼의 화면을 패치 적용 시 화면과 비교했다.

| 비교 | Forward 평균 / 최대 오차 | Deferred 평균 / 최대 오차 |
| --- | --- | --- |
| 입력 보존 | 0.0185865 / 1 | 0.00000144676 / 2 |
| Roughness 출력 변경 | 0.00409505 / 3 | 0.0149060 / 3 |
| 매크로 없는 일반 머티리얼: 원본 vs 패치 | 0.0186621 / 1 | 0 / 0 |

표의 오차는 8비트 RGB 값(0~255)의 전체 이미지 기준이다. 통과 판정은 전체 평균만으로 하지 않았다. 9개 구 각각에 전경 500픽셀 초과, 전경 평균 오차 ≤0.5, 99백분위 오차 ≤2, 최대 오차 ≤10 조건을 적용했고 모두 통과했다. 결과 파일:

- `Saved/InputValidationForwardRun3/comparison.json`, `Saved/InputValidationDeferred/comparison.json`
- `Saved/RoughnessValidationForward/comparison.json`, `Saved/RoughnessValidationDeferred/comparison.json`
- `Saved/UnmarkedRegressionForward/comparison.json`, `Saved/UnmarkedRegressionDeferred/comparison.json`

자동 캡처는 셰이더/로딩 완료를 기다리는 엔진 API를 사용하고, 재진입 방지 후 캡처 완료를 확인한다. 테스트 중 바꾼 머티리얼은 저장하지 않고 원래 참조로 되돌린다. 위 결과는 해당 검증 장면과 조건에 대한 결과이며 모든 머티리얼 그래프와 렌더링 기능에 대한 전수 검사는 아니다.

## 패치 복구

`python -m unittest -v test_shader_patch test_deferred_probe test_manage_shader_patch`의 **23개 테스트 통과**. `Saved/Logs/ManagedPatchTests.txt`에 결과를 보관했다. 실제 파일에 대한 중간 쓰기 실패를 주입해 적용·갱신·복원 중단 후 새 관리자에서 복구되는지 확인했다. 외부 수정, 손상된 백업, 다른 프로젝트의 활성 패치, 오래된 레지스트리, 동시 실행 잠금도 검사했다.

실제 설치 엔진에서도 원본 복원과 재적용을 수행했다. 최종 활성 셰이더 세 파일의 SHA-256은 GPU 및 패키지 검증 당시의 적용본과 일치한다. 최종 상태는 `applied`, `files_match_active_snapshot=true`이며 백업은 `C:/Program Files/Epic Games/UE_5.8/Engine/Saved/MazelineShaderOverride`에 있다. 복원은 에디터 종료 후 `Saved/Mazeline/ManagePatch.ps1 restore`, 중단된 작업은 `recover`를 사용한다.

지원 API와 GBuffer 제한은 [플러그인 사용법](../Plugins/MazelineSuperOverriderMaterial/README.md)에 명시했다. Deferred는 함수 ID 1~3과 shading-model ID 14를 사용하고, 선택된 픽셀의 contact-shadow / dynamic-indirect-shadow 플래그를 보존하지 않는다.
