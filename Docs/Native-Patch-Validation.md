# C++ 패치 관리자 전환 검증 — 2026-11-02

대상은 플러그인 0.3.0, Epic 설치형 UE 5.8.4, Visual Studio 2022 / MSVC 14.44.35228이다. 머티리얼 함수 수집과 엔진 패치·복원·복구를 C++으로 옮기고 Project Settings에 조작 화면을 등록했다. 이번 변경으로 셰이더 기능이나 지원 렌더링 경로를 늘리지는 않았다.

## 빌드와 자동 테스트

- 새 C++ 파일을 포함한 VS 2022 솔루션 재생성 성공: `Saved/Logs/NativeGenerateVS2022.log`. 엔진 .NET 10 자동화용 솔루션은 별도 생성되며 플러그인 C++ 빌드는 VS 2022를 사용한다.
- 최종 에디터 빌드 성공: `Saved/Logs/NativePatchBuildFinal.log`. VS 2022 14.44.35228 도구 체인과 Windows SDK 10.0.22621.0 확인.
- `-DisablePython`으로 실행한 자동 테스트 **5개 성공, 실패 0, 경고 포함 성공 0**: `Saved/Automation/NativePatchReportFinal/index.json` 및 `Saved/Logs/NativePatchAutomationFinal.log`.
- 테스트 소스: `Plugins/MazelineSuperOverriderMaterial/Source/MazelineSuperOverriderMaterial/Private/Tests/MazelineNativePatchTests.cpp`.

| 테스트 | 검증한 동작 |
| --- | --- |
| Lifecycle | 표준 SHA-256 벡터, 적용, 중복 적용, 함수 갱신, 원본 복원과 재적용 |
| InterruptedTransactions | 적용·갱신·복원 중 파일 쓰기 실패를 주입하고 새 관리자에서 직전 상태 복구 |
| ConflictsAndIntegrity | 다른 프로젝트 소유권, 외부 수정, 손상 백업, 중단 이후 외부 수정, 알 수 없는 삽입 지점 거부 |
| ExistingPythonBackupCompatibility | 실제 기존 Python 백업 읽기, 실제 Custom Expression 수집, 생성 셰이더 세 파일의 바이트 일치 |
| ProjectSettings | Project/Plugins 카테고리, 정확한 설정 표시 이름, 유지되는 Slate 위젯 등록 |

첫 실행에서는 설정 모듈이 위젯을 약한 참조로 보관하는 문제가 발견됐다. 플러그인 모듈이 위젯의 수명을 유지하도록 고쳤고 위 최종 실행은 모두 통과했다. 이후 좁은 창의 버튼 줄바꿈과 UI 결과 로그만 추가했으며 다시 빌드하고 실제 화면에서 검증했다.

## 실제 설치 엔진과 UI

1. Python을 비활성화한 C++ 커맨드릿에서 `status → restore → apply`를 실행해 모두 성공했다. `Saved/Logs/NativeCommandlet-status.log`, `NativeCommandlet-restore.log`, `NativeCommandlet-apply.log`.
2. DX11/SM5 에디터를 `-DisablePython`으로 실행해 **Project Settings → Plugins → Mazeline Super Override Material**을 열었다.
3. **Restore Original** 클릭 후 상태가 `restored`, 소유자가 `(none)`, `Restart required`로 표시됐다. 세 엔진 파일의 SHA-256이 최초 원본 백업과 일치함을 별도로 확인했다.
4. **Apply / Update Patch** 클릭 후 `applied`, 현재 프로젝트 소유, SHA-256 일치와 재시작 안내를 확인했다. `Saved/Logs/NativeSettingsUIFinal.log`에 `MAZELINE_SETTINGS_RESULT restore success` 및 `apply success`가 기록됐다.
5. **Restart Editor**와 엔진의 재시작 확인 창을 거쳐 새 에디터 프로세스가 시작됐다. 설정 화면은 `applied`이며 재시작 안내가 사라지고 Restart 버튼이 비활성화됐다. `Saved/Logs/NativeSettingsUIFinal_2.log`에서 Python 비활성 및 설정 등록을 확인했다. 셰이더 오류는 발견되지 않았다.
6. 좁은 설정 창에서는 작업 버튼이 다음 줄로 넘어가며 잘리지 않는다. Plugins 목록의 Mazeline 항목과 설정 화면을 직접 확인했다. 중단 복구는 실제 엔진에 고의 오류를 내지 않고 위 격리된 자동 테스트로 검증했다.

## 최종 파일과 기존 데이터

최종 엔진 상태 기록과 세 파일의 해시는 `Saved/Automation/NativePatchFinalHashes.json`에 있다. `pending` 작업은 없으며 최초 baseline `151c2919bdb04771b0db58dac376441c`를 보존한다. 활성 snapshot은 `958ff9764a72257baced5eb213091a83`이다.

세 파일 모두 활성 백업 및 기존 GPU 검증 세대 `6e97983817be49cd8a4778ffb05575bb`와 SHA-256이 일치한다. 따라서 기존 [렌더링·패키징 검증](Validation-Results.md)의 셰이더 코드가 유지됨을 확인했다. 이번 C++ 전환에서 전체 GPU 정량 비교와 게임 패키징을 다시 수행한 것은 아니다.

기존 Python 소스와 실행 파일은 작업 이력으로 보존하되 플러그인은 호출하지 않는다. 플러그인 descriptor/Build.cs의 PythonScriptPlugin 의존성을 제거했고, 배포 필터에서도 이전 Tools 및 Content/Python을 제외했다. 프로젝트의 Python 활성 설정은 기존 테스트 에셋 생성 도구용이다.

## 재현

에디터를 닫은 상태에서 자동 테스트를 실행한다. 실제 Python 백업 호환 테스트는 현재 엔진에 정상 패치가 적용돼 있어야 한다.

```powershell
& 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' `
  'D:/UnrealProject/Projects/MazeLineMaterialPlugin/MazeLineMaterialTest.uproject' `
  -nullrhi -DisablePython -unattended `
  '-ExecCmds=Automation RunTests Mazeline.NativePatch' `
  '-TestExit=Automation Test Queue Empty' `
  '-ReportExportPath=D:/UnrealProject/Projects/MazeLineMaterialPlugin/Saved/Automation/NativePatchReport'
```

종료 코드만 보지 않고 보고서의 `failed`, `succeeded`, 개별 테스트 결과를 확인한다. 적용·복원 사용법과 비정상 기동 시 C++ 복구 명령은 플러그인 README에 있다.
