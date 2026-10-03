# PlayerSkills 계약 v1

공용 루트는 `format=PlayerSkills`, `schemaVersion=1`, `coordinates=groundXY-height-facingRight-worldUnits`이며 `characters`, `skills` 배열을 갖습니다.
characters의 `{id: CharacterN, dataId: N}`은 characters.ini에서 읽습니다. 몬스터 ID와 스킬 원본 INI의 오라 정의를 혼용하지 않습니다.
스킬 ID는 영문으로 시작하고 영문/숫자/`_.-` 64자 이내이며 고유합니다.

각 스킬은 `id`, `name`, `characterId`, `type`, `input`, `cooldownSeconds`, `execution`, `ground`, `air`를 갖습니다.
input은 `command` 문자열 배열과 `maxStepSeconds`입니다. type은 direct/projectile/buff입니다.
ground/air는 null(불허)이거나 모드별 정의입니다.

모드의 공통 필드는 `motionId`, `frameCount`, `fps`, `durationSeconds`, `eventFrame`, `endFrame`입니다.
durationSeconds는 frameCount/fps와 일치해야 합니다. 프레임은 0 기반, 시작 시간은 eventFrame/fps입니다.
direct의 마지막 활성 시간은 (endFrame+1)/fps이며 한 시전의 중복 타격은 금지합니다.

| 유형 | execution | 모드별 추가 필드 |
|---|---|---|
| direct | damage, depthRadius | attackRects: `{index, rect:{x,y,width,height}}` 배열 |
| projectile | damage, speed, radius, range | spawn:{x,y,height}, yawDegrees, pitchDegrees |
| buff | target:self, stat:damageMultiplier 또는 movementMultiplier, multiplier, durationSeconds, refresh:replaceDuration | 없음 |

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
서버와 클라이언트 공용 JSON은 동일해야 하며, 표시 motion의 frameCount/fps는 공용 모드와 일치해야 합니다.
공용 JSON에는 이미지 경로와 바이트가 없습니다. 공용 C++ 로더는 알 수 없는 필드와 버전, 비정상 숫자·참조·범위를 거절합니다.

## 네트워크

Tool/PacketDefine.yml에 ID 14 DungeonSkillInput을 추가합니다: sequence:uint32, skillId:string, facingLeft:uint8.
기존 액션과 같은 sequence 및 DungeonActionResult를 사용하고 세션의 20회/초 제한을 공유합니다.
서버는 스킬 소유 캐릭터와 지상/공중 조건, 반응·사망·쿨다운을 검사합니다. 마을→룸 ConfirmJoin에 characterId:uint32를 추가하여 신원을 전달합니다.
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
