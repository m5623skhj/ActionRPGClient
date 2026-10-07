# 인증 및 타운 네트워크

2026-10-06 클라이언트 `1d153cf` 기준입니다. 이 문서는 클라이언트의 인증·타운 TLS·콘텐츠 이벤트 연결을 설명합니다. 서버 인증·DB 설정은 [AuthServer](../../ActionRPGServer/ActionRPGServer/AuthServer/DEVELOPMENT.md), 타운 규칙은 [TownServer](../../ActionRPGServer/ActionRPGServer/TownServer/DEVELOPMENT.md), 마이그레이션은 [DB 작업 문서](../../ActionRPGServer/docs/workflows/DATABASE_MIGRATIONS.md)에서 관리합니다.

## 로그인에서 타운 입장까지

```text
로그인 화면
 -> Auth POST /v1/challenges
 -> 시스템 브라우저 Google Desktop OAuth
 -> loopback 콜백의 code -> Google ID token
 -> Auth POST /v1/login -> gameToken
 -> 타운·캐릭터 종류·이름 선택
 -> Auth POST /v1/tickets -> ready=true인 단기 ticket
 -> Town TLS
 -> AdmissionTicketRequest(36)
 -> AdmissionResult(37, result=0)
 -> EnterTownRequest(1)
 -> EnterTownResponse(2)
 -> Playing -> Game 생성
```

Google Authorization Code/PKCE(S256), 매 시도 새 state, Auth challenge의 nonce를 사용합니다. 콜백은 `127.0.0.1` 동적 포트의 `/oauth2/callback`입니다. Auth가 ID 토큰을 검증하고 계정을 결정합니다. Google access token을 게임 인증에 사용하지 않습니다.

로그인과 게임 이름·종류 선택은 별개입니다. 현재 UI는 캐릭터 **종류 ID 1~3**과 UTF-8 1~32바이트 이름을 보내며, 저장 캐릭터 목록을 조회하는 API는 사용하지 않습니다. 입장 응답의 playerId·characterId가 게임 세션 기준입니다. 캐릭터 선택·이름은 인증 권한의 증거가 아닙니다.

서버 선택은 방향키·클릭, 페이지 이동은 PageUp/PageDown·휠, 캐릭터 종류는 좌우 선택, 이름은 텍스트 입력을 사용합니다. Enter로 입장합니다. Google/티켓/타운 입장 대기 중 Esc는 현재 시도를 취소하고, 서버 선택의 Esc는 로그아웃합니다.

## 연결 설정과 신뢰

기동 시 실행 파일 옆 `Assets/Data/AuthClient.json`을 읽습니다. 원본 [AuthClient.json](Assets/Data/AuthClient.json)은 빈 템플릿으로 보존합니다. 로컬 실행기 [RunLocalTest.bat](../../ActionRPGServer/RunLocalTest.bat)가 `%LOCALAPPDATA%/ActionRPG/LocalTest/settings.json`의 공개 client 항목을 매 실행 실제 EXE 옆 설정에 공급합니다. UTF-8 JSON의 최대 크기는 32KiB이며 최초 Google Desktop ID는 실제 등록값을 입력합니다. `playerName`은 빈 값으로 두어 프로세스별 이름을 사용하고 `characterId`는 1~3 범위입니다. 별도 override 기능이나 게임 명령줄 인증 인자는 사용하지 않습니다.

재빌드의 CopyRuntimeAssets와 에셋 미러 동기화는 빈 원본 설정을 다시 복사할 수 있으므로, 이후 실행기로 사용자 설정을 재공급합니다. 사용자 설정의 타운 CA 절대 경로를 그대로 사용합니다. 실행용 JSON에는 아래 공개 필드만 포함하며, DB 접속 정보·타운 등록키·인증서 개인 키는 공급하지 않습니다. 로컬 Auth HTTPS의 인증서 신뢰 준비는 실행기에서 CurrentUser 범위로 담당하고, 타운 TLS는 아래 townCaFile을 사용합니다. 변경된 설정은 클라이언트 재시작 시 읽습니다.

| 필드 | 의미·조건 |
| --- | --- |
| `authUrl` | HTTPS origin. 경로·쿼리·사용자 정보 없이 실제 Auth 호스트·포트 |
| `googleClientId` | Google Desktop app ID. Auth의 `ACTIONRPG_GOOGLE_CLIENT_ID`와 일치 |
| `townCaFile` | 타운 신뢰 CA PEM. 절대 경로 또는 Assets 기준 상대 경로 |
| `playerName` | 이름 초기값. 빈 값은 `Player-<PID>`; 입장 시 UTF-8 1~32바이트 |
| `characterId` | 기본 캐릭터 종류 1~3 |
| `servers[]` | 타운 목록. 각 항목에 `serverId/name/hostname/port` |
| `serverId` | Auth에 등록한 ID와 타운의 `ACTIONRPG_TOWN_ID` |
| `hostname/port` | 타운 인증서 SAN과 일치하는 DNS/IP 및 TLS 포트 1~65535 |

Auth·Google HTTPS는 WinHTTP의 Windows 인증서 저장소로 검증합니다. Town TLS는 Asio/OpenSSL과 `townCaFile`로 체인·호스트명을 검증하며 TLS 1.2 이상을 사용합니다. 타운 CA 파일 설정은 Windows의 Auth HTTPS 신뢰를 변경하지 않습니다. 인증서 검사 무시·평문 fallback은 구현하지 않았습니다. 서버 개인 키·타운 비밀·DB 접속 문자열은 클라이언트 설정에 넣지 않습니다.

토큰과 티켓은 메모리에만 두며 설정·로그·URL에 기록하지 않습니다. 게임 토큰은 응답 수명 최대 8시간, 티켓은 최대 30초, Google challenge는 최대 300초입니다. 요청 시작 시각을 기준으로 만료를 보수적으로 판단합니다. 토큰 영구 저장·자동 갱신·재기동 자동 로그인은 없습니다.

실행기가 여는 두 클라이언트는 서로 다른 실제 Google 계정으로 수동 로그인합니다. 브라우저가 같은 계정을 자동 선택하면 타운 선택 화면의 계정 전환을 사용해 다음 로그인에서 계정을 선택합니다. 같은 계정의 새 로그인은 기존 토큰을 무효화하므로 기존 클라이언트의 재로그인이 필요할 수 있습니다. playerName·characterId를 바꾸는 것은 계정 전환이 아닙니다.

## 취소·실패·세션 전환

HTTPS 전체 요청 한도는 15초이고 타운 TLS·승인·EnterTown은 티켓 잔여 시간과 15초 중 작은 한도를 적용합니다. 브라우저 대기는 challenge 만료까지입니다. UI 취소는 대기 화면을 즉시 빠져나오고 늦게 완료된 결과는 시도 ID로 거릅니다. 불확실한 HTTP 결과를 자동 재시도하지 않습니다.

`ready=false`는 기존 접속 종료 대기 상태입니다. 받은 티켓을 타운에 보내지 않으며 사용자가 다시 입장할 때 새 티켓을 발급합니다. 503은 준비 중, 403은 거절로 안내합니다. 타운 연결 종료만으로 중복 로그인·정지 등 상세 이유를 추측하지 않습니다. 기존 TCP 자동 연결·자동 재접속 경로는 제거되었습니다.

| 메뉴 | 처리 |
| --- | --- |
| 로그아웃 | Town/Dungeon 종료 요청·Game 폐기·Auth logout·로컬 토큰 제거 |
| 계정 전환 | 같은 정리 후 다음 Google 로그인에 계정 선택 요청 |
| 타운 변경 | Game/연결을 정리하고 유효한 gameToken만 유지. 목적 타운의 새 티켓 발급 |
| 게임 종료/창 닫기 | Application 소멸 과정에서 Auth worker·Town I/O·RUDP worker 정리 |

서버 logout 실패·불확실한 결과는 화면에 표시하며 로컬 로그아웃을 되돌리지 않습니다. 던전 종료는 `RequestStop` 후 매 tick `IsStopComplete`를 확인하여 UI 스레드의 blocking join을 피합니다. 프로세스 최종 정리는 별도입니다.

Auth 결과·타운 송신·수신 이벤트에는 시도 ID가 적용됩니다. 새 시도에서 이전 이벤트를 비우고, 타운 stream·읽기/쓰기 버퍼는 연결별 shared_ptr 수명을 유지합니다. WinHTTP callback 상태는 HANDLE_CLOSING까지 유지합니다. 새 Game에 이전 계정의 이벤트를 적용하지 않습니다.

## 서버·DB와 클라이언트의 책임

AuthServer/DB는 Google 계정 식별과 로그인 상태를 관리하고, TownServer는 접속 중 캐릭터·스킬 진행값을 결정합니다. 클라이언트에는 DB 호출·캐릭터 저장/복원 API가 없습니다. 로컬 파일은 스킬 **단축키 배치**만 저장하며 학습 권한·레벨·SP의 원본이 아닙니다.

2026-10-06 대조한 `TownInstance::EnterOnStrand`는 `progressionPolicy.Create()`로 입장 진행값을 만듭니다. 따라서 이 시점의 코드만으로 타운 변경·재로그인 시 캐릭터 진행값의 DB 복원을 보장할 수 없습니다. 영속 저장/이관의 추가 구현과 배포 상태는 서버·DB 담당 문서를 기준으로 확인해야 합니다. 클라이언트의 타운 변경 UI 구현을 DB 저장 구현으로 해석하지 않습니다.

## 패킷 원본과 framing

타운 패킷 ID·필드 선언의 원본은 서버 [Tool/TownPacketDefine.yml](../../ActionRPGServer/Tool/TownPacketDefine.yml)입니다. 양쪽 `TownPacket.generated.h`를 생성하고, 실제 바이트 인코딩/디코딩은 클라이언트 `TownProtocol.*`와 서버 `Protocol.*`에서 구현합니다. 생성 파일은 직접 수정하지 않습니다.

```text
[uint32 bodySize, big-endian]
[uint16 packetType, big-endian][payload]
문자열: [uint16 UTF-8 byteLength][bytes]
```

구조체 메모리를 그대로 보내지 않습니다. body와 송신 대기열은 각각 최대 1MiB, 수신 이벤트 큐는 최대 4096개입니다. 한 문자열은 uint16 길이 범위이므로 body 한도와 혼동하지 않습니다. malformed packet이나 큐 초과는 연결 실패로 처리합니다. 정상 콘텐츠 거절은 결과 코드로 처리합니다. 타운 framing에는 버전 협상이 없으므로 서버·클라이언트를 같은 계약으로 배포해야 합니다.

입장 티켓 패킷 36의 body는 ID 2 + 문자열 길이 2 + 64바이트 ticket = 68바이트, 패킷 37은 ID 2 + result 1 = 3바이트입니다.

| ID | 현재 패킷 |
| --- | --- |
| 1~6 | EnterTownRequest/Response, MoveInput, PlayerAppear/Move/Disappear |
| 7~11 | ConfirmDungeonJoin, EnterDungeonRequest/Response, MapChanged, DungeonSelectionOpen |
| 12~18 | PartyInviteRequest/Answer, PartyLeaveRequest, PartyKickRequest, PartyInvitation, PartySnapshot, PartyOperationResult |
| 19~24 | PartySettingsRequest, PartyDirectoryPageRequest/Unsubscribe/Page/Changed, PartyCreateRequest |
| 25~26 | DungeonCompletionRequest/Response |
| 27~29 | SkillStateRequest, LearnSkillRequest, SkillStateResponse |
| 30~35 | PartyDetailRequest/Response, PartyJoinRequest/Answer/RequestUpdate, PartyKicked |
| 36~37 | AdmissionTicketRequest, AdmissionResult |

마을 TCP/TLS의 big-endian framing을 던전 NetBuffer나 실시간 payload 직렬화에 적용하지 않습니다. 던전 계약은 [COMBAT_CLIENT](COMBAT_CLIENT.md)에서 따로 설명합니다.

## 송수신과 게임 이벤트

```text
게임 스레드 Send API
 -> TownProtocol::Encode (호출 스레드)
 -> asio::post(strand), 시도 ID/입장 완료 확인
 -> QueuePacket -> TLS async_write

Town network strand
 -> TLS async_read -> Impl::Session::HandlePacket -> Decode
 -> PushEvent (mutex 큐, 시도 ID 확인)
 -> ConsumeEvents -> GameWorld::ProcessNetworkEvents (게임 스레드)
```

네트워크 스레드에서 Player·GameWorld·UI·렌더러를 직접 변경하지 않습니다. 연결 상태·시도 ID는 atomic, 이벤트·상태 묶음은 mutex로 보호합니다. 각 연결의 소켓·송신 큐·deadline은 strand에서 접근합니다. 파일 로드·UI 처리·게임 보간은 strand 밖의 게임 스레드에서 합니다.

LoginFlow가 연결 ready를 확인한 후 Game이 입장 응답 이벤트를 소비합니다. 던전에서도 Town 연결은 입장 승인·파티·스킬·클리어/복귀 제어를 위해 유지합니다. 클리어 후 RUDP 정리 중 수신한 타운 이벤트는 보류했다가 초기화 완료 후 처리합니다.

## 파티·스킬·던전 콘텐츠 연결

- 공개 파티 목록은 페이지·revision을 사용하고, 상세 응답은 선택 partyId와 현재 UI 상태를 확인합니다. 가입 요청은 requestId별 Update와 Answer로 처리합니다.
- 초대와 가입 요청은 서로 다른 계약입니다. 승인/거절 대기 중 중복 전송을 막고, 추방 알림은 `PartyKicked`로 표시합니다. `PartyOperationResult`에는 requestId가 없으므로 다른 대기 요청을 임의로 완료시키지 않습니다.
- 던전 입장 중부터 파티 UI와 스킬 학습을 제한합니다. 서버도 예약 상태를 포함해 허용 여부를 검사합니다.
- `SkillStateResponse.payload`는 `characterId/progression/skillTrees/playerSkills/result` JSON입니다. 클라이언트 ID·카탈로그 일치와 트리/진행값을 검증합니다.
- `LearnSkillRequest`는 skillId와 expectedSkillLevel을 보냅니다. 서버 응답으로만 SP·스킬 레벨을 확정합니다. 10초 무응답은 상태 재조회로 복구합니다.
- 던전 입구는 서버가 `DungeonSelectionOpen`을 보내고 클라이언트가 `EnterDungeonRequest(zoneId,dungeonId)`를 보냅니다. 제공된 목록만 표시합니다.
- RUDP challenge는 타운 `ConfirmDungeonJoin`으로 확인합니다. 클리어 선택은 `DungeonCompletionRequest(roomId,retry)`와 Response로 제어합니다.

## 새 패킷을 연결하는 순서

1. 서버 담당과 ID·방향·필드·범위·거절 결과·세션/중복 처리 규칙을 확정합니다. 현재 ID 1~37은 재사용하지 않습니다.
2. 서버 YAML 원본을 갱신하고 기존 [패킷 생성 도구](../../ActionRPGServer/Tool/README.md)로 양쪽 헤더를 생성합니다.
3. 서버 Protocol의 Encode/Decode와 PlayerSession 라우팅·도메인 검증을 구현합니다.
4. 클라이언트 TownProtocol의 Encode/Decode에 같은 필드 순서와 자료형을 사용합니다. 타입·범위·필수 필드·잔여 바이트를 검사합니다.
5. TownClient Send API에서 인코딩한 패킷을 strand로 보내고 기존 시도 ID·ready 검사를 유지합니다.
6. `Impl::Session::HandlePacket`에서 디코딩한 이벤트를 `TownEvent` variant로 전달하고 GameWorld 또는 해당 UI 모델에서 소비합니다.
7. 해당 계약에 필요한 요청 식별·pending·시간 초과·연결 정리를 구현합니다. 기존 모든 요청에 requestId가 있다고 가정하지 않습니다.
8. 생성 결과·직렬화·콜백 수명을 정적으로 대조한 뒤 양쪽 빌드/실제 왕복 검증을 별도 수행합니다.

새 UI 기능에 네트워크 소켓 접근을 노출하지 않으며, 새 요청 ID를 예시 숫자로 임의 확정하지 않습니다. 접속이 끊기면 pending을 정리하고 새 티켓/로그인 흐름으로 복구해야 합니다.

## 검증 기록과 남은 확인

이번 문서 갱신은 최신 소스·생성 계약·설정·경로를 정적으로 대조한 작업입니다. 2026-10-06 이전 작업에서는 NASM 3.01 실행/CMake 검색, OpenSSL 3.6.1#3 바이너리 캐시 설치, Debug x64 컴파일·링크와 임시 PATH/manifest 강제 옵션 없는 일반 빌드 성공을 확인했습니다. 최초 NASM 검색 실패 원인은 재현하지 못했고 전역 PATH·vcpkg 도구 스크립트는 수정하지 않았습니다. 첫 컴파일의 기존 RUDP 공용 헤더 `NetServerSerializeBuffer.h` C4828 경고는 별개입니다.

이번에는 Release 빌드·기능 테스트·클라이언트/서버 실행·실제 인증 왕복·DB 확인을 수행하지 않았습니다. 이전 빌드 성공을 서버 준비나 DB 마이그레이션 적용의 근거로 사용하지 않습니다.

별도 실행 확인에는 로그인/거절/503/ready=false, 단계별 취소·역순 완료·만료·TLS 거절, 로그아웃 실패, 마을/던전에서 계정·타운 전환, 대기 중 창 닫기, 파티 동시 요청과 스킬 응답 지연이 포함됩니다. 실제 주소·CA·Google 등록 및 서버·DB 배포 상태는 운영 설정과 담당 문서에서 확인합니다.

코드 근거: [LoginFlow](ActionRPGClient/App/LoginFlow.cpp), [AuthClient](ActionRPGClient/Network/AuthClient.cpp), [AuthHttps](ActionRPGClient/Network/AuthHttps.cpp), [AuthSettings](ActionRPGClient/Network/AuthSettings.cpp), [TownClient](ActionRPGClient/Network/TownClient.cpp), [TownProtocol](ActionRPGClient/Network/TownProtocol.cpp), [TownEvent](ActionRPGClient/Network/TownClient.h), [GameWorld](ActionRPGClient/Game/GameWorld.cpp).
