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
3. fps·크기·pivot·사각형을 유한 수로 검사합니다. 너비·높이는 양수이고 프레임 경계 안에 있어야 합니다.
4. 애니메이션의 ID, frameCount, sourceRect, pivot, 배율과 표시 크기를 같은 버전의 원본과 대조합니다.
5. 누락은 오류입니다. 기본 사각형, 다른 상태의 영역, 무적 상태로 자동 대체하지 않습니다.
6. 선택한 현재 모션 ID와 프레임 index로 조회합니다. 클라이언트가 보낸 임의 사각형을 서버 판정에 사용하지 않습니다.

서버는 시작 시 검증한 정의를 읽기 전용으로 공유하고 개체의 현재 모션/프레임/위치/높이는 해당 룸 strand에서 관리해야 합니다.
클라이언트의 상태는 게임 스레드에서 관리합니다. 런타임의 상태·프레임 동기화는 출력 파일만으로 해결되지 않습니다.

## 파일 배치

캐릭터 피격 정의의 서버 설치 위치와 등록 방식은 서버 담당의 카탈로그 계약에 맞춰 결정합니다.
클라이언트에도 같은 파일을 배포해야 합니다. 설치·등록·로더·전투 판정은 이 편집기 구현에 포함하지 않습니다.
작업 파일의 images base64는 편집 재개용이며 서버에 설치하는 형식이 아닙니다.
이미지와 animation JSON 자체를 갱신했다면 피격 파일도 다시 확인하고 출력해야 합니다.
문자열 ID가 같다는 이유만으로 원본이 다른 영역을 재사용하지 않습니다.
