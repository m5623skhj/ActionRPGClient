# ActionRPGClient 개발 가이드

2026-10-06 클라이언트 `1d153cf` 기준의 구현 안내입니다. 설정·빌드는 [README](README.md), 인증·타운 패킷은 [TOWN_NETWORK](TOWN_NETWORK.md), 던전 입력·보간은 [COMBAT_CLIENT](COMBAT_CLIENT.md), 데이터 경로는 [Assets](Assets/README.md)를 참고합니다.

## 코드 책임

아래 소스 경로는 이 문서가 있는 폴더를 기준으로 합니다.

| 위치 | 책임 |
| --- | --- |
| [Main.cpp](ActionRPGClient/Main.cpp) | DPI·COM 초기화, Application 실행, 최상위 예외 표시. 명령줄 인자는 해석하지 않음 |
| [App/Application.cpp](ActionRPGClient/App/Application.cpp) | 창·그래픽·네트워크·LoginFlow 수명, 고정 업데이트·렌더, Game 생성·폐기 |
| [App/LoginFlow.cpp](ActionRPGClient/App/LoginFlow.cpp) | 로그인·서버/종류/이름 선택·취소·세션 메뉴 전환 |
| [Platform/GameWindow.cpp](ActionRPGClient/Platform/GameWindow.cpp), [InputState.h](ActionRPGClient/Input/InputState.h) | 키 유지/누름, 텍스트, 클릭·드래그·휠, 포커스·창 메시지 |
| [Game/GameWorld.cpp](ActionRPGClient/Game/GameWorld.cpp) | 네트워크 이벤트 적용, 월드·맵·파티·던전·메뉴, 입력 차단, 카메라·HUD |
| [Game/Player.cpp](ActionRPGClient/Game/Player.cpp), [Character.cpp](ActionRPGClient/Game/Character.cpp) | 로컬 입력·위치 보정, 공통 이동·점프·사격·방향 고정·피격·동작 표시 |
| [Game/SkillUi.cpp](ActionRPGClient/Game/SkillUi.cpp), [PlayerSkillPresentation.cpp](ActionRPGClient/Game/PlayerSkillPresentation.cpp) | 서버 스킬 상태 모델·습득 요청·슬롯/쿨타임, 커맨드 판정·스킬 이미지 |
| [Game/DungeonWorld.cpp](ActionRPGClient/Game/DungeonWorld.cpp), [DungeonCombat.cpp](ActionRPGClient/Game/DungeonCombat.cpp) | 월드·전투 JSON 및 실시간 바이너리 검증 |
| [Game/DungeonCombatBuffer.cpp](ActionRPGClient/Game/DungeonCombatBuffer.cpp) | 게임 스레드 전용 원격 상태 이력·표시 시각 보간 |
| [Network/AuthClient.cpp](ActionRPGClient/Network/AuthClient.cpp), [AuthHttps.cpp](ActionRPGClient/Network/AuthHttps.cpp), [AuthSettings.cpp](ActionRPGClient/Network/AuthSettings.cpp) | OAuth·Auth HTTPS·결과 큐·설정 |
| [Network/TownClient.cpp](ActionRPGClient/Network/TownClient.cpp), [TownProtocol.cpp](ActionRPGClient/Network/TownProtocol.cpp) | 타운 TLS·입장 승인·패킷 인코딩/디코딩·이벤트 큐 |
| [Network/DungeonClient.cpp](ActionRPGClient/Network/DungeonClient.cpp) | RUDP 기동·중단, reliable/unreliable 큐 소비·월드/상태 재조립 |
| [Resources/AssetCatalog.cpp](ActionRPGClient/Resources/AssetCatalog.cpp), [SpriteAnimation.cpp](ActionRPGClient/Resources/SpriteAnimation.cpp) | 실행 파일 기준 Assets 경로·스프라이트 |
| `Graphics/`, `Game/GameplayMap.*`, `Game/MapBackground.*` | 그래픽 장치·그리기, 예측용 이동 영역과 서버 배경 배치 |

## 기동·업데이트·렌더

1. `Main`이 `Application`을 생성합니다. 최초 화면은 `LoginFlow`이며 아직 `Game`은 없습니다.
2. 창 메시지를 처리하고 키 누름·텍스트·휠·클릭을 다음 업데이트까지 누적합니다. 업데이트는 60Hz, 한 프레임 시간은 최대 0.25초로 제한합니다. 최소화 시 `WaitMessage`로 기다립니다.
3. `LoginFlow::Update`가 Auth 이벤트와 타운 연결 상태를 처리합니다. TLS 티켓 승인과 `EnterTownResponse`까지 완료하면 Playing으로 전환합니다.
4. Playing에서만 `Game`을 만듭니다. 첫 업데이트에 빈 입력을 사용해 입장 버튼 입력이 게임 행동으로 이어지지 않게 합니다. 다른 로그인 상태로 전환하면 Game을 폐기합니다.
5. `GameWorld::Update`는 UI 포인터 입력, 연결 정리 완료, 타운·던전 이벤트, 피격 큐, UI·스킬 모델, 차단된 게임 입력, 로컬 플레이어·전송·원격 개체·카메라를 갱신합니다.
6. 렌더는 별도로 진행합니다. 맵 표시 영역으로 배경·개체를 자르고, HUD·스킬 바·메뉴·파티·클리어창을 그립니다. 창 크기 변경은 그래픽·카메라를 갱신하고 스킬 드래그를 취소합니다.
7. 세션 메뉴는 Game의 요청을 Application이 소비해 LoginFlow에 전달합니다. 기존 Game을 폐기한 뒤 로그아웃·계정 전환·타운 변경을 진행합니다.

## 스레드와 소유권

```text
Auth worker -> 시도 ID가 붙은 mutex 이벤트 큐 -> LoginFlow (게임 스레드)
Town Asio strand -> 패킷 Decode -> mutex 이벤트 큐 -> GameWorld (게임 스레드)
RUDP worker -> 코어 수신 큐 -> DungeonClient::ConsumeEvents (게임 스레드)
                                     -> 재조립·검증 -> GameWorld
GameWorld -> DungeonCombatBuffer::Push/Sample -> 원격 개체 표시
```

네트워크 콜백은 Player·GameWorld·UI·렌더러를 직접 변경하지 않습니다. 타운 스트림과 송신 큐는 strand, Auth 작업은 전용 worker, 스킬 UI·던전 재조립·보간 이력은 게임 스레드가 소유합니다. `QueuePlayerHit`는 외부 피격 요청을 mutex 큐로 받아 게임 스레드에서 적용하는 별도 진입점입니다. 던전 실제 피해/반응은 서버 전투 상태를 적용합니다.

UI 전환의 종료 대기는 `RequestStop` 후 `IsStopComplete`를 매 tick 확인합니다. RUDP 연결 worker는 stop token으로 브로커 대기를 취소한 뒤 정리합니다. 프로세스 최종 소멸에서는 worker와 코어를 정리·join합니다. 종료 future가 활성화된 동안 impl의 송수신/소비를 막으며, 새 연결 전에 기존 종료 완료를 확인합니다.

## 월드·이동 흐름

마을 맵은 클라이언트가 서버 원본 JSON을 직접 읽지 않습니다. `EnterTownResponse/MapChanged`의 표시 영역·이미지 배치·이동 다각형·시작 위치를 적용합니다. 이미지 자체는 전송하지 않으므로 대응하는 Assets 파일이 필요합니다. 섹터 가시 범위·구역 진입·최종 이동 가능 여부는 TownServer가 결정합니다.

마을 로컬 이동은 즉시 예측합니다. 방향 전환·정지를 즉시 전송하고 같은 이동은 0.25초 heartbeat로 유지합니다. 입력 sequence와 서버 tick으로 오래된 응답을 거르고, 실제 보낸 시각으로 제한된 지연 추정을 하며 작은 오차를 프레임 단위로 보정합니다. 다른 마을 플레이어는 위치·속도에 최대 0.25초 예측과 보정을 적용합니다. 마을은 달리기를 비활성화합니다.

던전 로컬 지면 이동도 즉시 예측합니다. 서버의 높이·반응·행동·HP·탄환을 따르며, 몬스터와 다른 플레이어는 125ms 지연한 서버 시각의 두 상태를 보간합니다. 마을 원격 표시 방식과 던전 버퍼는 다릅니다. 방·epoch·연결·클리어·복귀·재도전에서 이력과 입력을 초기화합니다. 세부 조건은 [던전 전투](COMBAT_CLIENT.md)에 있습니다.

## UI와 게임 입력

메뉴·파티 페이지·던전 목록·초대·추방 알림·클리어창·입장 전환·피격은 게임 입력을 막고 커맨드 큐를 비웁니다. 프레임 시작 때 UI가 열려 있었다면 닫은 입력도 게임에 넘기지 않습니다. UI 포인터 소비 여부와 드래그 취소를 유지해야 버튼 클릭·단축키가 캐릭터 행동으로 중복 처리되지 않습니다.

`IsDungeonUiRestricted()`는 던전 입장 상태가 Idle이 아닌 모든 구간에 적용합니다. 파티 페이지를 닫고 신규 가입 요청 UI를 정리하며, 스킬은 조회·슬롯 편집만 허용합니다. 서버도 던전 예약/입장 중 학습을 거절하므로 UI 제한만 최종 검증으로 사용하지 않습니다.

파티 목록은 page/revision 기반으로 갱신하며 상세 요청은 선택한 partyId와 UI 페이지를 확인해 적용합니다. 가입 요청은 requestId별로 대기·승인·거절·만료를 표시하고 파티장의 대기 요청을 큐로 관리합니다. 일반 OperationResult에는 requestId가 없으므로 임의 요청을 제거하지 않습니다. 추방은 빈 PartySnapshot으로 추측하지 않고 피해자에게 전달된 `PartyKicked`를 통해 알립니다.

## 스킬·성장

`SkillStateResponse`의 JSON에 캐릭터 ID, progression, skillTrees, playerSkills, result가 들어옵니다. 수신 카탈로그는 로컬 PlayerSkills와 내용이 같아야 하며 트리·진행값을 검증한 뒤 적용합니다. TownServer가 레벨·SP·스킬 레벨을 소유합니다. UI는 요구 레벨·SP·선행 스킬·상한을 안내하고 `LearnSkillRequest(skillId, expectedSkillLevel)`만 보냅니다.

습득 대기 10초가 지나면 상태를 무효화하고 다시 조회하며 성공을 미리 확정하지 않습니다. 서버의 LevelAdvanced도 새 상태로 표시합니다. 현재 초기 SP 0·레벨당 200 SP는 서버 [CharacterProgression.json](../../ActionRPGServer/ActionRPGServer/TownServer/Data/CharacterProgression.json)의 정책이고, 클라이언트는 레벨업/경험치 지급을 요청하는 UI를 구현하지 않았습니다.

슬롯은 `A/S/D/F/G/H`, 습득한 자기 캐릭터 스킬만 등록합니다. 현재 저장 파일은 `%LOCALAPPDATA%/ActionRPGClient/skill-slots-Character<ID>.json`이며 캐릭터 종류별 UI 설정입니다. 계정별 DB 캐릭터 저장이 아니며 다른 계정의 같은 종류도 같은 로컬 슬롯 파일을 사용합니다.

마을에서는 로컬 스킬 표시·쿨타임을 사용하고, 던전에서는 서버 수락과 reliable 전투 JSON으로 쿨타임을 확인합니다. 실시간 SKL1에는 학습 진행값·쿨타임이 없으므로 JSON을 빈 기본값으로 덮어쓰지 않습니다. 오래된 룸 진행값도 최신 타운 학습 권한을 덮어쓰지 않습니다.

## 확장할 위치

1. 데이터 ID·권위 주체를 먼저 결정합니다. 캐릭터 종류·스킬 ID·몬스터 ID는 서버와 동일하게 관리합니다.
2. 플레이어 표현은 Player/Character, 스킬 프레임·효과는 PlayerSkillPresentation, UI는 SkillUi나 해당 월드 페이지에 연결합니다. 피해·습득 권한·보상은 서버에서 판정합니다.
3. 캐릭터의 마을 점프 속도는 characters.ini에 둡니다. 던전은 서버 combatRules를 적용하며 이 값을 다시 곱해 보정하지 않습니다.
4. 새 패킷은 서버 Tool의 YAML 원본을 수정·생성한 뒤 양쪽 직렬화와 이벤트 소비를 함께 연결합니다. 생성 헤더의 ID를 임의로 추가하지 않습니다.
5. 이미지·INI/JSON은 원본 Assets에 반영하고 빌드 또는 SyncClientAssets로 배포합니다. 편집기 자체는 각 도구 문서를 따릅니다.
6. 캐릭터 영속 저장·복원은 서버/DB 계약과 연결해 구현해야 합니다. 클라이언트 로컬 슬롯 파일을 저장 권한·학습 정보의 원본으로 확장하지 않습니다.

## 확인 범위

문서 작성에서는 소스·데이터·생성 패킷 정의·상대 경로를 정적으로 대조했습니다. 이전 Debug x64 빌드 성공과 이번 확인을 구분하며, 이번에는 빌드·게임/서버 실행·DB 작업·기능 테스트를 하지 않았습니다. 실제 인증 설정·파티 동시 요청·패킷 손실·방 전환·클리어 선택·창 종료는 별도 실행 확인이 필요합니다.
