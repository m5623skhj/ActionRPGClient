# 플레이어 스킬 편집기

플레이어 캐릭터의 신규 스킬과 기존 작업을 편집하는 로컬 도구입니다. 몬스터 스킬과 AI는 다루지 않습니다.

## 빠른 시작과 문서 안내

아래 프로젝트·실행 파일 경로는 `ActionRPGClient.slnx`가 있는 클라이언트 프로젝트 폴더 기준입니다.

| 항목 | 안내 |
|---|---|
| 바로 열기 | [index.html](index.html)을 Edge/Chrome에서 열기; CharacterEditor/model.js와 DungeonEditor/archive.js를 함께 유지 |
| Windows 런처 | [SkillEditor.vcxproj](SkillEditor.vcxproj), `artifacts/bin/x64/Debug/SkillEditor.exe` (Release도 같은 구조) |
| 입력 | 플레이어 데이터 폴더·원본 모션/이미지; 편집 재개는 PlayerSkillEditorProject 작업 JSON |
| 작업 흐름 | 플레이어 자료 불러오기 → 스킬 생성·편집 → 작업 저장·불러오기 → 검사 → 런타임 ZIP 출력 → 성장 데이터 정합성 확인·별도 설치 |
| 상세 계약 | [FORMAT.md](FORMAT.md): PlayerSkills/PlayerSkillVisuals v1, 좌표·프레임·네트워크, 별도 SkillTrees v1 |

검사·ZIP 출력은 승인 기록이나 게임 플레이 검증을 만들지 않습니다.
스킬 트리·레벨·선행 조건·아이콘은 이 편집기의 출력 범위 밖이며, 아래 실제 적용 순서에 따라 함께 준비해야 합니다.
캐릭터 피격 편집은 [CharacterEditor 안내](../CharacterEditor/README.md)를 따릅니다.


## 시작

`SkillEditor/index.html`을 Edge/Chrome에서 열거나 Visual Studio의 `SkillEditor/SkillEditor.vcxproj` 프로젝트를 사용합니다.
빌드 시 `artifacts/bin/x64/Debug/SkillEditor.exe` 또는 Release 경로에 런처와 도구 파일이 생성됩니다.
이 문서에서 프로젝트·실행 파일 경로는 ActionRPGClient.slnx가 있는 클라이언트 프로젝트 폴더 기준입니다.
소스 HTML은 같은 상위 폴더의 `CharacterEditor/model.js`, `DungeonEditor/archive.js`가 필요합니다.
런처 빌드 시 이 의존 파일도 복사합니다. 편집기는 네트워크 접근 없이 선택한 파일만 읽습니다.

1. **플레이어 데이터 폴더**에서 클라이언트 프로젝트 폴더 또는 Assets 폴더를 선택합니다.
2. `characters.ini`의 `CharacterN`과 숫자 Data ID N을 읽고, 기존 CharacterEditor 변환기로 모션과 실제 이미지를 불러옵니다. `Default`는 런타임 기본 표시이며 스킬 소유자로 선택하지 않습니다.
3. 필요한 경우 **animations.json**으로 같은 플레이어 ID의 모션을 추가·교체합니다. 같은 모션 ID는 새 정의를 사용하고 다른 기존 모션은 유지합니다. 몬스터 ID는 가져오지 않습니다. 새 모션 자체를 제작하는 도구는 아닙니다.
4. **새 스킬**에서 고유 ID를 입력하고 이름, 유형, 입력 커맨드와 쿨다운을 설정합니다.
5. 지상/공중 모드를 각각 활성화하고 모션, 실행 프레임과 유형별 설정을 편집합니다.
6. 작업을 저장하고 검사한 다음 런타임 ZIP을 출력합니다.

커맨드는 `Right Right Z`처럼 공백으로 구분합니다. 긴 커맨드를 우선 처리하며 길이가 같으면 스킬 ID 순입니다.
입력 이력은 기존 InputCommandQueue의 1.5초 범위를 사용합니다. 같은 캐릭터의 같은 커맨드는 중복 등록하지 않습니다.
지상/공중 모션은 참조한 전체 모션을 한 번 재생합니다. 프레임은 0 기반이며 실행 시점은 프레임/FPS로 계산합니다.

## 직접 공격

현재 프레임에서 빈 공간을 드래그하면 공격 사각형을 그립니다. 내부 드래그로 이동하고 모서리로 크기를 조절합니다.
수치 적용, 영역 삭제, 활성 구간 전체에 복사를 지원합니다. 시작~종료 프레임에 각각 영역이 있어야 출력합니다.
사각형은 월드 X와 높이의 단면입니다. 지면 Y 판정 반경은 별도로 설정합니다.
공격 영역은 이미지 바깥에도 놓을 수 있습니다. 기존 피격 영역과 다른 데이터이며 수정한 피격 JSON을 출력하지 않습니다.
**피격 JSON 참조**는 CharacterEditor 원본 모션과 대조 후 파란색으로 표시합니다.
서버의 대상 피격 판정은 기존 bodyHeight/hitRadius를 사용합니다. CharacterHurtRects의 런타임 연결을 추가한 것은 아닙니다.

하나의 시전은 같은 몬스터를 최대 한 번 타격합니다. 여러 대상 타격은 가능하지만 PvP, 연타·다중 타격 이벤트는 첫 버전 범위에 없습니다.
서버는 시뮬레이션 틱보다 짧은 활성 프레임도 겹치는 시간 구간을 검사합니다. 지면의 막힌 구간을 가로질러 타격하지 않습니다.

## 투사체 공격

속도는 월드 단위/초의 **총속도**입니다. 발사 위치는 캐릭터 기준 X, 지면 Y, 높이로 설정합니다.
X는 오른쪽을 바라볼 때의 앞뒤 거리입니다. 왼쪽에서는 X 위치와 X 방향만 반전하고 지면 Y와 높이는 유지합니다.
방향은 지면 방향각(yaw)과 위아래 각도(pitch)이며 양수 pitch는 위로, -45°는 아래로 발사합니다.
캔버스 클릭/드래그는 X와 높이만 바꿉니다. 지면 Y는 숫자로 편집합니다. 표시 경로는 지면 Y를 화면 아래 방향으로 투영합니다.
최대 이동거리는 X/Y/높이를 합한 실제 경로 길이이며 충돌 반경은 월드 단위입니다.
한 시전은 투사체 하나를 생성합니다. 최대 거리, 지면 아래, 지도 장애물 또는 첫 몬스터 충돌에서 제거합니다.
투사체는 현재 원형으로 표시합니다. PNG 이펙트는 아래의 캐릭터 시전 연출에 연결됩니다.

## 버프

첫 버전은 자기 자신에게 피해 배율 또는 이동속도 배율을 적용합니다. 같은 스킬은 지속시간을 갱신합니다.
서로 다른 버프는 능력치별 가장 높은 배율 하나를 사용합니다. 활성 버프는 캐릭터당 최대 16개이며 초과 시 서버가 거절합니다.
사망/퇴장 시 제거하고 맵 이동은 남은 지속시간을 유지합니다. 쿨다운은 시전 승인 시 시작하고 피격으로 취소되어도 돌려주지 않습니다.
피격과 공중 스킬의 착지는 진행 중 모션을 취소합니다. 이미 적용된 버프는 사망 전까지 시간 만료 규칙을 따릅니다.
투사체 피해 배율은 발사 시, 직접 공격 피해 배율은 타격 시 확정합니다. 버프에는 투사체 필드가 없습니다.

## PNG 이펙트

모드별 투명 PNG 시트를 연결하고 열/행, 프레임 수, FPS, 월드 배율, 기준점 비율, 시작 프레임과 상대 위치를 설정합니다.
새 그래픽을 생성하지 않고 사용자가 선택한 PNG를 사용합니다. 프레임은 왼쪽에서 오른쪽, 위에서 아래 순서입니다.
이펙트는 캐릭터를 따라가며 좌우 반전합니다. 일회성 이펙트는 시전 모션이 먼저 끝나도 마지막 프레임까지 표시합니다. 반복은 시전 모션 동안만 적용하고 버프 지속시간 동안 별도로 반복하지 않습니다.
오디오, 월드에 고정하는 이펙트와 발사체 PNG는 이번 UI에 없습니다.

## 저장과 출력

작업 JSON은 `PlayerSkillEditorProject` schemaVersion 1로 원본 애니메이션, 스킬, 읽기 전용 피격 참조와 이미지 data URL을 포함합니다.
미완성 작업도 저장할 수 있습니다. 파일 쓰기 API가 있으면 직접 저장하며 없으면 다운로드합니다. 다운로드 완료를 확인할 수 없으므로 미저장 경고를 유지합니다.
실행 취소/다시 실행은 최근 스킬 변경 30회를 보존합니다. 원본 모션 교체 후에는 이력을 초기화합니다.

런타임 ZIP의 구조:

```text
server/Data/PlayerSkills.json
client/Assets/Data/PlayerSkills.json
client/Assets/Data/PlayerSkillVisuals.json
client/Assets/Images/... (사용한 이미지 의존성만)
REPORT.json
```

서버 폴더에는 JSON만 출력합니다. 이미지·HTML·JS·CJS·Python을 넣지 않습니다.
스킬 ZIP은 설치나 기존 파일 삭제를 자동 실행하지 않습니다. ZIP의 server/client 하위 내용을 대응하는 실행 디렉터리에 별도로 설치해야 합니다.
클라이언트 원본의 assets.ini에는 두 Data ID를 등록했습니다. 새 모션 PNG는 PlayerSkillVisuals 상대 경로를 통해 읽으므로 Images ID를 추가하지 않아도 됩니다.
서버/클라이언트 PlayerSkills.json이 다르면 스킬 상태 수신이나 던전 진입 시 계약 검증이 실패합니다.
기존 INI 커맨드 오라와 이 카탈로그는 서로 다른 정의입니다.

현재 던전은 서버가 습득 여부·쿨다운·타격·버프를 판정합니다.
마을에서는 습득한 카탈로그 스킬의 모션·이펙트를 로컬로 재생하지만 실제 몬스터 타격·버프 판정은 하지 않습니다.
레벨·선행 조건·습득·단축 슬롯 UI는 별도 SkillTrees/SkillUi 계약을 사용합니다.
패킷 생성 및 정적 검사와 게임 빌드·실행·플레이 확인은 구분합니다. 이 작업에서 빌드·브라우저 실행·기능 테스트는 수행하지 않습니다.

## 크기와 비용

스킬 최대 256개, 캐릭터 256개, 모션 512프레임, 이미지당 15MB/8192px, 원본 이미지 총량 100MB입니다.
디코딩 이미지 총량은 64M 픽셀로 제한합니다.
작업 JSON은 145MB, data URL 총량 140MB, 공용 런타임 JSON은 4MB, 표시 JSON은 16MB, ZIP은 128MB입니다.
실제 초기 던전 월드 스트림은 기존 4MB 제한을 따르므로 지도와 스킬의 합계도 이 제한 안에 있어야 합니다.
서버 투사체는 룸당 최대 256개이며 속도 4000, 범위 10000까지 허용합니다. 많은 투사체·타깃·긴 활성 구간은 틱 비용을 높입니다.
실시간 프레임이 기존 48KB 제한을 넘으면 전송을 생략하고 클라이언트의 JSON 복구 경로를 사용합니다. 이 밀집 상황의 표시 지연은 아직 실측하지 않았습니다.
모든 서버 인스턴스 변경은 룸 strand, 클라이언트 입력·표시·이미지 로딩은 게임 스레드, 편집은 브라우저 이벤트 루프에서만 처리합니다.
실행 중 정의 재로딩은 지원하지 않습니다. 수정된 데이터를 적용하려면 서버/클라이언트를 새 데이터로 시작해야 합니다.

## 레벨·선행 조건과 실제 적용 순서

이 편집기에는 스킬 트리·습득 레벨·SP 비용·선행 스킬·스킬 아이콘을 편집하는 UI가 없습니다.
현재 서버는 **모든 PlayerSkills 스킬에 SkillTrees 노드가 하나씩 있는 것**을 요구합니다.
따라서 편집기 ZIP만 새로 설치해 스킬 수를 바꾸면 기존 SkillTrees와 불일치하여 로딩이 실패할 수 있습니다.

1. 스킬 동작·공격·연출을 편집하고 작업 JSON을 저장합니다. **검사**는 Build의 데이터·이미지 검사를 수행하며 별도 승인 기록은 만들지 않습니다.
2. 오류가 없으면 ZIP을 출력합니다. 새 커맨드는 Left/Right/Up/Down/Z/X/C/V 중 1~16개로 구성합니다.
3. 별도 `SkillTrees.json`에 같은 skillId의 requiredLevel, spCost, maxSkillLevel, prerequisites,
   description, icon, column/row, damagePerLevel을 작성합니다. 자세한 제약은 [FORMAT.md](FORMAT.md)를 따릅니다.
4. TownServer와 GameRoomServer의 `Data/PlayerSkills.json`, `Data/SkillTrees.json`을 맞추고,
   클라이언트 `Assets/Data/PlayerSkills.json`, `PlayerSkillVisuals.json`과 사용 PNG·트리 아이콘을 맞춥니다.
   이 편집기 ZIP은 SkillTrees와 아이콘을 출력하거나 자동 설치하지 않습니다.
5. 원본 반영, 실행 폴더 반영, 서버 시작 시 로딩, 스킬 습득, 게임 플레이 확인을 따로 확인합니다.
   학습 레벨이 0인 스킬은 커맨드와 단축키로 사용할 수 없습니다.

실제 등록 예는 `Character1.FrontKick`(앞차기)입니다. 현재 공용 데이터는 Character1, `Up Z`,
쿨다운 2초, 기본 피해 25, 깊이 반경 24, 지상 frontKick 모션 8프레임/16FPS,
활성 프레임 3~4, 공중 불허입니다. 이는 현재 콘텐츠 값이며 모든 새 스킬의 기본값은 아닙니다.
스킬 트리는 요구 레벨 1, SP 20, 레벨 간격 2, 선행 없음, 피해 증가 5를 별도로 정의합니다.
frontKick은 플레이어 INI 변환의 공통 17개 모션에 포함되지 않으므로 해당 모션의 animations.json과 PNG를 추가로 불러와야 합니다.
런타임 PlayerSkills/PlayerSkillVisuals를 작업 열기에 넣을 수는 없습니다. 기존 작업 파일을 열거나 원본 모션으로 새 작업을 만듭니다.

| 오류·상황 | 해결 |
|---|---|
| 직접 공격 프레임 누락 | 시작~종료 프레임 모두에 공격 사각형을 지정 |
| 이미지를 찾을 수 없음 | motion.image 또는 effect.image 경로에 해당하는 PNG/모션 이미지를 불러오기 |
| 중복 커맨드·ID | 같은 캐릭터의 동일 커맨드와 문서 전체의 스킬 ID 중복 해소 |
| 지상/공중 모두 비활성 | 사용 가능한 모드를 최소 하나 지정 |
| 서버에 새 스킬이 안 보임 | 출력 ZIP뿐 아니라 Town/Room의 PlayerSkills·SkillTrees와 클라이언트 표시·아이콘 설치 확인 |

2026-10-06 근거: [model.js](model.js)의 Build/ReadProject, [editor.js](editor.js)의 출력,
[PlayerSkillPresentation.cpp](../ActionRPGClient/Game/PlayerSkillPresentation.cpp),
[SkillUi.cpp](../ActionRPGClient/Game/SkillUi.cpp), 서버 Shared/SkillTreeCatalog.h와 GameRoomSkills.cpp입니다.
이번 문서 작업에서는 빌드·편집기 실행·테스트·플레이 확인을 하지 않았습니다.
