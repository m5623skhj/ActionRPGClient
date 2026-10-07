# ActionRPGClient

Windows용 2D 액션 RPG 클라이언트와 콘텐츠 편집 도구를 관리하는 저장소입니다. 게임은 Win32·D3D11/DXGI·Direct2D를 사용하며, Google/Auth 로그인 후 TownServer에 TLS로 접속하고 던전은 MultiSocketRUDP로 GameRoomServer와 통신합니다.

실제 게임 프로젝트·에셋·솔루션은 저장소 안의 **[ActionRPGClient/](ActionRPGClient/README.md)**에 있습니다. 상세 기능과 조작은 해당 런타임 README에서 관리합니다. 문서 기준은 2026-10-07의 현재 소스입니다. 다른 PC 설치는 [새 PC 준비 순서](ActionRPGClient/README.md#새-pc에서-준비할-순서)를 따릅니다.

## 먼저 읽을 문서

| 문서 | 내용 |
| --- | --- |
| [런타임 README](ActionRPGClient/README.md) | 현재 기능, 조작, 빌드·실행 전제, 에셋 반영 |
| [개발 가이드](ActionRPGClient/DEVELOPMENT.md) | 로그인에서 월드까지 실행·스레드 흐름, UI·성장·확장 위치 |
| [인증 및 타운 네트워크](ActionRPGClient/TOWN_NETWORK.md) | AuthClient 설정, Google 로그인·TLS 입장, 파티·스킬 패킷과 세션 정리 |
| [던전 전투](ActionRPGClient/COMBAT_CLIENT.md) | RUDP 입장, 로컬 입력·원격 보간, 손실 복구, 클리어·복귀·재도전 |
| [에셋 안내](ActionRPGClient/Assets/README.md) | 데이터 ID·애니메이션·서버 공유 계약·원본과 실행 폴더 |

## 런타임 개요

현재 구현은 마을 이동 예측·다른 플레이어 표시, 던전 방 이동·몬스터/탄환/HP·피격·사망, 스킬 트리·SP·습득·단축키·쿨타임, 공개 파티·상세·가입 승인·추방 알림과 클리어 선택 창입니다. 계정 인증·캐릭터 영속 데이터는 서버/DB의 책임이며, 클라이언트는 서버 상태를 받아 표시하고 입력을 보냅니다.

방향키는 이동, C는 점프, X는 기본 사격, Esc는 전체 메뉴입니다. 던전은 같은 방향키 두 번으로 달리며 마을은 걷기만 허용합니다. A/S/D/F/G/H는 등록한 스킬을 사용합니다. 현재 Character1 앞차기는 **Up 다음 Z**이고 지상·습득·쿨타임 조건이 필요합니다. V는 마을 로컬 투척 표시입니다. 입력 제한·학습 조건과 선택창 조작은 [전체 조작 안내](ActionRPGClient/README.md#조작)를 확인합니다.

## 빌드·실행 전제

Windows x64, C++20·MSVC v143·Windows SDK와 `.slnx`를 지원하는 Visual Studio/MSBuild가 필요합니다. [MultiSocketRUDP 서브모듈](.gitmodules)과 하위 의존성을 준비하고 루트 [vcpkg.json](vcpkg.json)의 Asio·OpenSSL·nlohmann-json을 사용합니다. Visual Studio에서는 [ActionRPGClient.slnx](ActionRPGClient/ActionRPGClient.slnx)를 열어 Debug 또는 Release x64를 선택합니다.

아래 명령은 **이 README가 있는 저장소 루트**를 기준으로 합니다. Visual Studio Developer PowerShell에서 사용하며, 게임 명령줄 인자는 현재 해석하지 않습니다.

```powershell
msbuild ./ActionRPGClient/ActionRPGClient/ActionRPGClient.vcxproj /t:Rebuild /p:Configuration=Debug /p:Platform=x64 /p:VcpkgEnableManifest=true
```

실행 전에 다음을 준비합니다.

1. 실제 Auth/Town/GameRoom 서버와 서버 측 인증·DB 설정. 서버 절차는 [AuthServer 문서](../ActionRPGServer/ActionRPGServer/AuthServer/DEVELOPMENT.md)를 참고합니다.
2. [원본 AuthClient.json](ActionRPGClient/Assets/Data/AuthClient.json)은 빈 템플릿으로 보존합니다. 빌드 후 서버의 RunLocalTest.bat으로 실제 EXE 옆 공개 설정을 공급합니다. Google Desktop secret은 숨김 입력·DPAPI 저장·클라이언트 프로세스 환경 주입으로 전달하며 소스/실행 JSON에 기록하지 않습니다.
3. 실행 파일 옆 Assets·ClientOptionFile/CoreOption.txt와 OpenSSL 런타임 DLL. 빌드가 데이터를 복사하며 게임은 실행 파일 위치를 기준으로 읽습니다.

설정·신뢰 CA·실패/취소·로그아웃/타운 변경의 상세 조건은 [타운 네트워크 문서](ActionRPGClient/TOWN_NETWORK.md)에 있습니다. 소스 에셋을 빌드 없이 반영하는 [SyncClientAssets.bat](ActionRPGClient/SyncClientAssets.bat)은 Debug/Release 실행 폴더를 미러 동기화하므로 삭제도 반영합니다. 적용 후 재시작합니다.

## 저장소 경로

| 위치 | 역할 |
| --- | --- |
| `ActionRPGClient/ActionRPGClient/` | 게임 C++ 프로젝트·소스·RUDP 코어 옵션 |
| `ActionRPGClient/Assets/` | 클라이언트 에셋 원본 |
| `ActionRPGClient/artifacts/bin/x64/<Configuration>/` | 게임·편집기 실행 파일과 런타임 복사본 |
| `External/MultiSocketRUDP/` | RUDP 의존성 서브모듈 |
| `artifacts/` | RUDP·Logger 라이브러리 등의 출력. 게임 실행 파일 출력과 구분 |
| `vcpkg.json`, `vcpkg_installed/` | 의존성 선언과 로컬 설치 결과 |

## 콘텐츠 편집 도구

도구별 사용법과 파일 형식은 각각의 문서에서 관리합니다.

- [마을 맵 에디터](ActionRPGClient/TOWN_MAP_EDITOR.md)
- [던전 에디터](ActionRPGClient/DungeonEditor/README.md)
- [몬스터 에디터](ActionRPGClient/MonsterEditor/README.md)
- [캐릭터 에디터](ActionRPGClient/CharacterEditor/README.md)
- [스킬 에디터](ActionRPGClient/SkillEditor/README.md)

편집기 출력, 서버 데이터 설치, 클라이언트 에셋 반영과 실제 플레이 검증은 각각 확인해야 합니다. 미니맵 파일 출력과 현재 게임의 미니맵 HUD 구현도 구분합니다.

## 확인 범위

2026-10-07 사용자가 Google 로그인과 정상 입장을 확인했습니다. 이번 문서 갱신은 기존 런타임 문서·프로젝트/데이터 경로와의 정적 대조이며 에이전트는 빌드·게임/서버 실행·DB 접속을 하지 않았습니다. 새 PC 최초 설치와 다중 클라이언트·던전 전체 검증은 별개이며 구현/미구현/미검증 범위는 각 상세 문서에 기록합니다.
