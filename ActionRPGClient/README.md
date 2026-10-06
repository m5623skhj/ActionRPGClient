# ActionRPGClient

[저장소 진입 안내](../README.md)에서 전체 경로와 문서를 찾을 수 있습니다.

Windows용 2D 액션 RPG 클라이언트입니다. Win32 메시지 루프, D3D11/DXGI 장치와 Direct2D 렌더링을 사용합니다. Google 로그인 뒤 Auth의 입장 티켓으로 TownServer에 TLS 접속하며, 던전에서는 MultiSocketRUDP로 GameRoomServer와 통신합니다.

이 문서는 2026-10-06의 클라이언트 `1d153cf`와 현재 데이터·서버 계약을 기준으로 작성했습니다. 코드 구현과 실제 서비스 동작 확인은 구분합니다.

## 문서 안내

| 문서 | 내용 |
| --- | --- |
| [개발 가이드](DEVELOPMENT.md) | 실행·스레드 흐름, 월드·UI 책임, 확장 위치 |
| [인증 및 타운 네트워크](TOWN_NETWORK.md) | 로그인 설정, TLS 입장, 패킷·이벤트·세션 수명 |
| [던전 전투](COMBAT_CLIENT.md) | 던전 입장·클리어, 입력, 실시간 상태 보간과 복구 |
| [에셋 안내](Assets/README.md) | 데이터 ID, 이미지·애니메이션, 원본과 실행 폴더 동기화 |

## 현재 구현

- Google Desktop OAuth/PKCE 로그인, 타운·캐릭터 종류·이름 선택, 로그아웃·계정 전환·타운 변경.
- 마을 로컬 이동 예측·서버 위치 보정, 섹터별 다른 플레이어 등장·이동·퇴장.
- 던전 선택, 인증·월드 수신, 방 이동, 몬스터·플레이어·탄환·HP·피격·사망 표시.
- 던전 원격 플레이어와 몬스터의 서버 시간 기반 보간, reliable JSON 복구.
- 서버 스킬 트리·레벨·SP 표시, 습득·강화 요청, 6개 단축키 슬롯과 쿨타임.
- 공개 파티 목록·상세·생성·가입 요청 승인·초대·탈퇴·추방 알림.
- 보스 클리어 후 마을 이동·재도전 선택 창. 파티에서는 파티장이 선택합니다.

인증 계정과 캐릭터의 영속 데이터 저장은 서버·DB의 책임입니다. 클라이언트의 캐릭터 종류 선택을 계정별 저장 캐릭터 목록 조회로 해석하지 않습니다. 현재 선택은 데이터 ID 1~3이고, 실제 저장·복원 범위는 [타운 네트워크 문서](TOWN_NETWORK.md)의 책임 구분을 참고합니다.

## 조작

| 입력 | 동작 |
| --- | --- |
| 방향키 | 지면 이동 |
| 같은 방향키 빠르게 두 번 | 던전 달리기. 마을은 걷기만 허용 |
| `C` | 점프 |
| `X` 누르기 | 기본 사격 한 발 예약. 길게 누르기만으로 연사하지 않음 |
| `V` | 마을 로컬 투척 표시. 던전 기본 행동 패킷으로 전송하지 않음 |
| `↑` 다음 `Z` | Character1 앞차기. 입력 간격 최대 0.35초, 지상·습득·쿨타임 조건 필요 |
| `A/S/D/F/G/H` | 등록한 6개 스킬 사용 |
| `Esc` | 전체 메뉴 열기·닫기, 하위 UI에서 메뉴로 돌아가기 |
| 던전 목록 `↑/↓`, `Enter`, `C` | 선택, 입장 요청, 취소 |
| 클리어 창 방향키·`Enter` 또는 클릭 | 마을 이동·재도전 선택 |

메뉴의 스킬 창에서 습득한 아이콘을 슬롯으로 드래그해 등록합니다. 휠은 세로, Shift+휠은 가로 스크롤입니다. 던전 입장 처리 중부터 파티 UI와 스킬 습득·강화를 제한하며, 던전 스킬 조회·단축키 등록은 허용합니다. 메뉴·선택창·초대창·클리어창·피격 상태는 게임 입력을 차단합니다.

앞차기 정의는 [PlayerSkills.json](Assets/Data/PlayerSkills.json)에 있습니다. 과거 `skills.ini`의 `→→Z/←←Z` 예시는 현재 플레이어 스킬 실행 경로가 아닙니다. 서버의 현재 초기값은 레벨 1·SP 0, 앞차기 습득 비용은 20 SP입니다. 캐릭터 레벨/SP 갱신은 서버가 결정하며 클라이언트에 임의 지급 기능은 없습니다.

## 빌드와 실행

필요한 환경은 Windows x64, C++20, MSVC v143와 Windows SDK, `.slnx`를 지원하는 Visual Studio/MSBuild입니다. 저장소의 `External/MultiSocketRUDP`와 그 하위 의존성을 준비하고, 루트 [vcpkg.json](../vcpkg.json)의 Asio·OpenSSL·nlohmann-json을 사용합니다. 클라이언트 프로젝트에서 manifest를 활성화합니다.

아래 명령의 작업 위치는 **이 README가 있는 폴더**입니다. 게임 실행 인자는 현재 해석하지 않습니다.

```powershell
Set-Location C:/Users/KimHyeongJin/source/repos/ActionRPGClient/ActionRPGClient
msbuild ./ActionRPGClient/ActionRPGClient.vcxproj /p:Configuration=Debug /p:Platform=x64
./artifacts/bin/x64/Debug/ActionRPGClient.exe
```

Visual Studio에서는 이 폴더의 [ActionRPGClient.slnx](ActionRPGClient.slnx)를 열고 `Debug | x64` 또는 `Release | x64`를 선택합니다. 클라이언트 실행 파일은 `ActionRPGClient/artifacts/bin/x64/<Configuration>/ActionRPGClient.exe`에 생성됩니다. RUDP·Logger 라이브러리는 저장소 루트의 `artifacts` 경로를 사용하므로 두 출력 위치를 혼동하지 않습니다.

실행 파일 옆에 `Assets/`, `ClientOptionFile/CoreOption.txt`, OpenSSL 런타임 DLL이 필요합니다. 빌드 대상의 `CopyRuntimeAssets`·`CopyClientOptions`가 데이터를 복사합니다. 로그인 전에 [AuthClient.json](Assets/Data/AuthClient.json)에 실제 Auth 주소·Google Desktop ID·타운 목록·타운 CA를 설정하고 서버의 인증/TLS 설정과 맞춰야 합니다. 기본 파일은 미설정 상태이며 고정 localhost에 자동 접속하지 않습니다. 서버 기동·DB 준비 절차는 [AuthServer 문서](../../ActionRPGServer/ActionRPGServer/AuthServer/DEVELOPMENT.md)를 참고합니다.

## 에셋 반영

원본은 이 폴더의 `Assets/`입니다. 빌드 없이 이미 빌드한 실행 폴더에 반영하려면 다음을 사용합니다.

```powershell
./SyncClientAssets.bat
./SyncClientAssets.bat Release
```

생략 시 Debug x64, Release 지정 시 Release x64에 반영합니다. 이 도구는 원본에서 삭제한 파일도 실행 폴더에서 삭제하는 미러 동기화입니다. 실행 폴더만 수정하지 말고 원본을 관리하며, 캐시된 데이터·이미지와 기동 시 설정을 다시 읽도록 클라이언트를 재시작합니다. C++나 wire 계약 변경은 양쪽 소스 빌드·배포가 필요합니다.

## 편집 도구

각 도구의 사용법·입출력 형식은 해당 문서에서 관리합니다.

- [마을 맵 에디터](TOWN_MAP_EDITOR.md): 마을 배경·이동 영역·진입 위치.
- [던전 에디터](DungeonEditor/README.md): 방·워프·미니맵과 던전 출력.
- [몬스터 에디터](MonsterEditor/README.md): AI 그래프·스킬·동작 정의.
- [캐릭터 에디터](CharacterEditor/README.md): 프레임·pivot·피격 영역.
- [스킬 에디터](SkillEditor/README.md): 공유 스킬 계약·표시 데이터.

서버가 맵·몬스터·전투를 실행하고 클라이언트가 표시하는 연동은 구현되어 있습니다. 편집기의 출력 성공만으로 서버 데이터 설치와 실제 플레이 검증이 완료되는 것은 아닙니다.

## 확인 범위와 한계

2026-10-06 이전 작업에서 Debug x64 컴파일·링크와 일반 재빌드 성공을 확인했습니다. 이번 문서 작업은 코드·설정·패킷·경로의 정적 대조만 수행했으며 빌드나 프로그램 실행을 추가로 하지 않았습니다. Release, 실제 Google/Auth/Town 왕복, 다중 클라이언트 파티·던전·종료 흐름은 별도 실행 검증이 필요합니다.

토큰 영구 저장·자동 갱신·기동 시 자동 로그인·타운 자동 재접속은 구현하지 않았습니다. 미니맵 파일은 편집기 출력과 구분되며 현재 게임 미니맵 HUD는 연결되지 않았습니다. 화면 조작 안내의 과거 커맨드 문자열도 실제 PlayerSkills 입력과 다르므로 위 조작표를 기준으로 확인합니다. 현재 서버 계약의 드랍·인벤토리·보상 지급·부활·전멸 처리 범위는 [서버 전투 문서](../../ActionRPGServer/ActionRPGServer/GameRoomServer/COMBAT_PROTOCOL.md)에서 확인합니다. 표시 보간은 서버의 피해·충돌 판정을 바꾸지 않습니다.
