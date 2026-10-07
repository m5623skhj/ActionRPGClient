# ActionRPGClient

[저장소 진입 안내](../README.md)에서 전체 경로와 문서를 찾을 수 있습니다.

Windows용 2D 액션 RPG 클라이언트입니다. Win32 메시지 루프, D3D11/DXGI 장치와 Direct2D 렌더링을 사용합니다. Google 로그인 뒤 Auth의 입장 티켓으로 TownServer에 TLS 접속하며, 던전에서는 MultiSocketRUDP로 GameRoomServer와 통신합니다.

이 문서는 2026-10-07의 클라이언트 소스와 현재 데이터·서버 계약을 기준으로 작성했습니다. 코드 구현과 실제 서비스 동작 확인은 구분합니다. 현재 던전 전투 계약은 버전 3이며 공격자 역경직과 일반 피격 경직의 히트 리커버리를 포함합니다. 상세 상태·시간 규칙은 [COMBAT_CLIENT.md](COMBAT_CLIENT.md)를 참고하세요.

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
| X 누르기 | 기본 사격 한 발 예약. 던전 지상 달리기 중에는 슬라이딩으로 한 번 소비 |
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
Set-Location C:/work/ActionRPGClient/ActionRPGClient # 실제 체크아웃 경로로 변경
msbuild ./ActionRPGClient/ActionRPGClient.vcxproj /t:Rebuild /p:Configuration=Debug /p:Platform=x64 /p:VcpkgEnableManifest=true
```

Visual Studio에서는 이 폴더의 [ActionRPGClient.slnx](ActionRPGClient.slnx)를 열고 `Debug | x64` 또는 `Release | x64`를 선택합니다. 클라이언트 실행 파일은 `ActionRPGClient/artifacts/bin/x64/<Configuration>/ActionRPGClient.exe`에 생성됩니다. RUDP·Logger 라이브러리는 저장소 루트의 `artifacts` 경로를 사용하므로 두 출력 위치를 혼동하지 않습니다.

실행 파일 옆에 `Assets/`, `ClientOptionFile/CoreOption.txt`, OpenSSL 런타임 DLL이 필요합니다. 빌드 대상의 `CopyRuntimeAssets`·`CopyClientOptions`가 데이터를 복사합니다. 클라이언트는 실행 파일 옆 `Assets/Data/AuthClient.json`에서 실제 Auth 주소·Google Desktop ID·타운 목록·타운 CA 경로를 읽습니다. 원본 [AuthClient.json](Assets/Data/AuthClient.json)은 빈 템플릿으로 보존합니다. 서버 기동·DB 준비 절차는 [AuthServer 문서](../../ActionRPGServer/ActionRPGServer/AuthServer/DEVELOPMENT.md)를 참고합니다.

로컬 연동은 서버 저장소의 [RunLocalTest.bat](../../ActionRPGServer/RunLocalTest.bat)를 사용합니다. 최초 설정에서 실제 Google Desktop ID·DB 접속 정보 등을 입력받습니다. 공개 연결값은 `%LOCALAPPDATA%/ActionRPG/LocalTest/settings.json`에, Google Desktop secret·DB 접속 정보·타운 등록키는 같은 폴더의 `credentials.dpapi`에 보호해 보관합니다. 매 실행 클라이언트를 켜기 전에 공개 client 항목만 실행용 `AuthClient.json`에 공급합니다. 이 JSON에는 Auth 주소·Google client ID·타운 CA 경로·타운 목록·이름 초기값·캐릭터 종류만 포함하며 DB 정보·타운 등록키·개인 키는 포함하지 않습니다. Google Desktop secret은 실행용 JSON에 포함하지 않습니다. 실행기는 클라이언트 프로세스에만 ID·secret 환경변수를 전달하고 클라이언트는 읽은 뒤 제거합니다. ID 일치·값 검증 후 메모리에서만 사용하며, Google 토큰 교환에만 선택적으로 추가합니다. 상세 규칙은 [타운 네트워크 문서](TOWN_NETWORK.md)를 참고합니다. 타운 CA는 사용자 설정의 절대 경로를 사용합니다. 재빌드가 빈 템플릿을 다시 복사한 뒤에는 실행기로 설정을 재공급합니다.

실행기가 여는 두 클라이언트는 서로 다른 실제 Google 계정으로 로그인해야 합니다. 브라우저가 같은 계정을 선택하면 타운 선택 화면의 계정 전환을 사용합니다. 같은 계정의 새 로그인은 이전 토큰을 무효화하므로 먼저 로그인했던 클라이언트의 재로그인이 필요할 수 있습니다. 이름이나 캐릭터 종류를 다르게 선택해도 로그인 계정은 달라지지 않습니다.

## 새 PC에서 준비할 순서

전체 MySQL·Auth·Town·Room·클라이언트 설치는 [서버 통합 설치·문제 해결 가이드](../../ActionRPGServer/docs/workflows/LOCAL_DEVELOPMENT_SETUP.md)를 먼저 따릅니다. 아래는 새 PC에서 서버와 클라이언트를 함께 실행하는 로컬 연동 기준이며, 원격 접속·기존 DB 데이터 이관은 별도입니다. 명령은 설치·빌드를 수행하는 사용자를 위한 절차이며, 이번 문서 작업에서 실행하지 않았습니다.

1. Git과 Visual Studio의 C++ 데스크톱 개발 구성 요소, 클라이언트의 MSVC v143·Windows SDK를 설치합니다. 로컬 서버 빌드에는 MSVC v145 도구 집합도 필요합니다. `.slnx`를 열 수 있는 Visual Studio/MSBuild를 사용합니다. vcpkg 설치 후 해당 설치 폴더에서 `./vcpkg integrate install`로 사용자 MSBuild 연동을 준비합니다. [공식 MSBuild 연동 안내](https://learn.microsoft.com/en-us/vcpkg/users/buildsystems/msbuild-integration)를 참고합니다.
2. 서버 저장소와 클라이언트 저장소를 같은 부모 폴더의 `ActionRPGServer/`, `ActionRPGClient/`로 준비합니다. 클라이언트는 아래처럼 하위 서브모듈까지 받고 저장소가 기록한 커밋을 사용합니다. 임의로 서브모듈의 최신 원격 커밋으로 교체하지 않습니다.

   ```powershell
   git clone --recurse-submodules https://github.com/m5623skhj/ActionRPGClient.git
   Set-Location ./ActionRPGClient
   git submodule update --init --recursive
   ```

3. [서버 통합 설치 가이드](../../ActionRPGServer/docs/workflows/LOCAL_DEVELOPMENT_SETUP.md)에 따라 새 PC의 서버·DB·ODBC·인증서 준비를 마칩니다. 이전 PC의 DPAPI 파일과 인증서 개인 키를 복사하지 않습니다. `%LOCALAPPDATA%/ActionRPG/LocalTest`의 사용자 프로필·인증서·신뢰 등록은 새 PC와 Windows 사용자 기준으로 새로 준비합니다.
4. `ActionRPGClient/ActionRPGClient.slnx`를 열고 **Debug | x64**로 클라이언트를 다시 빌드합니다. 위 빌드 명령은 이 README 폴더 기준입니다. 서버와 타운 패킷·던전 전투 계약 및 데이터 버전이 같아야 합니다. 현재 전투 계약은 3입니다.
5. 빌드 성공 후 서버 저장소의 `RunLocalTest.bat`을 실행합니다. 최초 설정에서 실제 Google Desktop ID와 **같은 등록 항목의 secret**을 입력합니다. secret은 숨김 입력과 사용자별 DPAPI 저장을 사용하며 공개 JSON이나 소스에 넣지 않습니다. 실행기가 공개 설정을 실제 Debug EXE 옆에 공급하고 secret은 클라이언트 프로세스 환경으로만 전달합니다. 클라이언트만 재빌드한 이번 인증 수정 때문에 서버 C++를 다시 빌드할 필요는 없지만, 새 PC의 최초 서버 빌드는 별도로 필요합니다.
6. 브라우저 인증 후 게임 화면의 결과를 확인하고 타운을 선택해 입장합니다. 브라우저 콜백 수신 문구는 로그인 성공 확정이 아닙니다. 두 클라이언트는 서로 다른 Google 계정을 사용합니다. 재빌드·에셋 동기화 후에는 실행기로 설정을 다시 공급합니다.

현재 실행기는 로컬 localhost 연동을 준비합니다. 원격 서버에 접속하는 배포에서는 운영자가 제공하는 실제 Auth/Town 주소·동일 Google ID·인증정보 공급 절차·신뢰 CA가 필요하며, 로컬 프로필의 주소와 인증서 경로를 그대로 사용하지 않습니다. 상세 신뢰 조건과 실제 오류 사례는 [인증 설정 및 문제 해결](TOWN_NETWORK.md#새-pc-인증-준비와-문제-해결)을 참고합니다.

## 에셋 반영

원본은 이 폴더의 `Assets/`입니다. 빌드 없이 이미 빌드한 실행 폴더에 반영하려면 다음을 사용합니다.

```powershell
./SyncClientAssets.bat
./SyncClientAssets.bat Release
```

생략 시 Debug x64, Release 지정 시 Release x64에 반영합니다. 이 도구는 원본에서 삭제한 파일도 실행 폴더에서 삭제하는 미러 동기화입니다. 실행 폴더만 수정하지 말고 원본을 관리하며, 캐시된 데이터·이미지와 기동 시 설정을 다시 읽도록 클라이언트를 재시작합니다. C++나 wire 계약 변경은 양쪽 소스 빌드·배포가 필요합니다. 로그인 설정은 빈 원본 템플릿을 유지하는 예외이며, 에셋 동기화 후에는 RunLocalTest로 실행용 설정을 다시 공급합니다.

## 편집 도구

각 도구의 사용법·입출력 형식은 해당 문서에서 관리합니다.

- [마을 맵 에디터](TOWN_MAP_EDITOR.md): 마을 배경·이동 영역·진입 위치.
- [던전 에디터](DungeonEditor/README.md): 방·워프·미니맵과 던전 출력.
- [몬스터 에디터](MonsterEditor/README.md): AI 그래프·스킬·동작 정의.
- [캐릭터 에디터](CharacterEditor/README.md): 프레임·pivot·피격 영역.
- [스킬 에디터](SkillEditor/README.md): 공유 스킬 계약·표시 데이터.

서버가 맵·몬스터·전투를 실행하고 클라이언트가 표시하는 연동은 구현되어 있습니다. 편집기의 출력 성공만으로 서버 데이터 설치와 실제 플레이 검증이 완료되는 것은 아닙니다.

## 확인 범위와 한계

2026-10-07 사용자가 Desktop 자격 증명 전달 수정 후 실제 Google 로그인과 정상 입장을 확인했습니다. 이는 사용자 실행 확인이며 에이전트가 이번 작업에서 빌드·기능 테스트·게임/서버 실행·DB 접속을 수행한 결과가 아닙니다. 에이전트는 소스·설정 계약·문서 경로를 정적으로 대조했습니다. 새 PC의 최초 설치, Release, 다중 클라이언트 파티·던전·종료의 전체 회귀 검증은 별도로 필요합니다.

토큰 영구 저장·자동 갱신·기동 시 자동 로그인·타운 자동 재접속은 구현하지 않았습니다. 미니맵 파일은 편집기 출력과 구분되며 현재 게임 미니맵 HUD는 연결되지 않았습니다. 화면 조작 안내의 과거 커맨드 문자열도 실제 PlayerSkills 입력과 다르므로 위 조작표를 기준으로 확인합니다. 현재 서버 계약의 드랍·인벤토리·보상 지급·부활·전멸 처리 범위는 [서버 전투 문서](../../ActionRPGServer/ActionRPGServer/GameRoomServer/COMBAT_PROTOCOL.md)에서 확인합니다. 표시 보간은 서버의 피해·충돌 판정을 바꾸지 않습니다.
