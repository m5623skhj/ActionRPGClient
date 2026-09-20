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
- TownServer TCP 자동 연결과 재연결
- 서버 권위 위치 보정과 원격 플레이어 데드레커닝
- 석터 기반 Appear/Disappear 처리

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
  Network/             TownServer TCP 통신과 패킷 직렬화
  Platform/            Win32 창과 메시지 처리
  Resources/           에셋 카탈로그와 스프라이트 애니메이션
Assets/
  Data/                게임 데이터 INI 파일
  Images/              이미지와 스프라이트 시트
  Audio/               음악과 효과음 배치 위치
```

`TownMapEditor/` 프로젝트는 마을 이동 영역과 시작 위치를 배치하는 별도 도구입니다.

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

클라이언트는 기본적으로 `127.0.0.1:7777`의 TownServer에 연결합니다. 서버를 먼저
실행한 뒤 클라이언트를 여러 번 실행하면 원격 플레이어의 Appear, 이동,
Disappear를 확인할 수 있습니다.

## Town Map Editor

```powershell
artifacts/bin/x64/Debug/TownMapEditor.exe `
  C:\Users\KimHyeongJin\source\repos\ActionRPGServer\ActionRPGServer\TownServer\Data\TownMap.json `
  C:\Users\KimHyeongJin\source\repos\ActionRPGClient\ActionRPGClient\Assets
```

- `Add at X/Y`: 입력한 월드 좌표에 이미지 추가
- `Add Right`: 선택 이미지의 오른쪽 끝에 간격 없이 이미지 추가
- `V`: 이미지 선택. 드래그 또는 X/Y 입력으로 위치 수정
- `W`: 이동 가능 다각형 작성. 좌클릭으로 점을 추가하고 `Enter`로 완성
- `B`: 진입 금지 다각형 작성
- `P` 또는 우클릭: 플레이어 시작 위치 배치
- `Clear Mode Areas`: 현재 W/B 모드의 영역을 모두 제거
- 가운데 버튼 드래그 또는 `Space+좌클릭`: 캔버스 이동
- 마우스 휠: 커서 위치를 중심으로 확대·축소
- `F`: 전체 맵 맞춤, `S`: JSON 저장

추가한 이미지는 `Assets/Images/Towns`로 복사됩니다. 너비가 100인 이미지가
X=0에 있을 때 `Add Right`로 추가한 다음 이미지는 X=100에 배치됩니다.
두 번째 실행 인자를 생략하면 편집기는 프로젝트의 `Assets` 디렉터리를 자동으로 찾습니다.

맵을 수정한 후 TownServer를 다시 빌드·실행하면 서버가 새 영역을 로드합니다.
이미지를 새로 추가했다면 ActionRPGClient도 다시 빌드하여 해당 이미지를 실행 폴더의
`Assets`로 복사해야 합니다.
