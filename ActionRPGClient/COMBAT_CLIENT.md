# 던전 전투 클라이언트

2026-10-06 클라이언트 `1d153cf`와 현재 서버 계약 기준입니다. 던전 입장·입력·표시·클리어 후 전환을 설명합니다.

공유 패킷 원본은 [Tool/PacketDefine.yml](../../ActionRPGServer/Tool/PacketDefine.yml), 실행 규칙은 [서버 전투 계약](../../ActionRPGServer/ActionRPGServer/GameRoomServer/COMBAT_PROTOCOL.md)과 [플레이어 스킬 계약](../../ActionRPGServer/ActionRPGServer/GameRoomServer/PLAYER_SKILLS.md)입니다. [이동 보간 계약](../../ActionRPGServer/output/movement-smoothing/SERVER_HANDOFF.md)의 요청 ID는 `movement-smoothing-20261003`, 실시간 wire version은 1입니다.

## 입장과 월드 적용

1. TownServer가 입구 구역 진입을 판정하고 `DungeonSelectionOpen`으로 실제 목록을 보냅니다. 클라이언트는 목록에서 위/아래·Enter로 선택하며 C로 닫습니다.
2. `EnterDungeonRequest(zoneId,dungeonId)`를 타운에 보냅니다. 성공 응답의 roomId·combatSeed·sessionBrokerAddress/Port를 사용해 DungeonClient를 시작합니다.
3. 별도 연결 worker가 MultiSocketRUDP 코어로 TLS session broker 교환과 RUDP 연결을 진행합니다. `ClientOptionFile/CoreOption.txt`와 서버가 지정한 브로커를 사용합니다.
4. RUDP `DungeonChallenge`를 받으면 타운 TLS 연결로 `ConfirmDungeonJoin(roomId,challenge)`을 보냅니다. `DungeonAuthResult` 성공 뒤에만 월드 조각을 요청합니다.
5. 월드 JSON을 재조립한 뒤 roomId·localPlayerId·맵/몬스터 참조·스킬 계약을 검증합니다. 서버/로컬 PlayerSkills가 다르면 진입을 중단합니다.
6. 입장 맵·좌표·combatRules를 적용하고 실시간 v1 구독과 reliable 전투 JSON 요청을 시작합니다. Running 상태를 받은 뒤 행동 입력을 전송합니다.

상태는 Idle → WaitingRoom → Connecting → WaitingAuthentication → WaitingWorld → Entered입니다. 선택창이 닫힌 것만으로 입장이 완료되지 않습니다. 화면의 Dungeon 상태와 Town/GameRoom 연결·인증·월드 데이터 오류를 함께 확인해야 합니다. 목록·입장 조건·몬스터 처치에 따른 워프 허용은 서버가 결정합니다.

마을 연결은 던전에서도 유지합니다. 던전 전투 연결과 타운 로그인 TLS는 별도 채널이며 타운 CA 설정을 RUDP 코어 옵션으로 해석하지 않습니다.

## 입력·로컬 반응·권위

| 입력/표시 | 처리 |
| --- | --- |
| 방향키·달리기 | 로컬 지면 이동을 즉시 예측. 방향/달리기 변경 즉시, 같은 입력은 0.1초 간격으로 reliable 전송 |
| X | `DungeonActionInput` action=1, 기본 사격 |
| C | `DungeonActionInput` action=2, 점프 |
| 습득한 커맨드 또는 A/S/D/F/G/H | `DungeonSkillInput`의 skillId·facingLeft |
| V | 던전 기본 action으로 보내지 않음 |
| HP·피격·죽음·시전·탄환·클리어 | 서버 상태 적용 |

기본 행동과 스킬은 연결별 action sequence를 공유하며 최대 20입력/초로 제한합니다. 서버가 처리한 action/move sequence가 보낸 범위를 넘으면 무효 상태입니다. 이동의 오래된 ack는 로컬 위치 보정만 생략하고 최신 HP·명단·클리어를 버리는 기준으로 사용하지 않습니다.

로컬 지면 예측은 사망·피격·활성 스킬·사격 준비/발사 등 서버 상태의 제한을 따릅니다. 점프 중에도 허용되는 지면 입력은 즉시 반영합니다. 로컬 위치 오차 16 이내는 보정을 생략하고 200을 넘으면 즉시 맞추며 그 사이는 제한된 보정을 적용합니다. 높이·행동 단계는 최신 서버 상태에서 최대 0.25초 표시 예측을 사용합니다. 몬스터/원격 플레이어의 125ms 보간 지연을 본인 입력에 적용하지 않습니다.

던전에는 서버가 보낸 탄환만 표시합니다. 클라이언트는 발사·피해·타깃·충돌·AI·보상·워프 잠금을 확정하지 않습니다. 사격/스킬 입력 직후에는 방향만 잠시 고정해 응답 대기 중 반대로 도는 현상을 줄이며, 발사나 피해를 예측하지 않습니다. 수락/거절·서버 상태·피격/착지·시간 초과·방 전환이 이 고정을 해제합니다.

동일 사격 묶음의 준비·발사·회수와 후속 입력 유예 동안 방향을 유지합니다. 기본 사격은 최대 5발, 점프 전체 공중 사격도 최대 5발입니다. 후속 입력 유예 0.4초·기본 행동 예약 최대 0.25초는 서버 규칙과 마을 공통 동작에 반영되어 있습니다. 예약 수락은 즉시 발사·타격 성공을 뜻하지 않습니다.

`DungeonActionResult.accepted=false`를 받으면 ACTION NOT ACCEPTED를 1초 표시합니다. 사망·피격·다른 행동·쿨타임·허용 단계·제한 등에서 서버가 거절할 수 있지만 이 패킷에는 상세 거절 사유가 없습니다. 표시만으로 특정 원인을 단정하지 않습니다.

## 스킬과 성장 상태

앞차기의 현재 ID는 `Character1.FrontKick`, 입력은 Up 다음 Z, 단계 간 최대 0.35초입니다. 지상 전용이며 해당 캐릭터가 습득하고 쿨타임이 끝나야 합니다. unavailable 커맨드도 즉시 소비하여 나중에 자동 발동하거나 마지막 X/C 입력이 기본 사격/점프로 이어지지 않게 합니다.

스킬 학습·강화는 TownServer가 처리하고 던전 예약/입장 중에는 허용하지 않습니다. 던전에서는 스킬 창 조회와 습득 아이콘의 단축키 등록을 허용합니다. 서버 권한·캐릭터별 소유·지상/공중 허용·시전 상태를 확인한 뒤 요청하며, 슬롯 파일이 실행 권한을 만들지 않습니다.

실시간 SKL1 확장은 캐릭터·시전 번호·스킬·시간·버프·투사체 정보를 포함하지만 진행값·쿨타임은 포함하지 않습니다. reliable JSON이 이를 보완합니다. 서버 수락 시 쿨타임을 표시하고 JSON 갱신을 요청하며, GameWorld는 별도의 최신 tick/accepted sequence 기준으로 쿨타임을 적용합니다. 오래된 JSON 위치로 최신 실시간 자세를 덮어쓰지 않고, 룸 JSON의 진행값으로 최신 타운 학습 권한을 덮어쓰지도 않습니다.

## 패킷·전달 주기

[DungeonProtocol.h](ActionRPGClient/Network/DungeonProtocol.h)는 YAML 생성 결과입니다. ID 1~10을 유지하고 11~13에 실시간, 14에 플레이어 스킬 입력을 추가했습니다.

| ID | 계약 |
| --- | --- |
| 1~2 | DungeonChallenge, DungeonAuthResult |
| 3~4 | DungeonWorldRequest/Chunk |
| 5~6 | DungeonMoveInput, DungeonPlayerState |
| 7~8 | DungeonActionInput/Result |
| 9~10 | DungeonCombatStateRequest/Chunk |
| 11~12 | DungeonRealtimeRequest/Result, reliable |
| 13 | DungeonRealtimeChunk, unreliable |
| 14 | DungeonSkillInput, reliable |

실시간 구독은 월드 수신 뒤 version=1·enabled=1·현재 challenge로 요청합니다. 구독 결과의 roomId·dungeonId·version·challenge를 확인합니다. 첫 상태 조각은 reliable 구독 결과보다 먼저 도착할 수 있습니다.

현재 서버 목표는 **시뮬레이션 30Hz, 상태 전달 15Hz**입니다. 안내 필드는 `tickIntervalMs=33`, `snapshotIntervalMs=67`이지만 실제 tick dt는 1/30초입니다. 클라이언트는 v1의 이전 20Hz 안내값 50ms도 허용하고 전달 간격 50~100ms를 확인합니다. 33ms를 실제 dt로 누적하지 않습니다.

실시간 조각에는 연결 challenge, roomId, dungeonId, mapEpoch, snapshotSequence, serverTick, serverTimeMs, mapId, 길이/offset, state가 있습니다. serverTimeMs는 서버 steady_clock 캡처 시각이며 Unix 시각이 아닙니다. 도착 시각과의 오프셋을 추정해 표시 시각을 만듭니다.

payload는 최대 48KiB, 조각은 최대 768바이트입니다. 내부 레코드는 little-endian·IEEE754 float이며 player/monster/projectile 개수, actor 레코드와 SKL1 tail을 읽습니다. 플레이어 1~4, 몬스터/탄환 각각 최대 256, 유한 수치·ID 중복·참조·문자열/enum/bool·남은 바이트를 검사합니다. 바깥 필드는 기존 NetBuffer 직렬화와 YAML 순서를 따르므로 타운 TCP framing과 혼동하지 않습니다.

## 재조립·손실·복구

RUDP worker는 코어 큐만 변경합니다. 게임 스레드의 `DungeonClient::ConsumeEvents`가 reliable/unreliable 큐를 각각 최대 64개씩 소비하고 재조립·검증한 완성 이벤트를 GameWorld에 전달합니다. 조각 수신 중 게임 개체나 보간 이력을 네트워크 스레드에서 수정하지 않습니다.

실시간 재조립은 최신 2개 프레임으로 제한하고 500ms 지난 미완성 프레임을 제거합니다. 같은 프레임의 맵·epoch·tick·시각·state·전체 길이가 다르면 버립니다. 중복 조각, 낮은 epoch, 완료한 sequence 이하를 적용하지 않습니다. 조각 역순은 offset으로 처리하되 코어 unreliable sequence에서 오래된 패킷이 버려져 프레임이 유실될 수 있습니다. 불완전 프레임을 적용하지 않고 다음 완성 프레임을 기다립니다.

높은 mapEpoch의 첫 조각은 이전 재조립과 표시 버퍼를 즉시 비우는 reset 이벤트를 만듭니다. 같은 mapId 재방문도 epoch가 달라 이전 방 상태를 재사용하지 않습니다. 최신 완성 명단에서 사라진 개체만 제거합니다.

| 경로 | 한도·주기 |
| --- | --- |
| 초기 월드 JSON | 768바이트 순차 조각, 최대 4MiB, 진행 제한 30초 |
| reliable 전투 JSON | 768바이트 순차 조각, 최대 512KiB, 조각별 UTF-8 해석 없이 완성 후 Parse |
| JSON 제한 응답 | 같은 snapshotId/offset을 retryAfterMs 뒤 재요청 |
| JSON 응답 유실 | 5초 뒤 재요청; 진행 없이 30초이고 실시간도 신선하지 않으면 연결 실패 |
| 실시간 신선도 | 마지막 완성 프레임 500ms 미만 |
| JSON 다음 요청 | 실시간이 신선하면 완료 후 1000ms, fallback이면 200ms |
| 스킬 수락 | 중복 전송 없이 JSON refresh 요청을 합쳐 처리 |

실시간이 오래되면 JSON 복구를 앞당깁니다. **200ms/5Hz는 복구 경로**이며 정상 원격 표시 주기가 아닙니다. 이 낮은 주기에서 125ms 이력만으로 항상 두 샘플을 확보한다고 보장하지 않습니다.

## 시간 기반 표시 버퍼

[DungeonCombatBuffer](ActionRPGClient/Game/DungeonCombatBuffer.cpp)는 최근 최대 8개 완성 상태를 게임 스레드에서 보관합니다. 표시 목표 시각은 추정 서버 시각보다 125ms 이전이고, 동일 개체의 인접 상태 사이 위치·높이를 선형 보간합니다. 방향·행동 단계·시전/반응 타이머는 그 표시 시각에 맞는 상태를 사용합니다. HP·명단·사망·클리어는 최신 확정 상태를 반영합니다.

- 서버 시각 감소·완료 sequence 중복을 거릅니다. 같은 서버 시각의 새 프레임은 표본을 교체합니다.
- 오프셋은 천천히 보정하고 표시 시각을 뒤로 돌리지 않습니다.
- 최신 표본 이후 측정 속도 외삽은 최대 100ms입니다. 500ms 이상 중단되면 마지막 자세를 유지하고 이동 표시를 멈춥니다.
- 다른 room/mapEpoch/mapId/실시간 여부, 500ms 이상 수신 간격은 이력을 초기화합니다.
- 신규/삭제, 사망을 끼운 상태, 200 이상 위치 불연속은 일반 이동으로 보간하지 않습니다. 클리어에서는 미래 외삽을 제한합니다.
- 적용된 원격 위치에 다시 마지막 좌표 추종을 중첩하지 않습니다. 원격 Player의 보간된 높이에 로컬 중력을 다시 적용하지 않습니다.

Character는 reaction/shot/jump sequence·단계·발수 변경을 동작 전환으로 사용합니다. 같은 동작의 반복 수신은 애니메이션 시간을 역행시키지 않습니다. Monster는 action/reaction sequence와 animationId, Player 스킬은 skillSequence/ID/airborne을 사용해 재시작과 지속을 구분합니다. 사망 자세도 반복 상태로 재시작하지 않습니다.

원격 플레이어의 걷기/달리기 재생은 보간 구간의 실제 이동 속도와 연결합니다. Player의 재생 배율은 0.25~2 범위이며 이동이 없는 상태에 억지 걷기 프레임을 적용하지 않습니다. 몬스터 걷기는 메타데이터 fps로 재생하며 같은 속도 배율 경로가 연결되지 않았습니다. 부하·지연·손실에서의 실제 체감과 발 미끄러짐은 실행 검증이 필요합니다. 이 버퍼는 표시 도구이며 피해량·피격 대상·충돌 판정의 근거가 아닙니다.

## 클리어·복귀·재도전

서버 cleared 상태를 처음 받으면 게임 입력과 기존 UI를 차단하고 마을 이동·재도전 선택 창을 표시합니다. 방향키/Enter 또는 클릭으로 선택하며, 솔로 또는 파티장만 요청할 수 있고 파티원은 파티장 선택을 기다립니다.

`DungeonCompletionRequest(roomId,retry)`는 타운으로 보내고 응답 전 중복 요청을 막습니다. 30초 지연이나 연결 문제가 있으면 대기 상태를 해제해 안내합니다. 성공 응답에서는 기존 RUDP의 `RequestStop`을 호출하고 매 tick 종료 완료를 확인합니다. 그 동안 타운 이벤트는 보류합니다.

종료 완료 후 dungeon world·개체·보간·탄환·커맨드·action/move sequence·쿨타임 표시·방향 예측을 초기화합니다. 복귀는 타운 맵/위치를 적용하고, 재도전은 응답의 새 roomId/브로커로 challenge와 월드 인증부터 다시 진행합니다. 이전 연결·방·epoch의 버퍼가 새 던전에 섞이지 않게 합니다. 로그아웃·계정/타운 전환도 기존 Game을 폐기하고 연결을 정리합니다.

## 데이터·검증 범위

마을 점프 속도는 캐릭터별 `characters.ini`에서 읽고, 던전 높이·속도·중력·동작 시간은 월드 combatRules를 그대로 사용합니다. 몬스터 시트는 종류별 sourceRect/pivot/fps/scaleToMovement 또는 renderSize를 사용합니다. UseSkill의 `attack`은 해당 몬스터 공격 시트로 연결합니다. 플레이어 별도 사망 시트는 없어 쓰러짐 마지막 자세로 표시합니다.

코드 근거: [GameWorld](ActionRPGClient/Game/GameWorld.cpp), [DungeonClient](ActionRPGClient/Network/DungeonClient.cpp), [DungeonCombat](ActionRPGClient/Game/DungeonCombat.cpp), [Character](ActionRPGClient/Game/Character.cpp), [Monster](ActionRPGClient/Game/Monster.cpp), [Player](ActionRPGClient/Game/Player.cpp), [SkillUi](ActionRPGClient/Game/SkillUi.cpp).

이번에는 문서·생성 패킷·서버 계약을 정적으로 확인했습니다. 이전 Debug 빌드 성공은 실제 플레이 검증과 다릅니다. 손실·지연·역순·중복, 500ms 이상 중단, 워프/재방문·텔레포트·생성/삭제·사망·보스 클리어·파티 복귀/재도전·연결 리셋·창 종료는 추가 실행 확인이 필요합니다. 드랍·보상 지급·부활 등 서버 미구현 범위는 서버 문서를 따릅니다.
