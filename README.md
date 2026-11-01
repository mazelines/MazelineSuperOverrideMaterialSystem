# Mazeline Super Overrider Material 테스트 프로젝트

## 저장소와 버전관리

저장소: https://github.com/mazelines/MazelineSuperOverrideMaterialSystem

Git LFS를 설치한 뒤 다음 명령으로 프로젝트를 받는다. `.uasset`과 `.umap`은 Git LFS로 관리한다.

```powershell
git lfs install
git clone https://github.com/mazelines/MazelineSuperOverrideMaterialSystem.git
cd MazelineSuperOverrideMaterialSystem
git lfs pull
```

UE 5.8과 아래의 Visual Studio 도구를 준비한 뒤 `MazeLineMaterialTest.uproject`에서 프로젝트 파일을 생성하고 `Development Editor / Win64`로 빌드한다. `.vsconfig`와 `Saved/UnrealBuildTool/BuildConfiguration.xml`은 빌드 환경 재현을 위해 추적한다.

작업은 `git switch -c feature/작업명`으로 브랜치를 만든 뒤 변경 파일을 확인하고 커밋한다.

```powershell
git status
git add <변경한 파일 또는 폴더>
git commit -m "변경 내용"
git push -u origin HEAD
```

빌드 결과, 캐시, 생성된 IDE 프로젝트, RiderLink는 추적하지 않는다. Android File Server 인증 토큰은 추적되는 `Config/DefaultEngine.ini`에 저장하지 않고 로컬 `Saved/Config/WindowsEditor/Engine.ini`에서 관리한다. 문서의 `Saved` 아래 패키지·캡처 결과물은 로컬 검증 산출물이므로 복제본에 포함되지 않는다.

## 실행 환경

`MazeLineMaterialTest.uproject`를 `C:/Program Files/Epic Games/UE_5.8`의 UE **5.8.4**로 실행한다. C++ 타깃과 프로젝트 생성 설정은 **Visual Studio 2022 / MSVC 14.44.35228**을 사용한다.

플러그인은 `Plugins/MazelineSuperOverriderMaterial`에 있으며 프로젝트에서 활성화되어 있다. 시작 맵 `/Game/MazelineTests/L_ForwardComparison`에서 Stylized(1), Toon(2), HybridToon(3)의 렌더링을 비교할 수 있다. 프로젝트의 Windows 기본 실행 환경은 **DX12 / SM6**이며 DX11 타깃은 해제했다. `.uproject`를 실행할 때 `-d3d11 -sm5` 옵션을 사용하지 않는다. 기존 DX11/SM5 검증 기록은 당시 결과로 보존한다. 입력 보존 비교용 맵 `/Game/MazelineTests/InputValidation/L_InputValidation`의 구형 오버라이드 등록은 현재 비활성 상태다.

- 머티리얼 작성과 패치 적용·갱신·복원: [플러그인 사용법](Plugins/MazelineSuperOverriderMaterial/README.md)
- 개발 단계와 검증 범위: [구현 계획](Docs/Implementation-Plan.md), [검증 결과](Docs/Validation-Results.md), [개발 상태](Docs/Development-Status.md)
- 실제 패키지 결과: `Saved/Packaged/Forward/Windows`, `Saved/Packaged/Deferred/Windows`
- 패키지 렌더링 캡처: `Saved/PackagedValidation/Forward.png`, `Saved/PackagedValidation/Deferred.png`

Custom Expression의 Additional Define 이름은 **`MAZELINE_SHADER_OVERRIDE`**다. 함수 선택 값은 1~3이다. Base Color, Roughness, Metallic 핀을 연결하고 광원별 HLSL 함수 파일을 Custom의 Include File Paths에 지정한다. **Project Settings → Plugins → Mazeline Super Override Material**에서 **Apply / Update Patch**를 누른 뒤 **Restart Editor**로 재시작한다. Tools → Mazeline: Shader Patch Settings로도 같은 화면을 열 수 있다.

엔진 백업은 **엔진의 `Engine/Saved/MazelineShaderOverride`**에 저장된다. 프로젝트의 `Saved`를 지우는 것과 별개이며, 엔진 백업 폴더를 지우지 않는다. 같은 설치를 쓰는 다른 프로젝트로 전환하기 전에 필요한 경우 설정 화면의 **Restore Original**로 원본을 복원하고 재시작한다. 지원 범위와 GBuffer 채널 사용에 따른 제한은 플러그인 사용법을 참조한다.

0.3.0부터 등록 함수 수집, 상태 확인, 패치·복원·중단 복구는 모두 플러그인 내부 C++에서 실행한다. Python 플러그인이나 PowerShell 런처가 필요 없다. 기존 Python 관리자의 엔진 백업은 그대로 이어서 사용한다. [C++ 전환 검증](Docs/Native-Patch-Validation.md)을 참조한다.

현재 예제의 이름과 함수 매칭, 기존 검증 에셋 보관 위치는 [세 스타일 구성](Docs/Style-Material-Setup.md)을 참조한다.
