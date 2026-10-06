# Character Editor

플레이어와 몬스터의 기존 애니메이션을 읽고 **캐릭터 ID → 모션 ID → 프레임 index**마다 사각형 피격 영역 하나를 편집하는 로컬 도구입니다.
HTML/CSS/JavaScript와 Windows 브라우저 런처 프로젝트로 구성됩니다. 인터넷, npm, CDN, 외부 서버가 필요하지 않습니다.
빈 문서로 시작하며 캐릭터 ID, 상태/모션, 프레임은 불러온 데이터에서만 가져옵니다.

## 빠른 시작과 문서 안내

아래 프로젝트·실행 파일 경로는 `ActionRPGClient.slnx`가 있는 클라이언트 프로젝트 폴더 기준입니다.

| 항목 | 안내 |
|---|---|
| 바로 열기 | [index.html](index.html)을 Edge/Chrome에서 열기; 서버·npm 설치 불필요 |
| Windows 런처 | [CharacterEditor.vcxproj](CharacterEditor.vcxproj), `artifacts/bin/x64/Debug/CharacterEditor.exe` (Release도 같은 구조) |
| 입력 | animations.json과 참조 이미지, 또는 플레이어 INI·이미지; 편집 재개는 CharacterEditorProject 작업 JSON |
| 작업 흐름 | 원본 불러오기 → 캐릭터/모션/프레임 선택 → 피격 사각형 편집 → 작업 저장·불러오기 → 정적 검사 → 게임용 JSON 출력 |
| 상세 계약 | [FORMAT.md](FORMAT.md): sourceRect/pivot/배율, CharacterHurtRects v1, 로더·등록 경계 |

작업 저장은 편집 재개용이며 피격 JSON 출력과 다릅니다. 출력에는 별도 승인 기록이나 자동 설치가 없습니다.
런타임 로더 연결·실행 데이터 설치·플레이 확인은 별도 단계입니다. 아래에 실제 편집·오류 해결 방법을 설명합니다.


## 열기

`index.html`을 Edge 또는 Chrome에서 직접 엽니다. HTML이 텍스트 편집기에 연결되어 있으면 브라우저에서 여세요.
Visual Studio 솔루션에는 `CharacterEditor` 프로젝트가 추가됩니다. 사용자가 빌드하면
`artifacts/bin/x64/Debug/CharacterEditor.exe`와 옆의 `CharacterEditor/` 도구 파일이 생성됩니다.
런처는 OS의 HTML 연결 프로그램을 사용합니다.

이번 구현에서는 빌드, 브라우저 실행, 기능 테스트를 수행하지 않았습니다. 문법/XML/데이터 경계의 정적 확인만 수행합니다.

## 몬스터 불러오기

1. **애니메이션 JSON**에서 `animations.json`을 선택합니다.
2. **이미지 폴더**에서 참조 이미지가 들어 있는 폴더를 선택합니다. 상위 폴더도 가능합니다.
3. 캐릭터와 모션을 선택하고 프레임 번호를 선택합니다.

현재 클라이언트에 복사된 제작 데이터는 `Assets/Images/Monsters/animations.json`에 있습니다.
서버 저장소의 원본은 `output/monster-art/animations/animations.json`입니다.
각 모션의 `image` 경로에 해당하는 파일을 폴더에서 찾습니다. 후보가 여러 개면 임의 선택하지 않고 오류 처리합니다.
`sourceRect`는 원본 PNG 좌표, `pivot`은 잘라낸 프레임 내부 좌표입니다. 시트를 등분하지 않습니다.
`frames` 배열은 `index`를 기준으로 읽고 표시합니다. 배열 순서가 바뀌어도 동일 프레임을 찾습니다.
현재 제작 JSON은 move/hit/airborne/attack/death를 포함합니다. JSON에 없는 idle 등을 자동 생성하지 않습니다.

새로운 캐릭터의 JSON을 추가로 불러오면 현재 문서에 합칩니다. 같은 ID의 중복은 오류입니다.
원본을 교체하려면 작업을 저장한 후 새 문서에서 여세요. 현재 문서의 피격 영역을 원본 변경에 자동 재사용하지 않습니다.
이미지 폴더는 필요한 부분을 나누어 불러올 수 있습니다.

## 플레이어 INI → animations.json

**플레이어 INI 변환**에서 아래 두 폴더 중 하나를 선택합니다.

- 권장: `ActionRPGClient/ActionRPGClient` 프로젝트 폴더. `Assets/`와 `ActionRPGClient/Game/Player.cpp`를 함께 포함합니다.
- `Assets/` 폴더만 선택해도 변환할 수 있습니다. 이 경우 현재 문서화된 `PLAYER_ANIMATIONS` 연결표를 사용하며 C++ 변경 여부는 확인할 수 없습니다.

읽는 파일:

- `Data/characters.ini`: 캐릭터 ID와 idle/walk 애니메이션 참조. `Default`도 원본의 기본 정의 ID로 표시합니다.
- `Data/animations.ini`: 이미지 ID, columns/rows, frame_count, frame_seconds, 행 비율, 기준점, 게임 표시 크기.
- `Data/assets.ini`: 이미지 ID → Assets 기준 상대 경로.
- 해당 이미지 파일.
- 선택 폴더에 있으면 `Game/Player.cpp`: `PLAYER_ANIMATIONS`가 변환기의 검토된 연결표와 일치하는지 확인.

전체 상태 연결은 현재 C++에 선언되어 있어 INI만으로는 자동 복원할 수 없습니다.
변환기는 `CharacterAnimationSet`의 명시적 연결표를 사용하며 이름 접두어로 애니메이션을 추측하지 않습니다.
캐릭터별 idle/walk 참조는 `characters.ini`가 우선합니다. 나머지 상태는 현재 공통 플레이어 연결표를 사용합니다.
연결표가 바뀌면 변환기와 런타임의 계약을 함께 검토해야 합니다. 알려지지 않은 캐릭터 INI 필드는 오류로 표시합니다.

변환하는 모션 ID:
`idle`, `walk`, `run`, `attackStart`, `attackFire`, `attackEnd`, `jumpStart`, `jumpHold`, `jumpLand`,
`airAttackStart`, `airAttackFire`, `airAttackEnd`, `hit`, `airHitStart`, `airHitFall`, `knockdown`, `getUp`.

`SpriteAnimation::Draw`와 같은 규칙으로 프레임을 계산합니다.
2행 시트는 `first_row_ratio`를 적용하고 2행의 `second_row_anchor_y`, 프레임별 `anchor_xs`도 반영합니다.
`render_width`와 `render_height`는 `renderSize`로 기록합니다. 프레임 번호는 0부터 시작합니다.
변환 결과는 즉시 편집할 수 있고 **변환한 animations.json 저장**으로 독립 JSON을 출력합니다.
이 버튼은 몬스터와 플레이어를 합친 전체 현재 애니메이션 문서도 저장합니다. 이미지는 포함하지 않습니다.
게임의 원본 INI나 C++는 변경하지 않습니다.

## 사각형 편집

- **사각형 그리기 R**: 드래그해서 현재 프레임의 영역을 생성하거나 교체합니다.
- **선택 · 이동 V**: 내부를 드래그하면 이동, 네 모서리를 드래그하면 크기를 조절합니다.
- 오른쪽 X/Y/너비/높이를 입력하고 **좌표 적용**을 누르면 수치로 수정합니다.
- **복사 / 붙여넣기** 또는 Ctrl+C / Ctrl+V: 원본 프레임 픽셀 좌표 그대로 복사합니다. 대상 프레임에 들어가지 않으면 적용하지 않습니다.
- **현재 모션 전체에 복사**: 모든 프레임에 들어가는 경우에만 일괄 적용합니다. 자동 크기 보정은 하지 않습니다.
- Delete: 현재 영역 삭제. Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z: 실행 취소 / 다시 실행. 최대 30회.
- 좌우 화살표: 이전/다음 프레임. Space: 모션 재생/일시 정지. F: 전체 보기. Esc: 진행 중인 드래그 취소.
- 좌우 반전 보기는 미리보기만 반전합니다. 저장 데이터는 오른쪽을 바라보는 기준으로 유지합니다.
- 애니메이션 재생은 해당 모션만 미리 봅니다. 상태 전환, 점프 물리, 스킬 이벤트, 전투 판정은 실행하지 않습니다.

피격 사각형은 해당 프레임 이미지 내부에 있어야 합니다. 기준점과 축을 설정하는 UI는 없습니다.
게임 상태 우선순위, AI, HP, 이동 속도, 공격 영역, 무적 설정은 이번 범위에 없습니다.

## 저장·불러오기·출력

**작업 저장**은 `Character.character-project.json`을 저장합니다.
`format=CharacterEditorProject`, `schemaVersion=1`이며 원본 애니메이션 스냅샷, 피격 사각형, 불러온 이미지 base64를 포함합니다.
미완성 작업이나 일부 이미지가 없는 작업도 저장할 수 있습니다. 다시 불러오면 원본 경로 없이 편집을 이어갈 수 있습니다.
이미지가 없는 항목은 나중에 이미지 폴더를 불러오세요.

파일 쓰기 API를 지원하는 브라우저는 파일에 직접 저장합니다. 작업 저장 대상만 재사용하며,
불러온 작업 원본은 처음 저장할 때 별도로 위치를 선택합니다. 원본 애니메이션·피격 출력 파일과 혼동하지 않습니다.
지원하지 않는 브라우저는 다운로드를 요청합니다. 도구가 다운로드 완료를 확인할 수 없으므로 미저장 경고를 유지합니다.

**게임용 JSON 출력**은 `Character.hurtrects.json`을 저장합니다.
`format=CharacterHurtRects`, `schemaVersion=1`이며 모든 프레임의 영역이 유효하고 실제 이미지 크기가 선언과 일치해야 출력합니다.
이미지 경로·이미지 데이터는 포함하지 않습니다. 자세한 계약은 [FORMAT.md](FORMAT.md)를 읽으세요.
**피격 JSON 불러오기**는 현재 애니메이션의 ID·프레임·좌표·재생·표시 크기와 대조한 후 영역을 복원합니다.
다른 원본의 피격 영역을 ID만 보고 자동 적용하지 않습니다.

## 검사·제한

- animations.json version 1, 고유 캐릭터/모션 ID, 정확한 프레임 수와 고유 0 기반 index
- sourceRect의 이미지 경계, pivot, 재생 정보, 상대 배율과 표시 크기
- 실제 이미지 해석 여부와 PNG/JPEG/WebP 크기, 안전한 상대 경로
- 사각형 수치·크기·프레임 경계, 원본에 없는 참조와 미지정 프레임

캐릭터 최대 256개, 캐릭터별 모션 128개, 모션별 프레임 512개, 전체 프레임 32768개입니다.
애니메이션 JSON은 8MB, 작업/저장 JSON은 145MB, 피격 불러오기 JSON은 16MB입니다.
이미지당 15MB, 한 변 8192px, 불러오기 원본 총량 100MB, 저장용 data URL 총량 140MB입니다.
큰 이미지와 많은 프레임은 메모리·저장·검사 비용이 커집니다. 파일 작업 중에는 편집을 잠그고 애니메이션을 멈춥니다.
편집 데이터는 브라우저 단일 이벤트 루프에서만 수정하며 서버의 살아 있는 상태나 공유 메모리를 변경하지 않습니다.

## 게임 연동 범위

이 도구는 공용 JSON 출력까지 구현합니다. 클라이언트와 룸 서버의 실제 로더·피격 판정은 이번 변경에 포함하지 않습니다.
몬스터의 숫자 Data ID는 기존 카탈로그와 문자열 캐릭터 ID를 연결해야 합니다.
제작 몬스터의 최종 게임 크기, 서버의 현재 모션/프레임/점프 높이 관리도 런타임 담당과 연결해야 합니다.
파일을 출력했다고 즉시 게임 피격 판정에 적용되는 것은 아닙니다.

## 사용 예와 오류 해결

점프 중 피격 영역을 줄이려면 플레이어 INI를 변환한 뒤 해당 캐릭터의 `jumpHold`를 선택합니다.
프레임 0에 사각형을 그려 좌표 적용을 확인하고, 모든 프레임의 잘라낸 크기에 들어갈 때만 **현재 모션 전체에 복사**합니다.
필요한 다른 모션·프레임의 영역도 모두 지정하고 **정적 검사** → **작업 저장** → **게임용 JSON 출력** 순서로 진행합니다.
이 과정은 피격 정의 제작 예이며 실제 게임의 무적·점프 판정을 변경하는 명령은 아닙니다.

| 오류·상황 | 확인할 내용 |
|---|---|
| 이미지 누락·후보 중복 | motion.image와 폴더 내 상대 경로를 맞추고 같은 경로로 검색되는 파일을 하나로 정리 |
| 실제 크기와 선언 불일치 | 이미지를 바꿨다면 animations.json의 width/height/sourceRect를 함께 재작성 |
| 미지정 프레임 | 문서의 모든 캐릭터·모션·프레임을 확인; 일부만 완성해도 전체 출력은 차단 |
| PLAYER_ANIMATIONS 불일치 | C++ 연결표와 변환기 계약을 검토; Assets만 선택해 검사를 우회했다고 일치가 증명되지는 않음 |
| 같은 캐릭터 ID 재불러오기 | 다른 캐릭터 추가와 원본 교체를 구분; 교체는 저장 후 새 문서에서 수행 |

픽셀 기준점과 프레임 크기는 원본에서 읽으며 이 도구가 새 모션·PNG·숫자 Data ID를 제작하지 않습니다.
공용 피격 JSON을 출력해도 런타임 설치·등록·판정 연결을 자동 수행하지 않습니다.
직접 공격의 공격 영역과 시전 데이터는 별도 [SkillEditor](../SkillEditor/README.md)에서 편집합니다.

2026-10-06 소스 확인 기준: [model.js](model.js)의 ConvertPlayer/Check/Export/ReadHurtRects와
[editor.js](editor.js)의 파일·드래그 처리, [CharacterEditor.vcxproj](CharacterEditor.vcxproj)의 출력 경로를 대조했습니다.
이번 문서 갱신에서는 빌드·도구 실행·테스트·플레이 확인을 하지 않았습니다.
