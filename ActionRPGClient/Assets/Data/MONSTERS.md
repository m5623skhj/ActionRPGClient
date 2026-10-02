# 클라이언트 몬스터 표시 데이터

`assets.ini`의 `Monsters` 항목으로 `monsters.json`을 읽습니다.
서버의 숫자 Data ID와 `monsterId`를 맞춰 등록합니다. 현재 1 Dummy,
2 rusted_armor_soldier, 3 fallen_citadel_warden입니다. 미등록 ID는 던전 로딩 오류입니다.

## 등록

- Dummy는 기존 `animations.ini`의 `idleAnimationSection`을 사용합니다.
- 일반 몬스터는 `monsterId`, `renderHeight`, `metadata`, `idle`을 등록합니다.
- `metadata`와 `idle.image`는 Assets 기준 상대 경로입니다.
- 메타데이터는 version 1, `characters[monsterId].motions`의 move/hit/airborne/attack/death를 사용합니다.
- 각 모션의 `sourceRect`, sourceRect 내부 픽셀 좌표인 `pivot`, fps, loop,
  holdLastFrame, scaleToMovement를 사용합니다. 시트를 격자로 나누지 않습니다.
- 이동 기준 배율은 `renderHeight / referenceStandingHeight`이며,
  다른 모션에는 `scaleToMovement`를 곱합니다. 프레임마다 너비와 높이를 그대로 비례 확대합니다.
- idle은 단일 PNG의 `sourceRect`, `pivot`, `standingHeight`를 별도로 지정합니다.

현재 일반/보스 높이 200/330은 임시 표시값입니다. Idle 경계는 알파 16 초과 픽셀에
4픽셀 여백을 두었고 pivot은 초기 권장 접지점입니다. 게임에서 최종 크기와 접지점을
확인해야 합니다. PNG와 원본 모션 메타데이터는 변경하지 않았습니다.

## 재생과 서버 연동 경계

`Monster::PlayMotion`은 같은 모션도 처음부터 재생합니다.
`ApplyPresentationState(motion, position, facingLeft, height)`는 위치·방향·높이를 적용하고
모션이 바뀔 때만 재생을 초기화합니다. 같은 동작을 다시 시작하는 이벤트에는
`PlayMotion`을 별도로 호출해야 합니다. 모든 호출은 게임 스레드에서 수행합니다.

- Idle/Move: 반복. Hit/Attack/GetUp: 한 번 재생한 뒤 Idle로 복귀.
- Attack: 원본 8프레임을 준비→공격→회수 순서로 재생합니다. 클라이언트가
  타격 판정·총알·피해를 생성하지 않습니다. 권장 타격 프레임은 서버 연동용 메타데이터입니다.
- AirborneLaunch: launch 구간 다음 airHold 자세를 유지합니다.
- AirborneHold/AirborneFall/Knockdown: 해당 구간의 마지막 자세를 유지합니다.
  단계 전환과 공중 높이는 외부의 권위 상태가 결정합니다. 모션 시간으로 낙하를 추정하지 않습니다.
- Death: 한 번 재생 후 마지막 프레임 유지. 다른 모션으로 전환하지 않습니다.
  개체 초기화는 `ResetActionState`를 통해 별도로 수행합니다.
- HP 0으로 생성된 개체는 Death로 시작합니다.

전투 스냅샷 패킷은 개체 ID, 위치·방향·HP·높이·피격·AI 행동 상태를 보냅니다.
`Monster::ApplyCombatState`가 HP/피격을 우선하여 표시 모션을 선택합니다.
UseSkill의 animationId `attack`은 종류별 공격 클립에 대응합니다.
같은 공격은 actionSeconds로 프레임을 맞추고, aiNodeId/행동 시간 초기화로 재시작을 판별합니다.
사망은 개체당 한 번 시작하고 스냅샷 반복 수신으로 재시작하지 않습니다.
몬스터는 Character의 좌표·높이·방향을 재사용하지만 플레이어 사격 컨트롤러
`UpdateActions`를 호출하지 않습니다. 몬스터 갱신은 `Update`를 사용합니다.

템플릿은 Data ID별로 한 번 로드하며, 같은 시트의 이미지 자원은 공유합니다.
개체의 모션·프레임·재생 시간은 독립적입니다. 기존 Assets 복사 경로로 배포되므로
이 파일과 `monsters.json`을 반영하려고 프로젝트 파일을 수정할 필요는 없습니다.

이 변경에서는 빌드·프로그램 실행·기능 테스트를 수행하지 않았습니다.
