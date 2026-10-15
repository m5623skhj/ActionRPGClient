# 던전 전투 클라이언트

2026-10-13 슬라이딩·역경직·히트 리커버리·Force/에어본 연동을 포함한 현재 클라이언트와 서버 계약 기준입니다. 던전 입장·입력·표시·클리어 후 전환을 설명합니다.

공유 패킷 원본은 [Tool/PacketDefine.yml](../../ActionRPGServer/Tool/PacketDefine.yml), 실행 규칙은 [서버 전투 계약](../../ActionRPGServer/ActionRPGServer/GameRoomServer/COMBAT_PROTOCOL.md)과 [플레이어 스킬 계약](../../ActionRPGServer/ActionRPGServer/GameRoomServer/PLAYER_SKILLS.md)입니다. [이동 보간 계약](../../ActionRPGServer/output/movement-smoothing/SERVER_HANDOFF.md)의 요청 ID는 `movement-smoothing-20261003`, 이 문서의 현재 전투 wire version은 슬라이딩·역경직·히트 리커버리를 포함한 3입니다. 이전 이동 보간 인계의 version 1과 혼용하지 않습니다.

## 입장과 월드 적용

1. TownServer가 입구 구역 진입을 판정하고 `DungeonSelectionOpen`으로 실제 목록을 보냅니다. 클라이언트는 목록에서 위/아래·Enter로 선택하며 C로 닫습니다.
2. `EnterDungeonRequest(zoneId,dungeonId)`를 타운에 보냅니다. 성공 응답의 roomId·combatSeed·sessionBrokerAddress/Port를 사용해 DungeonClient를 시작합니다.
3. 별도 연결 worker가 MultiSocketRUDP 코어로 TLS session broker 교환과 RUDP 연결을 진행합니다. `ClientOptionFile/CoreOption.txt`와 서버가 지정한 브로커를 사용합니다.
4. RUDP `DungeonChallenge`를 받으면 타운 TLS 연결로 `ConfirmDungeonJoin(roomId,challenge)`을 보냅니다. `DungeonAuthResult` 성공 뒤에만 월드 조각을 요청합니다.
5. 월드 JSON을 재조립한 뒤 roomId·localPlayerId·맵/몬스터 참조·스킬 계약을 검증합니다. 서버/로컬 PlayerSkills가 다르면 진입을 중단합니다.
6. 입장 맵·좌표·combatRules를 적용하고 실시간 v3 구독과 reliable 전투 JSON 요청을 시작합니다. Running 상태를 받은 뒤 행동 입력을 전송합니다.

상태는 Idle → WaitingRoom → Connecting → WaitingAuthentication → WaitingWorld → Entered입니다. 선택창이 닫힌 것만으로 입장이 완료되지 않습니다. 화면의 Dungeon 상태와 Town/GameRoom 연결·인증·월드 데이터 오류를 함께 확인해야 합니다. 목록·입장 조건·몬스터 처치에 따른 워프 허용은 서버가 결정합니다.

마을 연결은 던전에서도 유지합니다. 던전 전투 연결과 타운 로그인 TLS는 별도 채널이며 타운 CA 설정을 RUDP 코어 옵션으로 해석하지 않습니다.

## 입력·로컬 반응·권위

| 입력/표시 | 처리 |
| --- | --- |
| 방향키·달리기 | 로컬 지면 이동을 즉시 예측. 방향/달리기 변경 즉시, 같은 입력은 0.1초 간격으로 reliable 전송 |
| X (달리기 이외) | `DungeonActionInput` action=1, 기본 사격 |
| 던전 지상 달리기 중 X | action=3, Slide 기본 행동. 일반 사격과 중복 소비하지 않음 |
| C | DungeonActionInput action=2, 점프 |
| 습득한 커맨드 또는 A/S/D/F/G/H·Q/W/E/R/T/Y | `DungeonSkillInput`의 skillId·facingLeft |
| V | 던전 기본 action으로 보내지 않음 |
| HP·피격·죽음·시전·탄환·클리어 | 서버 상태 적용 |

기본 행동과 스킬은 연결별 action sequence를 공유하며 최대 20입력/초로 제한합니다. 서버가 처리한 action/move sequence가 보낸 범위를 넘으면 무효 상태입니다. 이동의 오래된 ack는 로컬 위치 보정만 생략하고 최신 HP·명단·클리어를 버리는 기준으로 사용하지 않습니다.

로컬 지면 예측은 사망·피격·활성 스킬·사격 준비/발사 등 서버 상태의 제한을 따릅니다. 점프 중에도 허용되는 지면 입력은 즉시 반영합니다. 일반 이동의 로컬 위치 오차 16 이내는 보정을 생략하고 200을 넘으면 즉시 맞추며 그 사이는 제한된 보정을 적용합니다. 서버 확정 피격 밀림과 종료 좌표는 이 무시 구간·35% 보정 및 moveSequence 위치 보정 조건을 적용하지 않고 정확히 반영합니다. 높이·행동 단계는 최신 서버 상태에서 최대 0.25초 표시 예측을 사용합니다. 몬스터/원격 플레이어의 125ms 보간 지연을 본인 입력에 적용하지 않습니다.

던전에는 서버가 보낸 탄환만 표시합니다. 클라이언트는 발사·피해·타깃·충돌·AI·보상·워프 잠금을 확정하지 않습니다. 사격/스킬 입력 직후에는 방향만 잠시 고정해 응답 대기 중 반대로 도는 현상을 줄이며, 발사나 피해를 예측하지 않습니다. 수락/거절·서버 상태·피격/착지·시간 초과·방 전환이 이 고정을 해제합니다.

동일 사격 묶음의 준비·발사·회수와 후속 입력 유예 동안 방향을 유지합니다. 기본 사격은 최대 5발, 점프 전체 공중 사격도 최대 5발입니다. 후속 입력 유예 0.4초·기본 행동 예약 최대 0.25초는 서버 규칙과 마을 공통 동작에 반영되어 있습니다. 예약 수락은 즉시 발사·타격 성공을 뜻하지 않습니다.

`DungeonActionResult.accepted=false`를 받으면 ACTION NOT ACCEPTED를 1초 표시합니다. 사망·피격·다른 행동·쿨타임·허용 단계·제한 등에서 서버가 거절할 수 있지만 이 패킷에는 상세 거절 사유가 없습니다. 표시만으로 특정 원인을 단정하지 않습니다.

## 던전 전투 템포

2026-10-12: 서버 `GameRoomServer/Data/Combat.json`의 `tempoScale=0.7`을 초기 world의 combatRules로 받아 사용합니다. 값은 유한한0.1~1이고 생략은1입니다. 클라이언트에0.7 상수를 별도로 두지 않습니다. 전투 wire/rules version3·필드 순서를 유지합니다. 마을·전체 Update·렌더FPS·UI 입력/HP바 갱신·30Hz tick·15Hz 상태 전송·125ms 원격 보간·heartbeat·인증/요청 제한시간은 실제 시간으로 진행합니다. 게임/렌더 스레드에서 규칙과 버퍼를 소비하며 새 네트워크 공유 버퍼나 스레드를 추가하지 않습니다.

행동 시간은 정지 구간을 제외한 실제 시간×tempo입니다. 서버와 수신 shot/jump/skill/slide/itemUse elapsed/duration, reactionSeconds/reactionDurationSeconds, 몬스터 actionSeconds 및 verticalSpeed/slideSpeed/projectile speed/age는 기존 전투 시간 단위를 유지합니다. 클라이언트의 로컬 지면 이동·높이 외삽·행동 표시와 원격 상태의 action-time 외삽에 이 변환을 한 번 적용합니다. 수신 좌표는 이미 느려진 결과이므로 위치 보간 비율·측정 이동 속도를 다시 줄이지 않습니다. 원격 플레이어 걷기/달리기 모션은 실제 시간×측정 이동 속도/기준 속도를 사용하며 템포를 다시 곱하지 않습니다. 걷기/달리기 속도 판별과 애니메이션 배율 경계도 실제 시간 단위에 맞춥니다. 몬스터는 이동 속도 배율을 별도로 곱하는 구조가 아니므로 서버 상태가 있을 때 모션 Update에 행동 delta만 적용합니다. 플레이어 스킬 본체·FX, 몬스터 공격 본체·궤적의 Seek에는 같은 actionSeconds를 사용하고 피격/낙하/기상·사망 모션 및 접촉 FX도 전투 시간으로 재생합니다. PNG·프레임 개수·원본 시간표를 수정하지 않습니다.

기본 walk280/run480은 실제 초당196/336, 슬라이딩 전투 속도1500과 지속0.4초는 실제 속도1050·지속 약0.571429초입니다. 거리 계수1.25·목표거리600·좌우 직선 규칙·피해100%는 유지합니다. 점프/투사체는 같은 물리 적분을 느린 전투 시간으로 수행하여 연속식의 점프 최고점·사거리와 완료 시 상대 이동 거리를 유지합니다. 서버가 보고한 점프 최고점 이산 오차 약2.8좌표는 기존 적분의 실행 검증 항목이며 물리 방식을 새로 설계하지 않았습니다.

역경직 data 시간은 서버가 시작 시/tempo로 실제 초로 바꾸고, hitstopStartTimeMs·serverTimeMs는 wall clock, hitstopRemainingSeconds·hitstopDurationSeconds는 실제 초입니다. 클라이언트 정지 구간 함수와 인접 구간 병합에는 tempo를 곱하거나 나누지 않습니다. 그 구간 밖의 실제 예측 age를 행동 시간으로 바꿉니다.0.05 전투 초 역경직은 실제 약0.071429초입니다. 로컬250ms/보간 외삽100ms 예측 한계는 실제 시간이며 계산에 쓰는 행동 age만 변환합니다. 슬라이딩 예측 확인 대기는 행동 지속을 실제 시간으로 환산한 뒤 기존0.5초 유예를 더하고, 방향 예측은 서버의0.25 전투 초 예약 창을 환산한 뒤 기존 wall 예측/틱 유예를 더합니다. 입력 커맨드 maxStepSeconds·완료 커맨드250ms 소비 창은 실제 시간 그대로입니다.

스킬 cooldown/buff 내부 진행은 서버 전투 시간이며 전송된 skillCooldowns·buff.remainingSeconds는 서버가/tempo한 실제 남은 초입니다. UI와 buff 표시 버퍼는 wall delta로만 차감합니다. 사용 승인 직후 임시 스킬 cooldown과 스킬/아이템 상세의 기본 cooldown도 수신 tempo로 환산해 snapshot 전후 단위를 맞춥니다. JSON/실시간 검증 상한은 스킬86400/최소tempo=864000초, 버프3600/최소tempo=36000초로 새 실제 초 단위에 맞춥니다. 아이템은 서버 reserve 단계에서 authored cooldown/tempo를 밀리초로 변환하고 기존 DB wall UTC deadline·1시간 상한을 따릅니다. 클라이언트가 받은 아이템 잔여시간/UTC 마감에 tempo를 다시 적용하지 않습니다.

정적으로 서버 데이터·단위 계약과 로컬 advection, actionSeconds 외삽, hitstop wall 구간, 원격 measured velocity, 모션/FX Seek, 실제 쿨다운 및 호출 경계를 대조합니다. 이전 UI6아이템/12스킬·키보드 행 배치·옵션/전체화면·슬라이딩 변경을 보존합니다. 빌드·실행·기능 테스트·프로세스 종료·커밋·푸시는 수행하지 않습니다. 실제 던전 체감·명중/궤적 정렬·피격/사망 전환·점프 이산 오차·buff 만료·아이템/스킬 사용 직후/로그아웃/재접속 cooldown 일치는 실행 미검증입니다. 새 tempo 월드 규칙을 전달하는 서버와 새 클라이언트를 함께 사용해야 합니다.

## 슬라이딩

던전 지상에서 실제 이동 중 달리기+X를 누르면 Slide(action=3)를 한 번 전송합니다. 기존 스킬 커맨드/단축키 처리 우선순위는 유지합니다. 입력에는 현재 mapEpoch·moveSequence·directionX/Y·running을 함께 넣어 같은 프레임 달리기 시작과 X를 서버가 동일하게 판정합니다. 사격 Prepare/Fire 중에는 서버가 Slide를 예약하지 않고 거절하며, 클라이언트도 해당 X를 일반 사격으로 바꾸지 않습니다. 이동이 허용되는 Recover와 콤보 입력 유예에서는 시작 시 기존 사격 예약·방향 고정을 정리한 뒤 새 슬라이딩 방향을 고정합니다.

월드의 combatRules.slideDefinitions는 characterId·attackPower·durationSeconds·distancePerRunSpeedSeconds·motionId·hitRecovery·hitstopSeconds·hitStartRatio·hitEndRatio를 전달합니다. 거리 계수 distancePerRunSpeedSeconds는 유한한 0.05~5.0초이며 필수입니다. 캐릭터 1~3의 motionId=slide를 공용 PlayerSlide에 연결합니다. 현재 기본 지속 시간은 서버 데이터의0.4초입니다. 시작 프레임에 좌우 입력이 있으면 그 부호로 바라보는 방향을 확정하고, 순수 상하 입력이면 현재 바라보는 좌우 방향을 사용합니다. 실제 슬라이딩 벡터는 X=±1·Y=0으로 고정하고 높이는 기존 지상 조건을 따릅니다. 대각·상하 달리기에서 시작해도 필드 깊이 방향으로 슬라이딩하지 않으며 진행 중 이동 입력으로 방향을 바꾸지 않습니다. 액션의 원래 directionX/Y는 moveSequence 검증 및 종료 후 유지 이동에 사용하므로 그대로 전송합니다. 클라이언트는 액션에 담긴 시작 facing을 예측에 넘기고 승인 시 서버 facing을 적용합니다. 시작 시 버프가 반영된 달리기 속도도 한 번 고정합니다. 총 목표 거리는 시작 시 버프가 반영된 달리기 속도×distancePerRunSpeedSeconds이고 슬라이딩 속도는 총거리/durationSeconds입니다. 2026-10-12 서버 조율값은 캐릭터1~3의 거리 계수1.25초·지속시간0.4초입니다. 기본 달리기 속도480 전투 단위에서는 총거리600·슬라이딩 속도1500으로 이전1200/3000의 각각50%입니다. 던전 tempo0.7에서는 목표거리600을 유지하며 실제 초당 속도1050·지속 약0.571429초입니다. 거리 계수만2.5→1.25로 바꾸고 별도0.5 속도 배율을 중복 적용하지 않습니다. 데이터 소유 경로는 서버 `GameRoomServer/Data/Combat.json`이며 클라이언트에 계수 사본이나 하드코딩을 추가하지 않습니다. 신규 서버 데이터가 보낸 월드 정의로 실제 예측값을 결정하므로 기존 실행 서버를 갱신하지 않으면 이전 계수가 남을 수 있습니다. 벽·게이트에서 서버가 중단하면 실제 이동 거리는 짧아질 수 있습니다. 클라이언트 예측도 수신한 계수를 사용하고 승인 후 서버 속도를 적용합니다. JSON·실시간 상태는 서버 데이터 상한에서 가능한 슬라이딩 속도 2000000까지 유한·비음수로 검증하며 패킷 버전 3과 필드 순서는 유지합니다. 피해는 캐릭터별 공격력에 시작 시 버프를 반영한 값의 100%로 서버가 계산하며, 클라이언트는 타격·피해량을 확정하지 않습니다.

공격 유효 구간은 서버가 보내는 [hitStartRatio, hitEndRatio)이며 클라이언트는 두 필드를 유한한 0 ≤ start < end ≤ 1로 검증합니다. 2026-10-16 기준 캐릭터 1~3은 [0.225, 0.5)로, 동작 시간 [0.09, 0.20)초와 공용 PlayerSlide의 3~4번째 프레임(0-based 2~3)에 대응합니다. 준비·마무리에는 피해를 판정하지 않습니다. 시작 직후 접촉 타격을 제거하고 서버가 틱 이동 경로 중 유효 시간과 겹치는 부분만 검사하며, 끝 경계에서 처음 접촉한 대상은 제외합니다. 이동 거리·전체 지속 시간·프레임 시간표는 유지하고, 본인/원격 모두 기존 slideSeconds 정규화 표시와 역경직 정지를 사용합니다. 클라이언트는 이 구간으로 피해를 자체 확정하지 않으며 서버의 타격 결과를 표시합니다. 신규 필드는 월드 JSON에만 추가하며 실시간 상태 v3의 필드 순서는 유지합니다. 이번 클라이언트와 서버 변경을 함께 적용해야 합니다.

본인은 지연 버퍼 없이 이동을 즉시 예측하고 거절·서버 상태로 보정합니다. 승인 전의 오래된 위치로 끌어당기지 않으며, 승인 상태의 facing·속도·지속 시간을 사용하고 로컬 예측/승인 후 진행/지연 위치 보정 모두 X축만 적분합니다. 서버 위치의 깊이 값은 권위 위치로 복원하며 추가 슬라이딩 깊이 이동은 만들지 않습니다. 기존 발 충돌·벽/경계 제약과 사망·클리어/맵 전환 정리 경로를 유지합니다. 마을의 현재 Player::Update는 일반 이동·사격 경로이고 슬라이딩 시작은 던전의 action3 경로입니다. 다른 플레이어는 기존 125ms 상태 버퍼에서 위치와 slideSeconds를 같은 표시 시각으로 맞춥니다. 동일 slideSequence의 반복 수신으로 애니메이션을 되감지 않으며, 원격 위치 외삽은 슬라이딩 종료를 넘지 않습니다.

8프레임 PlayerSlide는 frame_seconds_list=[0.045,0.045,0.04,0.07,0.06,0.055,0.045,0.04]로 총 0.4초를 배분합니다. 다리를 뻗은 4번째 프레임은 0.07초이며 이전 특수 보정의 0.225초 유지와 다릅니다. 로컬·원격 모두 공통 데이터 시간표를 사용하고 역경직 동안 자세 유지 시간도 멈춥니다. PNG·그리드·앵커·전체 이동시간과 거리 계산은 이 표시 배분으로 바꾸지 않습니다.

이번 속도/방향 수정은 원본 이동 증명과 v3 액션/상태 필드 순서를 유지합니다. 서버 접촉 피해100%·역경직0.05초·히트 리커버리 및0.4초 프레임 배분은 변경하지 않습니다. 정적 대조로 캐릭터별 데이터 값·서버/클라이언트 거리 식·시작 방향·X 전용 적분·기존 충돌 경로를 확인했습니다. 빌드·실행·기능 테스트는 하지 않았으며 실제 조작감·대각/상하 시작·동일 프레임 좌우 전환·Move/Action 역순 도착·충돌/버프/역경직 동작은 실행 미검증입니다.

슬라이딩 동안 일반 이동을 추가 적분하거나 사격·점프·스킬을 종료 후 실행하도록 예약하지 않습니다. 피격·사망·클리어·맵 epoch 변경·복귀·재도전·연결 리셋에서 예측과 상태를 정리합니다. 서버 확인 없이 예측을 유지하는 시간은 제한됩니다.

## Force·에어본 상태 연동

2026-10-13 확정 계약은 기존 rules/wire version3의 `KBK1` 확장입니다. 기본 총 수평 밀림 거리는 서버 데이터의 사격24·앞차기60·슬라이딩40·갑옷병 베기24·보스 베기48입니다. global `combatRules.knockbackSeconds`는0.01~1의 유한 combat초이며 기본0.18입니다. direct/projectile `execution.force`는0~500, `execution.hitType`은Normal/Airborne이고 누락은0/Normal로 정규화합니다. 명시한0을 보존하고 null·문자열·범위 밖 값·buff의 해당 필드는 거절합니다. 클라이언트 Catalog reader는 서버 Shared/PlayerSkillCatalog.h와 동일하며 정규화한 공용 계약을 대조합니다. hitType은 피격 대상의 반응이고 skillAirborne은 시전자의 공중 모드이므로 서로 바꾸지 않습니다.

KBK1은HUD1 뒤에 항상 붙는 little-endian 확장입니다. `KBK1` 4바이트, playerCount:u16와 플레이어 레코드들, monsterCount:u16와 몬스터 레코드들입니다. 레코드마다33바이트로 id:u64, knockbackSequence:u32(=reactionSequence), active:u8, directionX/directionY/distance/elapsedSeconds/durationSeconds:f32 순서이며 패딩이 없습니다. JSON은 knockbackSequence·knockbackActive·knockbackDirectionX/Y·knockbackDistance·knockbackElapsedSeconds·knockbackDurationSeconds로 같은 값을 전달합니다. 모든 현재 맵 actor를 비활성 상태까지 포함합니다. 파서는 카운트·ID 참조/중복·태그 중복·부울·유한 값·방향 단위 길이·거리/시간 범위·피격 번호 일치·비활성 숫자0을 검증합니다. 구서버의 확장 없는 v3 상태는 안전한 비활성으로 읽으며 일부 JSON 필드만 있는 상태는 거절합니다. 구클라이언트는 새 tail을 해석하지 못하므로 서버·클라이언트를 함께 배포해야 합니다.

서버가 HR로 줄인 거리/시간을 다시 줄이지 않습니다. 수신XY에서 누적 거리D(t)=distance*(2u-u²), u=clamp(t/duration,0,1)의 D(elapsed+wallAge*tempo)-D(elapsed)만 예측합니다. 본인 예측은 wall100ms 이하이고 duration·지상 피격 단계의 남은 시간을 넘지 않습니다. 피격 중 입력·슬라이딩 예측과 별개로 표시하고, 새 피격 번호는 기존 슬라이딩/방향 예측과 커맨드 이력을 취소합니다. 높은HR로 한 서버tick 안에 시작·종료되어 active=false와 새 번호만 받은 경우도 최종 권위XY를 반영합니다. 피격 해제·충돌 중단·새 번호 이후에 잔여 밀림을 일반 이동 보정으로 이어 붙이지 않습니다.

원격 플레이어·몬스터는 기존125ms 서버 상태 버퍼에서 피격 번호·위치·높이·동작 표시 시점을 맞춥니다. 다음 피격 번호의 좌표를 이전 일반 자세에서 미리 보간하지 않습니다. 최신 상태 이후의 수평 외삽은100ms와 서버 감속 곡선/남은 시간으로 제한하며, 비활성/피격 종료 상태에서는 이전 밀림의 측정 속도로 외삽하지 않습니다. 사망·클리어·방/맵 epoch·복귀/재도전·연결 리셋은 기존 버퍼 초기화와 본인 밀림 취소를 사용합니다. 기존 게임 스레드 전달을 유지하고 공유 가변 상태/네트워크 스레드를 추가하지 않습니다. PNG·프레임 시간표·실제 피해 판정은 변경하지 않습니다.

검증은 JSON/Binary 필드 순서·크기·범위와 서버/클라이언트 loader 동일성, 표시 호출·종료 경로, diff의 정적 검토만 진행했습니다. 빌드·실행·기능 테스트·DB·프로세스 종료·추가 커밋/푸시는 하지 않았습니다. 실제 고HR/지연·손실·역순 상황, 벽 충돌, 에어본 재피격, 사망/클리어/복귀/재도전 표시와 조작 체감은 실행 미검증입니다.

## 역경직과 히트 리커버리

역경직은 서버가 명중을 확정했을 때 공격자만 정지시키는 처리입니다. 현재 근접·슬라이딩 기본값은0.05 전투 초(tempo0.7의 실제 약0.071429초)이고 기본 사격은0초입니다. 피해자는 자신의 피격 반응만 수행합니다. 공격별 hitstopSeconds=0은 정지를 끕니다. direct/projectile execution에 선택적으로 넣으며 누락은 0으로 정규화하고 buff에는 넣지 않습니다. 새 수치는 유한·비음수·float 표현 범위와 양수의 0 변환 방지를 검증하며 별도의 1초 상한은 없습니다.

모든 캐릭터의 hitRecovery 기본값은 0입니다. 서버는 k=1/(1+hitRecovery/100)을 일반 경직 시간·수평 밀림 시간·수평 밀림 거리에 각각 적용합니다. 수직 에어본 높이·공중 재피격 즉시 낙하·다운/기상은 기존 규칙을 유지합니다. 클라이언트는 서버가 전달한 reactionDurationSeconds와 reactionSeconds로 피격 애니메이션을 맞추며 능력치를 다시 적용하지 않습니다. 마을의 기존 공통 피격 미리보기는 일반 경직에 같은 비율을 사용하며 이번 서버 Force 판정을 대신하지 않습니다.

hitstopStartTimeMs는 serverTimeMs와 같은 steady_clock이고 발생번호·전체길이는 자연 종료 후에도 유지됩니다. 반복 상태가 새 정지 타이머를 만들지 않습니다. 취소 시 더 큰 발생번호와 길이 0을 받아 이전 이벤트를 복원하지 않습니다. 같은 serverTick의 더 새로운 snapshotSequence를 허용하며, 서버는 역경직 변화가 있는 tick의 끝에도 상태를 전달합니다.

본인 이동·점프·사격·스킬·슬라이딩의 진행과 표시 시간은 정지 구간을 제외합니다. 슬라이딩 예측 대기 만료도 정지 시간을 소비하지 않습니다. 마지막 틱에 명중하면 slideSeconds가 전체 길이에 도달해도 서버 slideActive가 유지되고 역경직이 활성인 동안 마지막 슬라이딩 자세를 유지합니다. 정지 종료 또는 서버 비활성 상태에서 해제하며 추가 이동은 하지 않습니다. 원격 플레이어·몬스터의 위치·높이 보간, 측정 이동 속도와 동작 타이머도 같은 진행 시간에 맞춥니다. 중복·겹친 인접 정지 구간은 한 번만 제외합니다. 쿨타임·버프·UI·이미 발사된 탄환의 시간은 계속 흐릅니다. 정지 중 새 행동을 종료 후 실행하도록 예약하지 않습니다.

늦게 도착한 확정 상태의 정지가 서버 시각상 이미 종료되었다면 나중 행동에 정지를 다시 적용하지 않습니다. 로컬 명중·피해 예측은 추가하지 않았으며 네트워크 지연의 영향과 실제 체감은 실행 검증이 필요합니다. 방·epoch 전환, 사망·클리어·복귀·재도전·연결 초기화에는 기존 개체 및 이력 정리 경로를 사용합니다.

## 가변 프레임 시간과 몬스터 공격 이펙트

INI의 선택 키는 `frame_seconds_list`, 몬스터·스킬 본체·독립 FX JSON의 선택 키는 `frameDurationsSeconds`입니다. 배열은 index 순서대로 프레임 수와 같은 개수의 초 단위 값을 담습니다. 생략하면 기존 frame_seconds 또는 1/fps를 사용합니다. 각 값과 누적합은 유한해야 하며 양수·float 변환 후 양수·float 정밀도에서 누적 경계의 엄격한 증가·총시간 60초 이하를 검증합니다. Update·Seek·AdvanceOnce·GetDuration과 스킬 본체/FX의 프레임 선택이 같은 시간표를 사용합니다.

플레이어 스킬은 본체의 모든 프레임 경계가 공유 기능 데이터와 일치해야 합니다. 이벤트는 T[eventFrame], 직접 공격의 유효 시간은 [T[index], T[index+1])입니다. 초기 콘텐츠의 총 행동시간·기존 타격 경계·조작 가능 시점은 유지합니다. 몬스터 공격의 index 4 시작은 갑옷병 0.285714초, 수호자 0.4초이고 총시간은 각각 0.571429초·0.8초입니다.

`Assets/Data/MonsterAttackEffects.json`은 별도 `Assets/Images/Effects/Monsters/animations.json`과 PNG를 연결합니다. eventFrame은 본체 타격 기준이며 startOffsetSeconds로 그보다 먼저 궤적을 시작할 수 있습니다. FX의 impactFrame 시작이 본체 타격 시각과 일치하고 FX가 공격 종료 전 만료하는지 로드 시 검사합니다. 병사는 시작 오프셋 -0.04초·6프레임·0.20초, 보스는 -0.05초·8프레임·0.32초이며 두 FX의 impactFrame은 2입니다.

FX는 피해 성공 표시나 추가 판정이 아닌 휘두르기 궤적입니다. 몸체와 분리된 비반복 시트로 같은 게임 스레드에서 표시하며, 맵 epoch·몬스터 instanceId·actionSequence별로 한 번만 시작합니다. 위치·높이·방향을 첫 표시 시 고정하고 역경직을 제외한 공격 actionSeconds로 Seek합니다. 늦은 상태는 중간 프레임부터 표시하고 만료했으면 건너뜁니다. 피격·취소·사망·삭제·클리어·맵 전환·복귀/재도전·연결 리셋에서 정리하며, 같은 공격의 반복 수신이나 역경직 sequence 변경으로 다시 생성하지 않습니다.

## 스킬과 성장 상태

앞차기의 현재 ID는 `Character1.FrontKick`, 입력은 Up 다음 Z, 단계 간 최대 0.35초입니다. 지상 전용이며 해당 캐릭터가 습득하고 쿨타임이 끝나야 합니다. unavailable 커맨드도 즉시 소비하여 나중에 자동 발동하거나 마지막 X/C 입력이 기본 사격/점프로 이어지지 않게 합니다.

스킬 학습·강화는 TownServer가 처리하고 던전 예약/입장 중에는 허용하지 않습니다. 던전에서는 스킬 창 조회와 습득 아이콘의 단축키 등록을 허용합니다. 서버 권한·캐릭터별 소유·지상/공중 허용·시전 상태를 확인한 뒤 요청하며, 슬롯 파일이 실행 권한을 만들지 않습니다.

실시간 SKL1 확장은 캐릭터·시전 번호·스킬·시간·버프·투사체 정보를 포함하지만 진행값·쿨타임은 포함하지 않습니다. reliable JSON이 이를 보완합니다. 서버 수락 시 쿨타임을 표시하고 JSON 갱신을 요청하며, GameWorld는 별도의 최신 tick/accepted sequence 기준으로 쿨타임을 적용합니다. 오래된 JSON 위치로 최신 실시간 자세를 덮어쓰지 않고, 룸 JSON의 진행값으로 최신 타운 학습 권한을 덮어쓰지도 않습니다.

## 패킷·전달 주기

전투 규칙·상태 JSON과 실시간 요청/응답/chunk의 version은 3입니다. ID 7 행동 입력은 version:u16, sequence:u32, action:u8, facingLeft:u8, mapEpoch:u32, moveSequence:u32, directionX:i8, directionY:i8, running:u8 순서로 19바이트입니다. ID 8 응답은 version:u16, sequence:u32, accepted:u8, serverTick:u64 순서로 15바이트입니다. 이 크기는 패킷 헤더를 제외한 필드 합계입니다.

실시간 공통 actor의 reactionSequence 뒤에는 hitRecovery:f32, reactionDurationSeconds:f32, hitstopRemainingSeconds:f32, hitstopSequence:u32, hitstopStartTimeMs:u64, hitstopDurationSeconds:f32를 추가합니다(28바이트). 플레이어 기본 레코드는 슬라이딩 25바이트를 포함해 129바이트입니다. 기존 SKL1 확장과 ID 7/8의 필드 순서는 유지하며 버전 1·2로 읽지 않습니다. 던전 world.version=1, 맵 version=5, PlayerSkills.schemaVersion=1은 유지합니다.

아이템 사용 표시(2026-10-09): 기본 투사체 레코드와 SKL1 순서는 유지하고 IUS1/ITP1 확장을 추가합니다. 파서는 태그를 반복 소비하고 중복·알 수 없는 태그, 잘린 데이터, 중복/미등록 참조 ID와 잘못된 수치를 거절합니다. IUS1은 playerCount:u16 뒤에 playerId:u64, itemUseSequence:u32, itemUseDefinitionId:text16, itemUseMotionId:text16, elapsed:f32, duration:f32를 전달합니다. ITP1은 projectileCount:u16 뒤에 projectileId:u64, itemDefinitionId:text16, visual:text16을 전달합니다. text16은 u16 길이와 UTF-8 본문이며 모든 수치는 기존 little-endian입니다. JSON도 같은 actor/투사체 필드를 읽고 최신 실시간 상태를 오래된 JSON으로 되돌리지 않습니다.

돌멩이는 서버 직선 투사체(speed 600/range 480/radius 6/forward 20/height 64/피해 10)이며 Stone 표시에서 기존 PlayerRock 색상·하이라이트만 재사용합니다. 마을의 로컬 생성·포물선 계산·피해 경로를 호출하지 않습니다. 별도 itemUseSequence/elapsed로 AttackFire 3프레임(0.025+0.045+0.08=0.15초)을 Seek하며 shotPhase/발사 이벤트와 분리합니다. 원격 동작 시간은 기존 서버 시간 보간에 맞춰 진행하고 동일 순서 번호의 반복 수신으로 되감지 않습니다. 사망·피격·슬라이딩 표시가 우선합니다. 첫 프레임의 총구 섬광은 기존 PNG에 포함되어 있어 재사용 시 함께 보이며, 이 작업에서 PNG를 수정하지 않았습니다.

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

실시간 구독은 월드 수신 뒤 version=3·enabled=1·현재 challenge로 요청합니다. 구독 결과의 roomId·dungeonId·version·challenge를 확인합니다. 첫 상태 조각은 reliable 구독 결과보다 먼저 도착할 수 있습니다.

현재 서버 목표는 **시뮬레이션 30Hz, 상태 전달 15Hz**입니다. 안내 필드는 `tickIntervalMs=33`, `snapshotIntervalMs=67`이지만 실제 tick dt는 1/30초입니다. 클라이언트는 tick 안내값 33~50ms와 전달 간격 50~100ms를 확인합니다. 33ms를 실제 dt로 누적하지 않습니다.

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

## 접촉 이펙트

등록된 플레이어 스킬은 `Character1.FrontKick`이며 발차기 자체로 동작이 읽혀 별도 시전 궤적은 추가하지 않습니다. 기본 사격의 총구·탄환, 몬스터 두 공격의 기존 궤적은 유지합니다. 실제 피해 시에는 공통 `CombatHitSpark` 6프레임을 0.18초 동안 표시합니다. 최종 시트는 `Assets/Images/Effects/combat_hit_spark.png`이며 `assets.ini`와 `animations.ini`에서 연결합니다.

서버에서 실제 피해에만 증가하는 `reactionSequence`로 한 번 재생합니다. 로컬 플레이어는 수신 상태, 원격 플레이어·몬스터는 보간된 피격 동작을 따릅니다. 보간 HP는 먼저 갱신되므로 원격 효과를 HP 감소와 같은 프레임에만 요구하지 않습니다. 첫 관측·중복 상태·방/맵/epoch 변경은 효과를 재생하지 않으며, 맵 이동·던전 종료 때 지웁니다. 게임 스레드에서 최대 64개를 관리합니다. 접촉 좌표를 전송하는 계약은 없어 몸통 기준 위치를 사용하며, 손실 구간에 여러 타격이 합쳐지면 관측된 반응당 한 번 표시합니다.

## 클리어·복귀·재도전

서버 cleared 상태를 처음 받으면 게임 입력과 기존 UI를 차단하고 마을 이동·재도전 선택 창을 표시합니다. 방향키/Enter 또는 클릭으로 선택하며, 솔로 또는 파티장만 요청할 수 있고 파티원은 파티장 선택을 기다립니다.

`DungeonCompletionRequest(roomId,retry)`는 타운으로 보내고 응답 전 중복 요청을 막습니다. 30초 지연이나 연결 문제가 있으면 대기 상태를 해제해 안내합니다. 성공 응답에서는 기존 RUDP의 `RequestStop`을 호출하고 매 tick 종료 완료를 확인합니다. 그 동안 타운 이벤트는 보류합니다.

종료 완료 후 dungeon world·개체·보간·탄환·커맨드·action/move sequence·쿨타임 표시·방향 예측을 초기화합니다. 복귀는 타운 맵/위치를 적용하고, 재도전은 응답의 새 roomId/브로커로 challenge와 월드 인증부터 다시 진행합니다. 이전 연결·방·epoch의 버퍼가 새 던전에 섞이지 않게 합니다. 로그아웃·계정/타운 전환도 기존 Game을 폐기하고 연결을 정리합니다.

## 데이터·검증 범위

마을 점프 속도는 캐릭터별 `characters.ini`에서 읽고, 던전 높이·속도·중력·동작 시간은 월드 combatRules를 그대로 사용합니다. 몬스터 시트는 종류별 sourceRect/pivot/fps/scaleToMovement 또는 renderSize를 사용합니다. UseSkill의 `attack`은 해당 몬스터 공격 시트로 연결합니다. 플레이어 별도 사망 시트는 없어 쓰러짐 마지막 자세로 표시합니다.

코드 근거: [GameWorld](ActionRPGClient/Game/GameWorld.cpp), [DungeonClient](ActionRPGClient/Network/DungeonClient.cpp), [DungeonCombat](ActionRPGClient/Game/DungeonCombat.cpp), [Character](ActionRPGClient/Game/Character.cpp), [Monster](ActionRPGClient/Game/Monster.cpp), [Player](ActionRPGClient/Game/Player.cpp), [SkillUi](ActionRPGClient/Game/SkillUi.cpp).

이번에는 문서·생성 패킷·서버 계약을 정적으로 확인했습니다. 이전 Debug 빌드 성공은 실제 플레이 검증과 다릅니다. 손실·지연·역순·중복, 500ms 이상 중단, 워프/재방문·텔레포트·생성/삭제·사망·보스 클리어·파티 복귀/재도전·연결 리셋·창 종료는 추가 실행 확인이 필요합니다. 드랍·보상 지급·부활 등 서버 미구현 범위는 서버 문서를 따릅니다.

## HUD와 서버 확정 명중

2026-10-11: 캐릭터·몬스터의 머리 위 HP 표시를 제거했습니다. 상단은 로컬 플레이어의 서버 확정 lastHitSequence/lastHitMonsterId가 지목한 몬스터 하나만 표시합니다. 서버는 실제 HP 감소를 낸 명중만 방 단위 u64 순서로 기록합니다. 공격 입력·타깃 선택·다른 플레이어 공격으로 표시 대상을 바꾸지 않습니다. 이름·레벨·isBoss는 world/전투 상태의 실제 데이터이며, 레벨 미제공은 Lv.—입니다. HP 비율은 현재/최대×100을 소수 둘째 자리까지 표시하고 누적 HP 배율을 사용하지 않습니다. 상단 프레임은 좌상단(12,12)의 한 줄 가로형입니다. 우상단 UI용 320px와 간격12px를 비워 두며 논리 1280/1920에서 폭936/1576px입니다. 일반은 높이44px·철색2px 프레임과 녹색(#4C9A63) HP, 보스는 높이48px·황동색3px 외곽/1px 내부 프레임과 붉은색(#BE2434) HP로 구분합니다. 왼쪽 초상화(일반36/보스40px), 간격8px, 보스 BOSS 배지, 한 줄 Lv/이름, 오른쪽 현재 HP / 최대 HP와 aa.bb%를 표시합니다. BOSS는 서버 isBoss의 표시이며 별도 등급 수치를 만들지 않습니다. HP 숫자186px·퍼센트82px 영역을 따로 확보하고 긴 이름은 이름 영역에서 잘라 숫자와 겹치지 않게 합니다. 던전 조작 안내/상태는 y72/y106으로, 사망/클리어 및 거절 안내는 y148/y188로 옮겨 HP바와 겹치지 않게 합니다.

초상화는 [Assets/Data/monsters.json](Assets/Data/monsters.json)의 선택적 portrait 객체에 image(Assets 기준 상대 경로)와 sourceRect{x,y,width,height}(원본 PNG 픽셀)를 지정합니다. 현재 Dummy·갑옷병·성채 수호자 세 항목 모두 기존 PNG 크롭으로 연결되어 새 PNG를 만들지 않습니다. MonsterCatalog는 기존 수치/경로 검사와 실제 파일 확인을 사용하고 GameWorld는 이미지별 한 번 디코드한 뒤 실제 픽셀 경계를 확인합니다. x/y는 0 이상, width/height는 양수이며 유한하고 이미지 범위 안이어야 합니다. 원본 픽셀 영역을 비트맵 DIP 좌표로 변환하되 표시 종횡비는 원본 픽셀 기준으로 보존합니다. portrait 필드가 없으면 빈 초상화 프레임을 유지하며 다른 몬스터 사진을 대신 사용하지 않습니다. 잘못된 필드·파일·범위는 기존 자산 로딩 오류로 처리합니다. 최종 DrawBitmap은 서버 대상의 dataId로 찾은 초상화 캐시를 사용하며 C++에 몬스터별 크롭 좌표를 하드코딩하지 않습니다.

HUD1은 기존 실시간 v3 base/SKL1/IUS1/ITP1 이후의 little-endian 확장입니다. playerCount:u16와 각 playerId:u64, mpKnown:u8, mp:u32, maxMp:u32, lastHitSequence:u64, lastHitMonsterId:u64, 이어 monsterCount:u16와 각 instanceId:u64, levelKnown:u8, level:u32, isBoss:u8, name:text16을 읽습니다. text16은 u16 UTF-8 바이트 길이입니다. 미제공 숫자는 known=false와 0으로만 처리합니다. JSON의 mp/maxMp·몬스터 level은 nullable입니다. 파서는 수·참조 ID·중복·잘린 데이터·부울과 수치 범위를 검사합니다.

HUD는 위치 보간과 분리된 최신 권위 상태를 사용합니다. 보간 버퍼도 플레이어 MP/명중과 몬스터 이름·레벨·보스 정보를 최신 상태로 복원합니다. 오래된 JSON을 최신 RT 위치·명중에 적용하지 않습니다. 사망·대상 제거·클리어·맵/epoch 전환은 대상을 숨기며 이전 살아 있는 대상을 다시 선택하지 않습니다. 같은 방의 map 경계에서는 그때 수신된 명중 순서까지 소비하고 새 명중을 기다립니다. 새 방·복귀·재도전·연결 초기화에서는 대상과 방 단위 순서를 초기화합니다.

HP 손실은 회색 구간으로 즉시 반영하고 0.2초 유지 후 초당 전체 비율 0.6씩 오른쪽부터 검은 빈 공간으로 줄어듭니다. 프레임 delta에 따라 유지/감소 시간을 분리해 적용합니다. 회복 시 회색 비율은 현재 HP 이상으로 보정하고, 최대 HP 변경·새 대상은 현재 비율로 초기화합니다.

2026-10-12 최신 정정 반영: 사용자 원본 이미지의 하단 구조에 맞춰 아이템6칸(1~6) → 붉은 HP 구체 → 스킬6열×2행(12칸) → 푸른 MP 구체 → 메뉴/ESC 순서로 배치합니다. 키보드 배열처럼 윗줄은 Q/W/E/R/T/Y, 아랫줄은 A/S/D/F/G/H입니다. 기존 키별 스킬 배정은 보존하며 두 행의 화면 위치만 바꿉니다. Q/W/E/R/T/Y는 현재 이동·Z/X/V 공격·C 점프·숫자 아이템·Enter/ESC와 충돌하지 않습니다. 아이템/스킬 키는 이동·공격 커맨드 이력에 삽입하지 않습니다. 숫자 키5·6도 기존 instanceId 사용 경로에 연결하며 모든12개 스킬 칸은 드래그 등록·해제·스킬 ID 사용·서버 쿨타임·hover 설명을 공유합니다. 현재 스킬 카탈로그에 정의/습득한 스킬만 등록할 수 있고 슬롯 수 증가만으로 새 스킬을 만들지 않습니다. 스킬 저장은 schemaVersion2·12칸이며 기존 version1·6칸 파일은 A/S/D/F/G/H의 기존 저장 인덱스로 읽어 아랫줄에 표시하고 Q/W/E/R/T/Y 배정은 비웁니다. 다음 실제 배정 저장 때12칸 형식으로 기록합니다. 아이템은 기존 캐릭터별 세션 메모리 정책을 유지합니다. 패킷은 스킬ID/instanceId로 보내므로 UI 슬롯 수나 키를 서버 계약에 추가하지 않습니다.

1280×720 기준 슬롯40×40px·아이콘34px·내부 여백3px·칸 간격4px·키/쿨타임 글자11px로 키웠습니다. 스킬바268×92px, 아이템260×40px, 구체 외경92px, 메뉴96×40px이며 전체 하단은840×92px, 아래 여백8px입니다. 어두운 갈색 바탕(#17130D), 검은 외곽·얇은 황동 선(#74603D)·상단 하이라이트(#AE9460)·작은 금색 키 표시로 원본 프레임 표현을 유지합니다. 기존 커다란 공통 직사각형 배경은 사용하지 않습니다. 비대칭 아이템·메뉴 폭까지 고려하여 전체 HUD를 중앙에 놓습니다.

`HudLayout::Scale`은 viewportWidth/1280입니다. 1280에서는1.0,1920×1080에서는1.5를 사용하여 슬롯60px·아이콘51px·글자16.5px, 스킬바402×138px, 아이템390×60px, 구체138px, 메뉴144×60px, 전체1260×138px 및 아래 여백12px로 표시합니다. HUD·스킬 창·인벤토리의 패널/아이콘/글씨/테두리/툴팁/드래그는 같은 렌더 변환으로 확대합니다. 포인터는 GameWindow에서 물리/letterbox 좌표를 논리 좌표로 바꾼 뒤 각 UI 진입점에서 공통 배율의 역수를 한 번 적용합니다. 같은 기준 사각형을 클릭·우클릭·hover·드롭에 사용합니다. `D2DRenderer::UiScaleScope`는 게임/렌더 스레드에서만 잠시 변환을 바꾸고 RAII로 이전 letterbox 변환을 복원하며 네트워크 스레드는 접근하지 않습니다.

구체 프레임은 투명96×96 `Images/UI/hud_resource_orb_frame.png`를 HP·MP 공통으로 사용하고, 안쪽 색과 회색 손실 구간은 서버 자원 비율로 그립니다. 92px 표시의61.3px 투명 구멍 안에 반경30px 채움을 넣으며 1920에서는 함께1.5배 적용합니다. 중앙 aa.bb%·미제공 표시와 hover의 현재/최대 숫자를 유지합니다. 스킬 창 기준760×420px(1920은1140×630px), 노드80×80px·간격14px, 트리/상세/드래그 아이콘34px도 동일 배율입니다. 상세 본문14px는1920에서21px로 보입니다. 아이템·스택·입력 정책은 [TOWN_NETWORK.md](TOWN_NETWORK.md)의 HUD 절을 따릅니다. 상단 보스·일반 몬스터 HP 렌더링은 이번 슬롯 확장에서 변경하지 않았습니다. 두 해상도의 배치·입력·소스/데이터 연결을 정적으로 확인하며 실제 게임 표시·가독성·추가키 사용은 실행 미검증입니다.

이번 작업은 소스·JSON·PNG 헤더 경계·두 해상도의 배치와 diff 정적 검토만 진행했습니다. 세 초상화의 JSON 좌표가 해당 실제 PNG 안에 있는 것을 확인했습니다. 빌드·기능 테스트·프로그램 실행·DB 적용·커밋·푸시는 수행하지 않았습니다. 실제 게임의 초상화 식별성·텍스트 정렬·피해/회복 애니메이션은 실행 검증이 필요합니다.

## 게임 옵션과 고정 해상도

2026-10-12: ESC 시스템 메뉴의 게임 옵션(440×350px)에서 해상도1280×720·1920×1080 및 창모드·전체화면을 선택하고 화면 적용을 누릅니다. ↑↓로 다섯 항목을 선택하고 ←→로 값을 바꿉니다. 마스터·효과음·배경음악은 게임 전용 XAudio2 mastering/submix voice의 실제 gain을0~100%,5% 단위로 즉시 조절합니다. 시스템/다른 프로그램의 볼륨은 바꾸지 않습니다. 설정은 Application 수명 동안 유지하고 로그아웃은 재생만 중단합니다. 지원 음원·등록·소비 경로는 [Assets/Audio/README.md](Assets/Audio/README.md)를 따릅니다. 하단·스킬·인벤토리는 위40px/34px 기준 및1920의 공통1.5배 규격을 따릅니다. 해상도는 창 테두리를 제외한 논리 게임 화면 크기입니다. 창모드의 실제 클라이언트 영역은 선택한 픽셀 크기로 유지하며, 크기 조절 테두리·최대화 버튼·제목줄 더블 클릭과 크기 조절/최대화 명령을 막습니다. 최소화·닫기는 유지합니다. 설정은 실행 중 유지하며 별도 저장 파일은 추가하지 않습니다.

전체화면은 독점 모드가 아닌 현재 모니터의 테두리 없는 창입니다. 물리 렌더링 대상은 실제 클라이언트 영역 크기이며, 게임·로그인·HUD는 선택한 논리 해상도로 배치합니다. min(실제 폭/논리 폭, 실제 높이/논리 높이)의 동일 배율과 중앙 여백을 렌더링·포인터 역변환에 함께 적용합니다. 논리 화면 밖은 검정으로 비우고 잘라 표시하며 여백의 포인터는 UI 밖 좌표로 처리합니다.

선택한 클라이언트 크기와 해당 DPI의 창 테두리까지 모니터 작업 영역에 들어가지 않으면 창모드 선택·적용을 차단하고 전체화면을 안내합니다. 시작 시 또는 모니터/DPI/화면 구성 변경으로 기존 창모드가 들어가지 않게 되면 논리 해상도를 유지한 전체화면으로 전환합니다. DPI·모니터 변경도 게임 업데이트 사이에서 적용하고, 최소화 중에는 적용을 보류하며 0 크기를 렌더러로 보내지 않습니다. 전환 때 누적 입력·키 상태·드래그·명령 큐를 정리합니다. 화면 설정은 게임/창 스레드에서만 접근하며 네트워크 수신 흐름은 유지합니다.

창 모드 전환의 스타일 교체는 기존 WS_VISIBLE/WS_DISABLED를 보존합니다. 전체화면 WM_WINDOWPOSCHANGING은 실제 제안 사각형이 대상 모니터 경계와 다를 때만 재적용을 예약합니다. WM_SIZE는 실제 물리 크기 변경을 예약하며 같은 버퍼 크기는 ResizeBuffers를 생략합니다. 논리 해상도 변경, 모드 변경, 둘의 동시 변경을 Application에서 별도로 판별합니다. 논리 변화만 있을 때는 viewport·게임 UI를 갱신하고, 물리 영역이 바뀔 때만 버퍼를 바꾸며 모드만 바뀌었다면 게임의 논리 Resize를 반복하지 않습니다.

정적으로 확인한 종료 경로는 GraphicsDevice의 HRESULT 실패 → runtime_error → wWinMain의 오류 처리/종료입니다. resize는 프레임 EndDraw 이후에만 처리하므로 타깃 해제 뒤 추가했던 D2D Flush를 제거하고, 타깃/비트맵 참조 해제 및 D3D ClearState/Flush 후 기존 버퍼 수·포맷·flags를 보존해 ResizeBuffers를 호출합니다. 새 타깃은 생성 성공 뒤 바인딩합니다. 실패 시 이전 버퍼 타깃을 복구하고 Application은 이전 창 설정·물리 버퍼·논리 viewport를 함께 복구한 뒤 오류를 보여 줍니다. 타깃이 누락됐으면 같은 크기에서도 타깃 생성은 생략하지 않습니다. 복구가 실패한 경우에는 원래 오류와 복구 오류를 함께 최상위 처리로 전달합니다.

사용자 증상은1920×1080 전체화면 진입 직후 오류 창 없이 종료입니다. 최근7일 Windows Application 오류 기록과 기존 클라이언트 로그 후보에서 해당 종료를 특정할 기록을 찾지 못했습니다. 따라서 추가 Flush의 실제 반환값, 네이티브 예외 또는 정상 종료 중 어느 경로인지는 아직 확정하지 않습니다. Visual Studio 출력에 `ActionRPGClient: Screen change`의 변경 종류·전후 논리 크기·모드·물리 크기, ResizeBuffers 시작/완료와 HRESULT 실패를 남깁니다. 정상 메뉴 종료와 WM_CLOSE/WM_QUIT도 구분합니다. 디버거가 없는 실행에서 처리되지 않은 네이티브 예외는 코드·주소 안내 후 Windows의 원래 종료 처리로 넘기며 게임 상태를 복구하려 하지 않습니다. 디버거가 예외를 먼저 받는 경우 Visual Studio의 중단 위치·예외 코드와 마지막 출력 문구를 확인합니다. 이 진단은 모든 강제 종료·fast-fail을 포착한다고 보장하지 않습니다. 실제 재현·원인 확정·해결 검증은 수행하지 않았습니다.

정적 검토: 창 스타일과 게임 옵션 Action 연결, 두 해상도 제한, 논리/물리 Resize 분리, 포인터·클립의 공통 배율 및 최소화/DPI 분기, 옵션 표시/클릭 사각형을 대조했습니다. 오디오 파서는 RIFF/chunk 경계·길이·PCM 정렬과 형식을 검사하며 버퍼는 source voice가 파괴될 때까지 보유합니다. callback 없이 게임 스레드에서 완료 voice를 정리합니다. 빌드·실행·기능 테스트는 수행하지 않았습니다. 두 해상도/모드 전환, 모니터 비율과 DPI 혼합, Snap/최대화 시도, 최소화·복원 및 클릭 정렬은 실행 검증이 필요합니다.
