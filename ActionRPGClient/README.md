# ActionRPGClient

던전형 2D 액션 게임을 위한 Windows 클라이언트 기초 프로젝트입니다.  
Win32 게임 루프 위에 D3D11/DXGI 장치와 Direct2D 렌더링을 구성했습니다.

## 현재 기능

- 방향키 이동과 짧은 시간 내 두 번 입력하는 달리기
- `C` 키 점프
- 방향 입력과 `Z` 키를 조합하는 스킬 커맨드 큐
- 대기, 달리기, 사격 스프라이트 애니메이션
- 직선형 총알과 포물선형 투척물
- 플레이 영역과 상단 배경 영역을 분리한 맵
- INI 파일을 이용한 애니메이션, 스킬, 이펙트 및 투사체 설정

## 조작법

| 입력 | 동작 |
| --- | --- |
| 방향키 | 이동 |
| 같은 방향키 빠르게 두 번 | 달리기 |
| `C` | 점프 |
| `X` | 사격 및 직선형 총알 발사 |
| `V` | 포물선형 돌 투척 |
| `→`, `→`, `Z` | 오른쪽 방향 스킬 |
| `←`, `←`, `Z` | 왼쪽 방향 스킬 |

달리기 판정은 스킬 커맨드 큐와 별도로 관리하므로 방향키 연속 입력이 스킬 커맨드에서 제거되지 않습니다.

## 프로젝트 구조

```text
ActionRPGClient/       C++ 소스 프로젝트
  App/                 프로그램 실행과 게임 루프
  Core/                INI 문서 처리 등 공통 기능
  Game/                플레이어, 맵, 스킬, 투사체
  Graphics/            D3D11/DXGI 장치와 Direct2D 렌더러
  Input/               입력 상태
  Platform/            Win32 창과 메시지 처리
  Resources/           에셋 카탈로그와 스프라이트 애니메이션
Assets/
  Data/                게임 데이터 INI 파일
  Images/              이미지와 스프라이트 시트
  Audio/               음악과 효과음 배치 위치
```

주요 데이터 파일은 다음과 같습니다.

- `Assets/Data/assets.ini`: 논리적 에셋 이름과 파일 경로
- `Assets/Data/animations.ini`: 스프라이트 프레임과 재생 속도
- `Assets/Data/skills.ini`: 스킬 커맨드와 입력 제한 시간
- `Assets/Data/effects.ini`: 스킬 이펙트 정보
- `Assets/Data/projectiles.ini`: 투사체 이동 및 표시 정보

빌드할 때 `Assets` 디렉터리가 실행 파일 옆으로 복사됩니다. 실행 중에는 작업 디렉터리가 아니라 실행 파일 위치를 기준으로 에셋을 찾습니다.

## 빌드 및 실행

Visual Studio 2022에서 `ActionRPGClient.slnx`를 열고 다음 구성을 선택합니다.

- Configuration: `Debug` 또는 `Release`
- Platform: `x64`

명령줄에서는 Visual Studio Developer PowerShell에서 다음과 같이 빌드할 수 있습니다.

```powershell
msbuild .\ActionRPGClient\ActionRPGClient.vcxproj /p:Configuration=Debug /p:Platform=x64
```

Debug 실행 파일은 `artifacts/bin/x64/Debug/ActionRPGClient.exe`에 생성됩니다.
