# ActionRPGClient 개발 가이드

이 문서는 클라이언트의 코드 위치와 실행 흐름을 설명합니다. TownServer 패킷을 추가할 때는 [마을 패킷 개발 가이드](TOWN_NETWORK.md), 맵을 작성할 때는 [마을 맵 에디터 설명서](TOWN_MAP_EDITOR.md)를 함께 참고합니다.

## 1. 프로젝트 구성

| 위치 | 역할 |
| --- | --- |
| `ActionRPGClient/App/Application.*` | 창, 렌더러, 게임, TownClient의 수명과 메인 루프 |
| `ActionRPGClient/Platform/GameWindow.*` | Win32 창 메시지, 키 입력, 창 크기 변경 |
| `ActionRPGClient/Graphics/` | D3D11/DXGI 장치와 Direct2D 그리기 |
| `ActionRPGClient/Game/GameWorld.*` | 마을 상태, 로컬·원격 플레이어, 맵, 카메라, 투사체의 갱신과 출력 |
| `ActionRPGClient/Game/Player.*` | 로컬 캐릭터 이동, 애니메이션, 공격 |
| `ActionRPGClient/Game/GameplayMap.*` | 서버가 보낸 이동 가능·진입 금지 영역의 클라이언트 측 판정 |
| `ActionRPGClient/Game/MapBackground.*` | 서버가 보낸 이미지 배치 정보로 로컬 배경 이미지 출력 |
| `ActionRPGClient/Network/TownClient.*` | TownServer TCP 연결, 재연결, 비동기 송수신 |
| `ActionRPGClient/Network/TownProtocol.*` | 패킷 인코딩·디코딩과 자료형 |
| `ActionRPGClient/Resources/AssetCatalog.*` | 실행 파일 옆 `Assets/`의 상대 경로 해석 |
| `TownMapEditor/` | 맵 JSON과 배경 이미지 배치를 작성하는 별도 실행 파일 |
| `Assets/` | INI 데이터, 이미지, 오디오 파일 |

## 2. 실행 및 스레드 흐름

`Application`은 시작할 때 `TownClient`를 `127.0.0.1:7777`에 연결하고 `Game`을 생성합니다. 메인 루프는 창 메시지를 처리한 뒤 60Hz 고정 간격으로 게임 상태를 갱신하고 화면을 그립니다. 창 크기가 바뀌면 그래픽 장치와 게임 카메라의 크기도 갱신합니다.

```text
게임 스레드: Application::Run → Game::Update → GameWorld::Update → GameWorld::Render
네트워크 스레드: TownClient의 asio::io_context → TCP 읽기/쓰기 → 패킷 디코딩
연결 지점: TownClient::PushEvent → 이벤트 큐 → GameWorld::ProcessNetworkEvents
```

`TownClient`는 별도 네트워크 스레드에서 동작합니다. 소켓과 송신 큐는 Asio strand에서 순서대로 처리하며, 수신 패킷은 mutex로 보호되는 이벤트 큐에 넣습니다. `GameWorld`는 게임 스레드에서 `ConsumeEvents()`로 이벤트를 가져와 상태를 변경합니다. 네트워크 콜백에서 `Player`, `GameWorld`, 렌더러를 직접 수정하지 않습니다.

연결에 실패하거나 끊기면 `TownClient`가 재연결을 시도합니다. 접속 후에는 `EnterTownRequest`를 보내고, `EnterTownResponse`의 맵 정보로 플레이어 시작 위치와 이동 영역·배경 배치를 설정합니다.

## 3. 마을 이동과 다른 플레이어

- 클라이언트는 로컬 입력으로 이동을 예측하고, 서버가 보낸 권위 위치로 오차를 보정합니다.
- 입력 방향이 바뀌거나 멈추면 `MoveInput`을 보냅니다. 이동 중에는 입력 상태 유지를 위해 주기적으로 다시 보내며, 멈춘 상태에서는 반복 전송하지 않습니다.
- 현재 마을에서는 달리기를 비활성화하고 걷기 속도만 사용합니다.
- `PlayerAppear`로 원격 플레이어를 만들고 `PlayerDisappear`로 제거합니다. `PlayerMove`의 위치·속도로 이동 중 화면 위치를 예측하며, 정지 패킷을 받으면 최종 좌표에 맞춥니다.
- 마을의 섹터 분류와 Appear/Disappear 판정은 TownServer가 담당합니다. 기본 클라이언트 창 크기는 1280×720이지만 창을 더 크게 늘릴 수 있으므로, 화면 크기와 서버 가시 범위가 항상 같다고 가정하지 않습니다.

## 4. 에셋과 맵 적용

일반 게임 데이터와 캐릭터 이미지는 `Assets/Data/assets.ini`의 논리적 ID로 찾습니다. `AssetCatalog`는 실행 파일이 있는 폴더의 `Assets/`를 기준으로 상대 경로를 해석합니다. `ActionRPGClient` 빌드 후에는 프로젝트의 `Assets/` 전체가 실행 파일 옆으로 복사됩니다. 데이터 파일별 역할과 스프라이트 기준점은 [에셋 설명](Assets/README.md)을 참고합니다.

마을 맵 JSON은 클라이언트가 직접 읽지 않습니다. TownServer가 실행 파일 옆의 `Data/TownMap.json`을 로드하고, 입장 응답으로 맵의 표시 영역, 이미지 배치, 이동 영역과 시작 위치를 보냅니다. **배경 이미지 파일 자체는 전송하지 않으므로** JSON의 `images[].asset`에 대응하는 파일을 클라이언트 `Assets/Images/Towns/`에 두어야 합니다. 마을 배경은 `assets.ini` 등록 없이 JSON의 상대 경로로 찾습니다.

맵을 갱신할 때는 서버 프로젝트의 `TownServer/Data/TownMap.json`을 저장하고, 서버 실행 폴더의 `Data/TownMap.json`도 갱신한 뒤 TownServer를 재시작합니다. 새 이미지를 추가했다면 클라이언트를 빌드하거나 실행 폴더의 `Assets/`에도 이미지를 복사합니다. 맵 편집 절차는 [맵 에디터 설명서](TOWN_MAP_EDITOR.md)에 있습니다.

## 5. 기능을 추가할 때

1. 캐릭터의 개인 상태·행동이면 `Player`와 `GameWorld` 중 책임에 맞는 곳에 둡니다. 스킬·투사체 설정은 가능하면 기존 `Assets/Data/*.ini`와 해당 시스템을 사용합니다.
2. 모든 유저가 공유하거나 악용 가능성이 있는 규칙은 TownServer를 권위 주체로 둡니다. 클라이언트의 이동 판정은 반응성을 위한 예측이지 최종 판정이 아닙니다.
3. 새 네트워크 요청·응답은 서버와 클라이언트의 `TownProtocol` 필드 순서를 맞추고, `TownClient`에서 이벤트를 전달한 뒤 `GameWorld`에서 처리합니다. 세부 절차는 [마을 패킷 개발 가이드](TOWN_NETWORK.md)를 따릅니다.
4. 패킷 형식이 바뀌면 서버와 클라이언트를 함께 빌드하고 재실행합니다. 현재 프로토콜에는 버전 협상이 없습니다.

## 6. 빌드와 확인

Visual Studio에서 `ActionRPGClient.slnx`를 열고 `Debug | x64`로 `ActionRPGClient`와 필요한 경우 `TownMapEditor`를 빌드합니다. 명령줄은 클라이언트 프로젝트 루트에서 다음과 같습니다.

```powershell
msbuild .\ActionRPGClient\ActionRPGClient.vcxproj /p:Configuration=Debug /p:Platform=x64
msbuild .\TownMapEditor\TownMapEditor.vcxproj /p:Configuration=Debug /p:Platform=x64
```

결과는 `artifacts/bin/x64/Debug/`에 생성됩니다. TownServer를 먼저 실행한 뒤 클라이언트를 두 개 실행하면 접속, 마을 배경, 다른 플레이어의 등장·이동·퇴장을 확인할 수 있습니다. 두 저장소가 같은 상위 폴더에 있다면 서버 저장소의 `RunTownLocalTest.bat`으로 이 과정을 실행할 수도 있습니다.

배경이 보이지 않으면 서버 실행 폴더의 맵 JSON에 `images`가 있는지, 각 `asset` 경로의 파일이 클라이언트 실행 폴더 `Assets/`에 있는지 확인합니다. 접속이 실패하면 서버와 클라이언트를 같은 패킷 형식으로 빌드했는지 확인합니다.
