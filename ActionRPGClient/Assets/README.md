# 클라이언트 Assets 안내

2026-10-06 현재 런타임의 데이터·이미지 경로와 확장 지점을 설명합니다. 편집기 파일 형식은 해당 도구 문서에서 관리합니다. 클라이언트 에셋 원본은 **이 Assets 폴더**이며 서버 저장소에 클라이언트 이미지나 설정 사본을 만들지 않습니다.

## 경로와 실행 폴더

[AssetCatalog](../ActionRPGClient/Resources/AssetCatalog.cpp)는 실행 파일 위치의 `Assets/Data/assets.ini`를 읽습니다. 작업 디렉터리는 기준이 아닙니다. 논리적 데이터·이미지·오디오 ID는 assets.ini의 각 섹션에서 Assets 기준 상대 경로로 해석합니다. 일반 에셋 경로는 절대 경로와 `..`를 거부합니다.

원본은 `ActionRPGClient/ActionRPGClient/Assets`가 아니라 저장소의 `ActionRPGClient/Assets`입니다. 빌드 후 런타임 복사본은 `ActionRPGClient/artifacts/bin/x64/<Configuration>/Assets`에 있습니다. 이 경로들은 저장소 루트를 기준으로 합니다.

| 파일/디렉터리 | 용도·소비 위치 |
| --- | --- |
| [Data/assets.ini](Data/assets.ini) | 논리 ID와 데이터·미디어 상대 경로 연결 |
| [Data/AuthClient.json](Data/AuthClient.json) | AuthSettings가 기동 시 직접 읽는 인증·타운 연결 설정. assets.ini 데이터 ID와 별개 |
| [Data/characters.ini](Data/characters.ini) | Character/원격 마을 플레이어의 캐릭터별 점프 속도·대기/걷기 섹션 |
| [Data/animations.ini](Data/animations.ini) | 기본 스프라이트 셀·프레임·재생 시간·표시 크기·anchor |
| [Data/PlayerSkills.json](Data/PlayerSkills.json) | 서버와 공유하는 캐릭터·스킬 ID, 입력·쿨타임·행동 프레임/공격 계약 |
| [Data/PlayerSkillVisuals.json](Data/PlayerSkillVisuals.json) | 클라이언트 전용 스킬 모션·이펙트 이미지, sourceRect/pivot·표시 크기 |
| [Data/SkillUi.json](Data/SkillUi.json) | 스킬 트리/상세·슬롯 레이아웃, 최소 크기·작은 창 배치·폰트·색 |
| [Data/monsters.json](Data/monsters.json) | 몬스터 dataId·이름·기본 시트/동작 메타데이터·표시 높이 |
| [Images/Monsters/animations.json](Images/Monsters/animations.json) | 몬스터별 프레임·모션·sourceRect/pivot/fps |
| [Data/projectiles.ini](Data/projectiles.ini) | 마을 로컬 총알·공중 총알·투척물 표시/이동 |
| [Data/skills.ini](Data/skills.ini), [Data/effects.ini](Data/effects.ini) | 기존 SkillCommandSystem 예시 데이터. 현재 PlayerSkills 커맨드 실행과 구분 |
| [Data/system_menu.ini](Data/system_menu.ini) | Esc 메뉴 순서·라벨·아이콘·Action |
| `Images/Characters`, `Images/UI`, `Images/Effects` | 캐릭터 시트, 메뉴/스킬 아이콘, 효과 |
| `Images/Towns`, `Images/Dungeons` | 서버 맵의 배경/오브젝트 asset 상대 경로에 대응하는 로컬 이미지 |
| `Audio/` | 오디오 파일 배치와 경로 등록. 현재 게임 재생 경로는 연결되어 있지 않음 |

마을·던전 맵 JSON은 서버가 읽고 배치·이동 영역을 네트워크로 보냅니다. 배경 이미지 자체는 전송하지 않습니다. `images[].asset`은 Assets 상대 경로를 직접 사용하므로 모든 배경을 assets.ini의 Images 논리 ID로 등록할 필요는 없습니다.

던전 에디터가 출력하는 미니맵 파일과 게임 HUD 구현은 구분합니다. 현재 클라이언트 런타임의 DungeonWorld/GameWorld/Game에는 미니맵 데이터 소비·HUD 그리기 경로가 없습니다. 맵/워프·몬스터·전투 표시는 구현되어 있습니다.

## ID와 서버 공유 데이터

| 종류 | 현재 ID |
| --- | --- |
| 캐릭터 종류 | Character1/dataId 1, Character2/2, Character3/3 |
| 플레이어 스킬 | Character1.FrontKick |
| 몬스터 | 1 Dummy, 2 rusted_armor_soldier, 3 fallen_citadel_warden |

캐릭터 종류 ID는 EnterTownResponse/PlayerAppear와 스킬 카탈로그에서 연결합니다. `characters.ini`의 Default/Character1~3은 현재 같은 대기·걷기 시트를 사용합니다. 종류가 3개라고 서로 다른 기본 공격 이미지가 자동 선택되는 것은 아니며 Player의 공통 CharacterAnimationSet 연결도 확인해야 합니다.

플레이어 스킬 카탈로그의 format은 PlayerSkills, schemaVersion은 1입니다. 앞차기는 Up 다음 Z, 최대 간격 0.35초·쿨타임 2초·ground 모션 8프레임 16fps, air는 null입니다. 입력 정의만 존재한다고 즉시 사용할 수 있는 것은 아닙니다. TownServer의 학습 진행값과 스킬 트리 권한을 받아야 합니다.

서버 [TownServer/Data/PlayerSkills.json](../../../ActionRPGServer/ActionRPGServer/TownServer/Data/PlayerSkills.json)·[GameRoomServer/Data/PlayerSkills.json](../../../ActionRPGServer/ActionRPGServer/GameRoomServer/Data/PlayerSkills.json)과 클라이언트 카탈로그의 내용이 같아야 합니다. 로더는 Town 스킬 상태와 Dungeon 월드에서 일치를 검사합니다. 공유 계약 로더도 서버 Shared와 클라이언트 Game 양쪽을 함께 갱신합니다.

스킬 트리·초기 레벨·SP·레벨당 지급 정책은 서버 `SkillTrees.json/CharacterProgression.json`이 원본이고 클라이언트는 Town의 SkillStateResponse로 받습니다. 클라이언트 폴더에 별도 정책 사본을 추가하지 않습니다. 학습 결과·피해·보상·캐릭터 영속 저장은 서버/DB의 책임입니다.

## 기본 애니메이션과 기준점

[SpriteAnimation](../ActionRPGClient/Resources/SpriteAnimation.cpp)은 INI의 columns/rows/frame_count/frame_seconds와 render_width/render_height를 사용합니다. 프레임은 왼쪽에서 오른쪽, 위에서 아래 순서입니다. 같은 셀 크기라도 실제 인물의 크기·발 위치가 다를 수 있으므로 원본 해상도만으로 표시 크기를 통일하지 않습니다.

- anchor_y는 지면에 맞출 셀의 세로 기준점(0~1)입니다.
- second_row_anchor_y는 두 번째 행의 별도 기준점입니다.
- anchor_xs는 프레임별 가로 기준점입니다. frame_count와 개수를 맞춥니다.
- first_row_ratio는 2행 시트의 행 분리 비율입니다.
- 사격 event_frame은 로컬 동작에서 한 발 발생 시점을 지정합니다. INI event_frame과 JSON eventFrame의 인덱스 규칙은 각각의 로더/도구 형식을 따릅니다.

현재 PlayerIdle은 192×192, PlayerWalk는 192×198, PlayerRun은 224×224, 지상 사격은 202×202로 설정되어 있습니다. 다른 시트를 추가할 때 이전의 모든 동작 192×192 가정을 복사하지 않습니다. 걷기는 anchor_xs와 행별 anchor를 사용합니다.

기본 지상 사격은 start/fire/end, 공중 사격도 start/fire/end로 분리합니다. 점프는 start/hold/land, 피격은 hit/air-hit-start/fall/knockdown/get-up을 사용합니다. 원본 통합 시트가 있어도 실제 런타임은 분리 섹션을 사용할 수 있습니다. 최대 사격 횟수·입력 유예·방향 고정과 던전 서버 타이밍은 [전투 문서](../COMBAT_CLIENT.md)를 참고합니다.

PlayerSlide는 [player_slide.png](Images/Characters/player_slide.png)를 사용합니다. 원본 2048×1024, 셀 512×512, 4열×2행의 8프레임을 행 우선으로 읽고 248×248로 표시합니다. frame_seconds=0.05로 기본 재생은 0.4초이며, 던전에서는 서버 캐릭터별 durationSeconds에 맞춰 경과 시간을 비례 적용하고 단발 재생합니다. 캐릭터 1~3은 공용 PlayerSlide를 사용합니다.

발 기준점은 셀의 (208,488)입니다. anchor_xs의 8개 값은 모두 0.40625, 두 행의 anchor_y는 0.953125입니다. 기존 SpriteAnimation은 좌우 반전 때 가로 기준점도 1-anchor로 반사하므로 캐릭터 지면 좌표가 유지됩니다. 동일 슬라이딩 순번의 반복 상태 수신은 재생을 처음으로 되돌리지 않습니다.

## 캐릭터별 점프·피격

`characters.ini`의 Default/Character1~3은 현재 `jump_speed=690`입니다. 마을의 초기 상승 속도로 사용하며 ID가 확정된 입장 응답에서 적용합니다. 던전은 서버 combatRules.jumpSpeed·gravity·height/verticalSpeed를 적용합니다. 마을 값을 던전 서버 값에 다시 곱하지 않습니다.

Character의 로컬 피격 진입점과 GameWorld::QueuePlayerHit는 존재하지만, 던전은 서버 HP·reaction·sequence를 적용합니다. 실제 서버 피격 생산부가 없다는 과거 설명은 현재 구현과 다릅니다. 게임 스레드 외부에서 직접 Character를 수정하지 않습니다.

피격·사망은 일반 행동보다 우선하고 예약·커맨드를 정리합니다. 동일 반응 sequence의 수신은 같은 동작을 되감지 않습니다. 현재 플레이어 사망 전용 시트는 없어 knockdown 마지막 자세를 사용합니다. 몬스터 사망 표시와 필요한 다른 모션은 몬스터 메타데이터에 연결합니다.

CharacterEditor의 피격 사각형 출력은 이미지 편집 데이터입니다. 현재 서버 플레이어 스킬 타깃 판정은 bodyHeight/hitRadius를 사용하며 CharacterHurtRects 런타임 로더 연결과 혼동하지 않습니다.

## 스킬 모션과 몬스터

PlayerSkillVisuals의 각 항목은 공유 skillId·characterId·ground/air 존재 여부가 같아야 합니다. 모션 frameCount/fps도 공유 타임라인과 맞춥니다. 이미지 크기, sourceRect 범위, pivot, 프레임 인덱스·개수와 로드 예산을 검사합니다. 이미지 경로는 Assets 상대 경로입니다.

JSON 모션은 프레임별 sourceRect/pivot를 사용하고 renderSize 또는 scaleToMovement로 표시합니다. pivot는 잘린 sourceRect 안의 로컬 좌표이며 좌우 반전 시 기준점도 반전합니다. 프레임마다 전체 캔버스 중심으로 그리면 발 위치가 흔들리므로 원본 pivot를 유지합니다.

몬스터 dataId는 서버 월드 spawn·AI/Combat 데이터와 클라이언트 monsters.json에 일치해야 합니다. 종류 1은 기존 DummyIdle 섹션, 2/3은 Images/Monsters/animations.json의 동작을 사용합니다. UseSkill의 animationId=attack은 해당 종류의 공격 모션으로 연결합니다. 메타데이터·이미지를 바꾸어도 서버 AI와 피해 수치가 자동 변경되지 않습니다.

던전 이동 보간은 표시 위치·높이·동작 시각을 맞추며 원격 Player 걷기/달리기 재생에 속도 배율을 적용합니다. 몬스터 걷기 시트는 현재 메타데이터 fps로 재생하므로 실제 속도 변화 시 발 미끄러짐은 별도 확인 항목입니다.

## 메뉴·스킬 UI·사용자 저장

메뉴 섹션 순서가 화면 순서입니다. 현재 Action은 Party, Skills, SelectTown, SwitchAccount, Logout, Exit입니다. Icon은 assets.ini의 Images 논리 ID이며 아이콘은 고정 표시 크기로 그립니다. 메뉴가 높이를 넘으면 휠로 스크롤합니다. 새 Action은 코드 처리와 설정을 함께 연결해야 합니다.

SkillUi.json은 최소 640×360과 compact/stacked 배치, 슬롯·노드·상세 패널·폰트·색·드래그 임계값·습득 응답 대기를 정의합니다. 스킬 아이콘은 서버 트리의 icon 상대 경로를 사용합니다. 누락 아이콘은 읽을 수 있는 placeholder로 처리하지만 게임 권한을 새로 만들지는 않습니다.

단축키 저장 위치는 `%LOCALAPPDATA%/ActionRPGClient/skill-slots-Character<ID>.json`입니다. 6개 슬롯 A/S/D/F/G/H, 습득한 자기 캐릭터 스킬만 허용하며 동일 스킬 중복 등록을 제거합니다. 프로세스별 임시 파일을 쓴 뒤 교체하고 저장 실패는 안내합니다. 저장은 종류별 UI 설정이며 계정별 캐릭터 DB 저장과 다릅니다.

## 원본 반영·확장 순서

1. 실제 소비 프로젝트와 ID를 확인하고 이 Assets 원본을 수정합니다. 편집기 사용은 [CharacterEditor](../CharacterEditor/README.md), [SkillEditor](../SkillEditor/README.md), [MonsterEditor](../MonsterEditor/README.md), [DungeonEditor](../DungeonEditor/README.md), [TownMapEditor](../TOWN_MAP_EDITOR.md)를 따릅니다.
2. 공유 스킬·몬스터/맵 참조가 바뀌면 서버 데이터·계약과 함께 맞춥니다. 시각 데이터만 변경한 경우에도 sourceRect/pivot·실제 이미지 크기를 확인합니다.
3. 데이터·이미지는 빌드의 CopyRuntimeAssets 또는 [SyncClientAssets.bat](../SyncClientAssets.bat)으로 실행 폴더에 반영합니다. Sync는 /MIR로 삭제까지 반영하며 Debug/Release x64별로 실행합니다.
4. RUDP 코어 옵션 원본은 [ActionRPGClient/ClientOptionFile/CoreOption.txt](../ActionRPGClient/ClientOptionFile/CoreOption.txt)입니다. CopyClientOptions가 실행 파일 옆에 복사합니다. 브로커 주소·포트는 서버 입장 응답에서 받고 런타임 임시 옵션을 정리합니다.
5. 실행 중 캐시와 기동 시 Auth 설정이 자동 갱신된다고 가정하지 말고 재시작합니다. 소스/패킷 변경은 빌드·양쪽 배포가 필요합니다.

일반 빌드 Copy는 변경 파일을 복사하지만 원본에서 삭제한 실행 폴더 파일까지 정리하는 mirror 작업은 아닙니다. 에셋 삭제가 포함되면 Sync의 삭제 반영 동작을 확인합니다. 서버 데이터 설치는 클라이언트 Sync로 수행되지 않습니다.

## 확인 범위

이번 문서는 로더·INI/JSON·프로젝트 복사 대상·현재 서버 계약을 정적으로 대조했습니다. 이미지를 생성·편집하거나 실행 에셋을 복사하지 않았고 빌드·게임 실행도 하지 않았습니다. 새 미디어·보고서·설정 사본은 만들지 않았습니다.

현재 화면의 Game::Render 조작 안내에는 과거 Left/Right x2 + Z 문자열이 남아 있습니다. 실제 스킬 입력은 PlayerSkills.json과 PlayerSkillPresentation을 기준으로 확인해야 합니다. 미니맵 HUD·오디오 재생·CharacterHurtRects 서버 연결과 실제 인증/전투 체감은 구현 상태와 실행 확인을 나누어 관리합니다.
