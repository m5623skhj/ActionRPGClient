# PlayerSkills 계약 v1

공용 루트는 `format=PlayerSkills`, `schemaVersion=1`, `coordinates=groundXY-height-facingRight-worldUnits`이며 `characters`, `skills` 배열을 갖습니다.
characters의 `{id: CharacterN, dataId: N}`은 characters.ini에서 읽습니다. 몬스터 ID와 스킬 원본 INI의 오라 정의를 혼용하지 않습니다.
스킬 ID는 영문으로 시작하고 영문/숫자/`_.-` 64자 이내이며 고유합니다.

각 스킬은 `id`, `name`, `characterId`, `type`, `input`, `cooldownSeconds`, `execution`, `ground`, `air`를 갖습니다.
input은 `command` 문자열 배열과 `maxStepSeconds`입니다. type은 direct/projectile/buff입니다.
ground/air는 null(불허)이거나 모드별 정의입니다.

모드의 공통 필드는 `motionId`, `frameCount`, `fps`, `durationSeconds`, `eventFrame`, `endFrame`입니다.
모션과 공용 모드에는 선택 frameDurationsSeconds 배열을 지정할 수 있습니다. 프레임 index 순서로 정확히 frameCount개이며 배열이 FPS보다 우선합니다. 누락된 기존 데이터는 각 프레임을 1/fps로 해석합니다.
T[0]=0, T[i+1]=T[i]+frameDurationsSeconds[i]이며 durationSeconds는 T[frameCount]와 0.0001초 이내로 일치해야 합니다. 프레임은 0 기반, 실행 시작은 T[eventFrame], direct의 각 attackRects 구간은 [T[index],T[index+1])입니다. 마지막 활성 시간은 T[endFrame+1]이며 한 시전의 중복 타격은 금지합니다.
각 시간은 유한한 양수이고 float 변환 후에도 유한한 양수여야 합니다. 누적 경계는 double과 float 누적 모두 엄격히 증가하며 유한해야 합니다. 본체·독립 이펙트 총시간은 공통 재생기와 같은 최대 60초이며 스킬 본체는 최소 0.001초입니다. 잘못된 길이·null·0·음수·시간 합계 불일치는 거절합니다.
공용 PlayerSkills와 표시 motion의 명시 시간 배열은 동일하게 출력합니다. 기존 schemaVersion 1을 유지하지만 가변 시간을 적용하려면 양쪽 로더·런타임을 함께 갱신해야 합니다.

| 유형 | execution | 모드별 추가 필드 |
|---|---|---|
| direct | damage, depthRadius, hitstopSeconds(선택) | attackRects: `{index, rect:{x,y,width,height}}` 배열 |
| projectile | damage, speed, radius, range, hitstopSeconds(선택) | spawn:{x,y,height}, yawDegrees, pitchDegrees |
| buff | target:self, stat:damageMultiplier 또는 movementMultiplier, multiplier, durationSeconds, refresh:replaceDuration | 없음 |

`PlayerSkills.schemaVersion`과 작업 schemaVersion은 1을 유지합니다.
`execution.hitstopSeconds`는 direct/projectile의 선택 필드이며 단위는 초입니다. 누락은 0으로 정규화하고 명시한 0은 보존합니다.
편집기의 새 direct 기본값은 0.05, 새 projectile은 0입니다. 기존 작업의 누락을 새 direct 기본값으로 바꾸지 않습니다.
불러온 기존 작업은 다음 저장에 0을 포함하며, 공용 JSON 출력은 두 공격 유형에 이 필드를 명시합니다. buff에는 허용하지 않습니다.

수치는 유한·비음수이며 IEEE754 binary32로 변환해 유한한 값이어야 합니다.
float 최대값 3.4028234663852886e38을 넘거나, 양수인데 float 변환 후 0이 되는 값은 거절합니다. 임의의 게임 시간 상한은 없습니다.
null·문자열을 누락으로 취급하지 않습니다. 적용 대상은 명중한 공격자이며 대상의 경직이나 버프 지속시간이 아닙니다.
`hitRecovery`는 일반 경직만 단축하는 별도 공통 캐릭터 능력치이고, PlayerSkills나 이 편집기의 출력 필드에 추가하지 않습니다.

공격 rect의 x는 오른쪽 기준 전방 X, y는 캐릭터 발 기준 아래쪽 경계의 높이, width/height는 양수 월드 크기입니다.
서버에서 왼쪽 rect의 X 구간은 [-x-width, -x], 높이 구간은 [actorHeight+y, actorHeight+y+height]입니다.
타깃의 현재 bodyHeight/hitRadius와 지면 Y 깊이를 함께 검사합니다. 픽셀 좌표 또는 편집기 줌을 저장하지 않습니다.

투사체 방향벡터는 yaw/pitch를 라디안으로 변환하여 다음과 같이 계산합니다.

```text
dx = facingSign * cos(yaw) * cos(pitch)
dy = sin(yaw) * cos(pitch)
dh = sin(pitch)
velocity = direction * speed
spawnWorld = (actorX + facingSign*spawn.x, actorY + spawn.y, actorHeight + spawn.height)
```

클라이언트 전용 루트는 `format=PlayerSkillVisuals`, `schemaVersion=1`, `skills` 배열입니다.
각 항목은 id/characterId/ground/air입니다. 각 모드는 `{motion, effect}`이며 motion은 CharacterEditor의 검증된 프레임 정의입니다.
effect는 null 또는 `{motion,eventFrame,offset:{x,y,height},loop}`입니다. PNG 바이트는 JSON이 아닌 Assets 상대 경로의 파일로 출력합니다.
독립 effect.motion에도 같은 frameDurationsSeconds 규칙을 적용합니다. 이펙트 시작 시간은 본체 T[eventFrame], 이펙트 프레임은 이펙트 자신의 누적 시간표로 선택합니다.
서버와 클라이언트 공용 JSON은 동일해야 하며, 표시 motion의 frameCount/fps와 실제 시간표는 공용 모드와 일치해야 합니다.
공용 JSON에는 이미지 경로와 바이트가 없습니다. 공용 C++ 로더는 알 수 없는 필드와 버전, 비정상 숫자·참조·범위를 거절합니다.

## 네트워크

Tool/PacketDefine.yml의 ID 14 DungeonSkillInput: sequence:uint32, skillId:string, facingLeft:uint8.
기존 액션과 같은 sequence 및 DungeonActionResult를 사용하고 세션의 20회/초 제한을 공유합니다.
서버는 스킬 소유 캐릭터와 습득 레벨, 지상/공중 조건, 반응·사망·쿨다운을 검사합니다. 마을→룸 ConfirmJoin에 characterId:uint32를 추가하여 신원을 전달합니다.
TownServer와 룸 서버 모두 해당 소스로 다시 빌드해야 합니다. 구버전 바이너리와 새 ConfirmJoin을 혼합하지 않습니다.

기존 실시간 v1의 actor/projectile 레코드 순서는 유지합니다. 스킬 카탈로그가 비어 있지 않으면 다음 tail을 덧붙입니다.

```text
magic: ASCII SKL1 (4 bytes)
playerCount: uint16
각 플레이어: id:uint64, characterId:uint32, skillSequence:uint32,
  skillId:text, active:uint8, airborne:uint8, seconds:float, movementMultiplier:float, buffCount:uint8
  각 버프: skillId:text, remainingSeconds:float
projectileCount:uint16
각 투사체: id:uint64, skillId:text, directionY:float, speed:float, radius:float, ageSeconds:float
```

정수는 little-endian, float는 IEEE754 binary32이며 text는 기존 uint16 길이의 UTF-8입니다.
목록 개수와 ID는 기본 레코드와 정확히 일치해야 합니다. frame 48KB/플레이어 4/투사체 256/버프 16 제한을 유지합니다.
JSON 복구 스냅샷에도 동일한 캐릭터·스킬·버프·투사체 필드를 추가합니다.
skillId/skillSequence/seconds는 마지막 시전 정보를 보존하며 active만 현재 실행 여부입니다. 짧은 시전 종료 뒤에도 일회성 이펙트를 남은 시간 동안 표시할 수 있습니다.
이 버전의 클라이언트는 tail 유무를 모두 읽습니다. 스킬을 설치한 서버에 구버전 클라이언트로 접속하면 확장을 읽을 수 없으므로 양쪽 바이너리도 함께 갱신해야 합니다.

## 별도 성장 계약: SkillTrees v1

PlayerSkills는 레벨 1의 실행 정의이며 편집기 출력에 성장·습득 정보를 추가하지 않습니다.
현재 서버 Shared/SkillTreeCatalog.h는 별도 `format=SkillTrees`, `schemaVersion=1`,
`skillLevelInterval`(1~1,000,000), `nodes`(최대 256개)를 읽습니다. 파일은 필수이고 최대 1MB입니다.

| 노드 필드 | 계약 |
|---|---|
| skillId | PlayerSkills에 존재하는 고유 ID; 모든 스킬에 정확히 하나의 노드 필요 |
| requiredLevel / spCost | 1~1,000,000 정수; 첫 습득 레벨 / 매 습득 SP 비용 |
| maxSkillLevel | null 또는 1~1,000,000 정수; null이어도 내부 학습 레벨 상한은 1,000,000 |
| prerequisites | `{skillId,skillLevel}` 배열; 같은 캐릭터의 등록 스킬·양수 레벨, 중복·자기 참조·순환 금지 |
| description / icon | 비어 있지 않은 설명(UTF-8 최대 2048바이트) / Images/로 시작하는 PNG Assets 상대 경로 |
| column / row | 0~255 정수; 같은 캐릭터의 같은 칸 중복 금지 |
| damagePerLevel | 0~1,000,000 정수; 공격 스킬의 레벨당 추가 피해 |

```text
필요 캐릭터 레벨 = requiredLevel + (새 스킬 레벨 - 1) * skillLevelInterval
공격 피해 = PlayerSkills.execution.damage + (습득 스킬 레벨 - 1) * damagePerLevel
```

서버는 이 공격 피해에 활성 버프의 피해 배율을 적용합니다. 버프의 multiplier/duration은
현재 스킬 레벨별로 자동 증가하지 않습니다. 습득은 요구 레벨·SP·선행 스킬·최대 레벨·현재 레벨을 검증합니다.
클라이언트는 서버의 성장 상태와 트리를 수신하므로 이 편집기 출력에 임의의 level/prerequisites 필드를 넣지 않습니다.
캐릭터의 저장된 성장 상태와 정의의 소유 캐릭터·레벨·선행 조건도 일치해야 합니다.

## 작업 저장과 런타임 출력

PlayerSkillEditorProject schemaVersion 1은 characters/animations/skills/hurtRects/images를 갖는 편집용 스냅샷입니다.
이미지 data URL과 미완성 설정을 저장할 수 있으나 Build가 실패하면 런타임 ZIP은 출력하지 않습니다. 시간 배열은 작업 저장·불러오기에서 보존하며, 불러오기 시 배열의 길이·수치·합계를 검사합니다. FPS 변경은 명시 배열을 바꾸지 않고 전체 균등화 버튼만 1/fps로 덮어씁니다.
PlayerSkills와 PlayerSkillVisuals는 작업 열기 형식이 아니며 별도 원본 모션과 작업 파일을 보존해야 합니다.
PNG 시전 이펙트는 클라이언트 전용이고, 타격 피해·버프와 이미지 재생은 서로 다른 실행 정보입니다.
습득 노드·아이콘 및 실행 디렉터리 설치는 ZIP의 범위 밖입니다.

확인 근거: [model.js](model.js), [editor.js](editor.js), 서버 Shared/PlayerSkillCatalog.h,
Shared/SkillTreeCatalog.h와 GameRoomSkills.cpp. 소스 대조일은 2026-10-06이며 게임 실행 검증은 하지 않았습니다.
