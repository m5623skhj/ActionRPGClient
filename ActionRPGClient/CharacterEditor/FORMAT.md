# CharacterHurtRects v1 — 클라이언트·룸 서버 공용 계약

## 식별

루트는 `schemaVersion: 1`, `format: "CharacterHurtRects"`, `animationVersion: 1`, `coordinates`, `characters`입니다.
`characters`의 키는 animations.json의 문자열 캐릭터 ID, `motions`의 키는 해당 JSON의 모션 ID입니다.
`frames[].index`는 **0 기반 프레임 번호**입니다. 배열 순서 대신 index를 사용합니다.
캐릭터 ID·모션 ID는 대소문자를 구분합니다. 숫자 Data ID를 생성하거나 문자열에서 추측하지 않습니다.
몬스터 런타임은 카탈로그에서 개체 Data ID를 출력 파일의 문자열 ID에 연결해야 합니다.
AI 노드 ID는 이 모션 ID와 별개입니다. AI 노드를 그대로 피격 영역의 상태로 사용하지 않습니다.

```json
{
  "schemaVersion": 1,
  "format": "CharacterHurtRects",
  "animationVersion": 1,
  "coordinates": {
    "unit": "sourcePixels",
    "origin": "frameTopLeft",
    "positiveX": "right",
    "positiveY": "down",
    "facing": "right",
    "frameIndexBase": 0
  },
  "characters": {
    "ExampleCharacter": {
      "motions": {
        "idle": {
          "frameCount": 1,
          "fps": 1,
          "loop": true,
          "holdLastFrame": false,
          "renderSize": { "width": 64, "height": 96 },
          "frames": [
            {
              "index": 0,
              "sourceRect": { "x": 0, "y": 0, "width": 64, "height": 96 },
              "pivot": { "x": 32, "y": 96 },
              "hurtRect": { "x": 8, "y": 8, "width": 48, "height": 88 }
            }
          ]
        }
      }
    }
  }
}
```

위 예시는 형식 설명이며 실제 등록 캐릭터나 기본 피격 영역이 아닙니다.

## 프레임 시간

모션에는 선택 `frameDurationsSeconds`를 저장할 수 있습니다. schemaVersion/animationVersion은 1을 유지합니다.
배열은 정확히 frameCount개이며 배열 위치 i가 frames의 배열 순서가 아닌 **프레임 index i**의 시간(초)입니다.
명시한 null, 빈 배열, 길이 불일치, 0·음수·비유한 수는 오류입니다.
각 값은 float 변환 후에도 유한 양수여야 하며 누적 시간은 유한하고 엄격히 증가해야 합니다.
float 값으로 누적했을 때의 오버플로 또는 경계 소실도 오류입니다.
모션의 전체 시간은 공통 재생기와 동일하게 배열 유무와 관계없이 60초 이하이어야 합니다.
fps는 기존 호환용으로 유지하며 유한 양수·240 이하이어야 합니다.
배열이 **없을 때만** 각 프레임 시간을 1/fps로 해석합니다.
INI 입력의 대응키는 선택 `frame_seconds_list`이며 쉼표로 구분한 frame_count개의 시간입니다.
배열이 없으면 기존 frame_seconds를 사용합니다. 이 도구는 INI를 출력하지 않습니다.

```text
T[0] = 0
T[i + 1] = T[i] + frameDurationsSeconds[i]  // 없으면 1/fps
전체 시간 = T[frameCount]
프레임 i의 유효 시간 = [T[i], T[i + 1])
이벤트 시작 시각 = T[eventFrame]
```

애니메이션·작업 파일·피격 JSON 출력에 명시 배열을 보존합니다.
피격 JSON 불러오기는 기존 fps/반복 등의 비교에 더해 실제 프레임별 시간을 원본과 비교합니다.
배열이 없는 기존 파일도 동일한 균등 시간 원본에는 사용할 수 있습니다.
가변 시간 원본과 다른 시간표인 기존 피격 파일은 다시 확인·출력해야 합니다.
전체 시간이나 이벤트 시점의 자동 보정은 하지 않습니다.
초기 콘텐츠는 전체 시간과 타격 시작·종료 경계를 유지하며 구간 내부에서만 재배분합니다.

## 좌표와 배율

`hurtRect`의 x/y는 **잘라낸 프레임의 왼쪽 위**에 대한 원본 이미지 픽셀입니다.
sourceRect.x/y는 시트에서 자르는 위치이며 hurtRect.x/y에 더해서 월드 좌표로 쓰지 않습니다.
pivot도 잘라낸 프레임 내부의 원본 픽셀 좌표입니다. 카메라 위치나 미리보기 확대율은 저장하지 않습니다.

프레임별 게임 배율을 `scaleX`, `scaleY`, 지면 위치를 `(groundX, groundY)`, 점프 높이를 `height`라고 하면:

```text
오른쪽 보기:
  left   = groundX + (hurtRect.x - pivot.x) * scaleX
  top    = groundY - height + (hurtRect.y - pivot.y) * scaleY
  width  = hurtRect.width * scaleX
  height = hurtRect.height * scaleY

왼쪽 보기:
  left   = groundX + (pivot.x - hurtRect.x - hurtRect.width) * scaleX
  top/width/height는 위와 동일
```

계산의 `height` 입력(점프 높이)과 결과 사각형의 높이를 코드에서 다른 변수로 구분하세요.
이 식은 현재 클라이언트의 지면 Y에서 높이를 빼는 몸체 평면 기준입니다.
타격 간 깊이 허용치, 공격 판정 좌표, 현재 프레임 선택, 상태 우선순위는 런타임에서 별도로 확정합니다.

플레이어 변환 결과에는 INI의 `renderSize`가 포함됩니다.
`scaleX = renderSize.width / sourceRect.width`, `scaleY = renderSize.height / sourceRect.height`입니다.
2행 시트에서 프레임의 잘라낸 높이가 다르면 이 계산도 프레임별로 수행합니다.

몬스터 제작 데이터의 `scaleToMovement`는 기준 이동 모션에 대한 상대 배율이며 **최종 월드 배율이 아닙니다**.
예를 들어 게임에서 확정한 이동 기준 배율이 S이고 등방 배율을 사용한다면 해당 모션 배율은 `S * scaleToMovement`입니다.
`referenceStandingHeight`와 원본 상대 배율은 출력에 보존합니다. 클라이언트·서버가 같은 최종 크기를 사용해야 합니다.
preview.html의 보스 1.65배나 도구의 확대 슬라이더는 판정 배율로 사용하지 않습니다.

## 로더 요구 사항

1. 버전·format·coordinates를 확인합니다. 알 수 없는 좌표 규칙을 그대로 해석하지 않습니다.
2. 캐릭터/모션 ID와 각 프레임의 고유 index를 검사합니다. frameCount만큼 모두 존재해야 합니다.
3. fps·크기·pivot·사각형을 유한 수로 검사합니다. 선택 시간 배열은 위 길이·양수·float 범위·누적 증가 계약을 검사합니다. 너비·높이는 양수이고 프레임 경계 안에 있어야 합니다.
4. 애니메이션의 ID, frameCount, 프레임별 시간, sourceRect, pivot, 배율과 표시 크기를 같은 버전의 원본과 대조합니다.
5. 누락은 오류입니다. 기본 사각형, 다른 상태의 영역, 무적 상태로 자동 대체하지 않습니다.
6. 누적 시간으로 선택한 현재 모션 ID와 프레임 index로 조회합니다. 클라이언트가 보낸 임의 사각형을 서버 판정에 사용하지 않습니다.

서버는 시작 시 검증한 정의를 읽기 전용으로 공유하고 개체의 현재 모션/프레임/위치/높이는 해당 룸 strand에서 관리해야 합니다.
클라이언트의 상태는 게임 스레드에서 관리합니다. 런타임의 상태·프레임 동기화는 출력 파일만으로 해결되지 않습니다.

## 파일 배치

캐릭터 피격 정의의 서버 설치 위치와 등록 방식은 서버 담당의 카탈로그 계약에 맞춰 결정합니다.
클라이언트에도 같은 파일을 배포해야 합니다. 설치·등록·로더·전투 판정은 이 편집기 구현에 포함하지 않습니다.
작업 파일의 images base64는 편집 재개용이며 서버에 설치하는 형식이 아닙니다.
이미지와 animation JSON 자체를 갱신했다면 피격 파일도 다시 확인하고 출력해야 합니다.
문자열 ID가 같다는 이유만으로 원본이 다른 영역을 재사용하지 않습니다.

## 입력·작업 파일과 소비자 구분

| 파일 | 버전·역할 | 이미지 포함 |
|---|---|---|
| animations.json | version 1; 캐릭터·모션·프레임의 원본 좌표·재생 정보 | 경로만 |
| Character.character-project.json | CharacterEditorProject schemaVersion 1; animations/hurtRects/images 작업 스냅샷 | data URL 포함 |
| Character.hurtrects.json | CharacterHurtRects schemaVersion 1; 서버·클라이언트 공용 피격 정의 | 없음 |

현재 클라이언트 `Assets/Data/monsters.json`은 숫자 dataId와 monsterId를 연결하고,
`renderHeight / referenceStandingHeight`를 기준 배율로 사용합니다. 모션의 scaleToMovement는 그 배율에 곱합니다.
예를 들어 현재 등록된 녹슨 갑옷병의 renderHeight는 200, 수호자는 330입니다. 이 값은 편집기 줌과 별개입니다.
이 외형 로딩 경로가 CharacterHurtRects의 런타임 판정 로더를 의미하지는 않습니다.
현재 서버 직접 공격은 bodyHeight/hitRadius를 사용하며 피격 JSON의 프레임 사각형으로 대체하지 않습니다.

공격 사각형의 월드 X/높이 계약은 [스킬 FORMAT](../SkillEditor/FORMAT.md)을 따릅니다.
sourcePixels인 hurtRect를 변환 없이 PlayerSkills.attackRects에 복사하지 마세요.
작업 저장, 공용 출력, 양쪽 데이터 설치, 로더 연결, 플레이 확인은 각각 다른 완료 단계입니다.
근거는 [model.js](model.js)의 Export/ReadProject/ReadHurtRects 및
[Monster.cpp](../ActionRPGClient/Game/Monster.cpp)의 MonsterCatalog입니다. 실행 검증은 이번 문서 작업에서 수행하지 않았습니다.
