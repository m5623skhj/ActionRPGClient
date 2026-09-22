# 이미지 디렉터리

캐릭터 스프라이트 시트, 마을 배경, 이펙트 이미지를 보관합니다.

- 캐릭터·이펙트처럼 논리적 ID로 찾는 이미지는 `Assets/Data/assets.ini`의 `[Images]`에 상대 경로를 등록합니다.
- 마을 배경은 맵 JSON의 `images[].asset`에 `Images/Towns/파일명.png`처럼 `Assets/` 기준 상대 경로를 기록합니다. 이 이미지는 `[Images]` 등록이 필요하지 않습니다.

플레이어 스프라이트 시트 구성은 `Assets/Data/animations.ini`에서 지정합니다. 프레임은 왼쪽에서 오른쪽으로, 다음 행은 위에서 아래로 읽습니다. 두 행의 여백이 서로 다르면 `first_row_ratio`로 행의 분할 위치를 조정할 수 있습니다.
