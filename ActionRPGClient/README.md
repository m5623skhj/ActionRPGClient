# ActionRPGClient

마을 TCP 통신과 2D 액션 기능을 함께 개발하는 Windows 클라이언트입니다.
Win32 게임 루프 위에 D3D11/DXGI 장치와 Direct2D 렌더링을 구성했습니다.

문서 안내:

- [클라이언트 개발 가이드](DEVELOPMENT.md): 구조, 실행 흐름, 기능과 에셋을 추가할 위치
- [마을 패킷 개발 가이드](TOWN_NETWORK.md): TownServer와 주고받는 패킷 추가 절차
- [마을 맵 에디터 설명서](TOWN_MAP_EDITOR.md): 이미지 배치, 이동 영역, 저장과 적용
- [에셋 디렉터리 설명](Assets/README.md): INI와 이미지·오디오 경로 규칙

## 현재 기능

- 방향키 이동과 달리기 판정(현재 마을에서는 걷기만 허용)
- `C` 키 점프
- 방향 입력과 `Z` 키를 조합하는 스킬 커맨드 큐
- 대기, 달리기, 사격 스프라이트 애니메이션
- 직선형 총알과 포물선형 투척물
- 플레이 영역과 상단 배경 영역을 분리한 맵
- INI 파일을 이용한 애니메이션, 스킬, 이펙트 및 투사체 설정
- TownServer TCP 자동 연결과 재연결
- 서버 권위 위치 보정과 원격 플레이어 데드레커닝
- 섹터 기반 Appear/Disappear 처리

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

달리기 판정은 스킬 커맨드 큐와 별도로 관리하지만 현재 마을에서는 비활성화되어 있습니다.

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
TownMapEditor/          마을 맵 JSON을 작성하는 별도 프로젝트
```

`TownMapEditor/`는 마을 배경 이미지, 이동 영역, 시작 위치를 배치하는 도구입니다.

주요 데이터 파일은 다음과 같습니다.

- `Assets/Data/assets.ini`: 논리적 에셋 이름과 파일 경로
- `Assets/Data/animations.ini`: 스프라이트 프레임과 재생 속도
- `Assets/Data/skills.ini`: 스킬 커맨드와 입력 제한 시간
- `Assets/Data/effects.ini`: 스킬 이펙트 정보
- `Assets/Data/projectiles.ini`: 투사체 이동 및 표시 정보

빌드할 때 `Assets` 디렉터리가 실행 파일 옆으로 복사됩니다. 실행 중에는 작업 디렉터리가 아니라 실행 파일 위치를 기준으로 에셋을 찾습니다. 마을 배경은 `assets.ini` 등록 대신 서버 맵 JSON의 `asset` 상대 경로를 사용합니다.

## 빌드 및 실행

Visual Studio 2022에서 `ActionRPGClient.slnx`를 열고 다음 구성을 선택합니다.

- 구성: `Debug` 또는 `Release`
- 플랫폼: `x64`

명령줄에서는 Visual Studio Developer PowerShell에서 다음과 같이 빌드할 수 있습니다.

```powershell
msbuild .\ActionRPGClient\ActionRPGClient.vcxproj /p:Configuration=Debug /p:Platform=x64
```

Debug 실행 파일은 `artifacts/bin/x64/Debug/ActionRPGClient.exe`에 생성됩니다.

클라이언트는 기본적으로 `127.0.0.1:7777`의 TownServer에 연결합니다. 서버를 먼저
실행한 뒤 클라이언트를 여러 번 실행하면 원격 플레이어의 Appear, 이동,
Disappear를 확인할 수 있습니다.
입장 시 맵의 좌표와 이동 영역은 서버에서 받지만 배경 이미지 파일은 클라이언트의
`Assets/Images/Towns`에 있어야 합니다. 서버 또는 클라이언트의 입장 패킷 형식을
변경했다면 양쪽 프로젝트를 함께 다시 빌드합니다.

## Town Map Editor

전체 사용법은 [`TOWN_MAP_EDITOR.md`](TOWN_MAP_EDITOR.md)를 참고합니다.

```powershell
artifacts/bin/x64/Debug/TownMapEditor.exe `
  ..\..\ActionRPGServer\ActionRPGServer\TownServer\Data\TownMap.json `
  .\Assets
```

- `Add at X/Y`: 입력한 월드 좌표에 이미지 추가
- `Add Right`: 선택 이미지의 오른쪽 끝에 간격 없이 이미지 추가
- `Add Bottom`: 선택 이미지의 아래쪽 끝에 간격 없이 이미지 추가
- `Add Top`: 선택 이미지의 위쪽 끝에 간격 없이 이미지 추가
- `Add Left`: 선택 이미지의 왼쪽 끝에 간격 없이 이미지 추가
- `O`: 기존 TownMap JSON 불러오기
- `V`: 이미지 선택. 드래그 또는 X/Y 입력으로 위치 수정
- `W`: 이동 가능 다각형 작성. 좌클릭으로 점을 추가하고 `Enter`로 완성
- `B`: 진입 금지 다각형 작성
- `P` 또는 우클릭: 플레이어 시작 위치 배치
- `R`: 드래그로 실제 표시 영역 지정. 영역 밖 배경은 클라이언트에서 잘림
- `Clear Mode Areas`: 현재 W/B 모드의 영역을 모두 제거
- 가운데 버튼 드래그 또는 `Space+좌클릭`: 캔버스 이동
- 마우스 휠: 커서 위치를 중심으로 확대·축소
- `F`: 전체 맵 맞춤, `S`: 현재 JSON 저장, `Shift+S`: 다른 이름으로 저장

추가한 이미지는 `Assets/Images/Towns`로 복사됩니다. 너비가 100인 이미지가
X=0에 있을 때 `Add Right`로 추가한 다음 이미지는 X=100에 배치됩니다.
두 번째 실행 인자를 생략하면 편집기는 프로젝트의 `Assets` 디렉터리를 자동으로 찾습니다.

맵을 수정한 후 TownServer를 다시 빌드·실행하면 서버가 새 영역을 로드합니다.
이미지를 새로 추가했다면 ActionRPGClient도 다시 빌드하여 해당 이미지를 실행 폴더의
`Assets`로 복사해야 합니다.
