# 던전 패키지 생성·설치 자동화

이 도구는 **기존 작업 JSON**을 입력받습니다. 던전 컨셉, 방 연결, 지형, 몬스터 배치를 새로 만들거나 수정하지 않습니다.
FallenCitadel 제작 기록을 다시 실행하지 않으며 원본 작업 파일을 덮어쓰지 않습니다.
사용자가 승인한 기존 UI 수동 출력의 대안입니다. UI 편집·미리보기는 계속 사용할 수 있습니다.

구현 당시에는 빌드·브라우저/명령행 도구 실행·기능/게임 테스트를 하지 않았습니다.
문법, 프로젝트/XML/HTML 참조와 변경 범위의 정적 확인만 수행했습니다. 아래 명령은 사용 방법이며 실행 기록이 아닙니다.

## 공용 출력

`package.js`의 `DungeonPackage.Build`를 브라우저 UI와 명령행이 함께 사용합니다.
기존 DungeonModel의 parse/validate/minimap/runtimeFiles와 DungeonArchive.create를 재사용합니다.
원본 PNG/JPEG/WebP/BMP 해석, PNG 생성만 호스트 어댑터로 분리합니다.

출력 계약은 작업 schemaVersion 3 / Dungeon.json version 3 / DungeonRoom version 5입니다.
미니맵, 일반/보스 아이콘, 사용한 게이트/발판/오브젝트, Dummy PNG를 같은 파일 목록 규칙으로 만듭니다.
UI의 ZIP에는 신규 몬스터 외형 리소스가 포함되지 않습니다. 명령행 설치 도구는 별도 의존성으로 검사하고 실행 Assets에 배포합니다.
브라우저의 SVG→Canvas와 Node의 SVG→Sharp는 래스터 엔진이 다르므로 PNG 바이트가 완전히 동일하다고 보장하지 않습니다.
JSON·SVG 도형·파일 경로·크기·투명 배경·참조 관계를 공유합니다. ZIP 자체는 동일 archive.js로 생성합니다.

UI는 로컬 HTML 그대로 열며 npm/CDN/인터넷 의존성을 추가하지 않습니다.
Node 도구는 설치된 Node.js 22 이상과 Sharp 모듈이 필요합니다. npm 설치나 인터넷 접속을 자동 수행하지 않습니다.
BMP는 Windows System.Drawing을 이용해 해석합니다. 다른 플랫폼의 BMP 입력은 오류로 보고합니다.
명령행은 원본 클라이언트 프로젝트의 MonsterEditor/model.js로 AI 정의를,
CharacterEditor/model.js로 animations.json의 프레임 좌표·기준점을 검증합니다. 검증기를 별도로 복제하지 않습니다.

## 패키지 생성과 설치 목록 확인

다음은 현재 프로젝트용 PowerShell 예시입니다. 변수는 예약된 HOME/CODEX_HOME과 무관합니다.

```powershell
$dungeonTool = 'C:\Users\KimHyeongJin\source\repos\ActionRPGClient\ActionRPGClient\DungeonEditor\tools\dungeon-package.cjs'
$dungeonWork = 'C:\Users\KimHyeongJin\source\repos\ActionRPGServer\output\dungeons\FallenCitadel\FallenCitadel.dungeon-project.json'
$dungeonOutput = 'C:\Users\KimHyeongJin\source\repos\ActionRPGClient\ActionRPGClient\DungeonEditor\output\FallenCitadel\automated-output-01'
$serverRepo = 'C:\Users\KimHyeongJin\source\repos\ActionRPGServer'
$clientProject = 'C:\Users\KimHyeongJin\source\repos\ActionRPGClient\ActionRPGClient'
$sharpModule = 'C:\Users\KimHyeongJin\.cache\codex-runtimes\codex-primary-runtime\dependencies\node\node_modules\sharp'

node $dungeonTool --project $dungeonWork --out $dungeonOutput --server-repo $serverRepo --client-project $clientProject --sharp-module $sharpModule
```

Sharp를 일반 Node 모듈 검색으로 찾을 수 있으면 `--sharp-module`을 생략할 수 있습니다.
위 경로는 이 PC에서 확인한 기존 의존성 위치입니다. 다른 PC에서는 설치된 경로를 지정하세요.

생성물:

- `<DungeonId>.zip`: 공용 처리로 생성한 게임용 패키지.
- `INSTALL_PLAN.json`: 대상 루트, 생성/교체 파일, 변경 전후 SHA-256, 크기.
- `REPORT.json`: 검사 결과, 몬스터 의존성, 패키지 정보, 설치 상태, 미연동 사항.

출력은 새 폴더의 절대 경로를 지정합니다. 기존 출력 폴더를 덮어쓰지 않습니다.
Data ID와 패키지 내부 경로는 기존 규칙을 따릅니다. 출력 폴더 이름도 영문·숫자·_·-·.으로 지정하세요.
`--install`이 없으면 패키지와 설치 계획만 출력하며 원본/실행 데이터를 변경하지 않습니다.

## 설치 대상

기본 설치 대상은 서버 던전 원본과 클라이언트 원본 Assets입니다. `--install`을 명시할 때만 적용합니다.

- 서버 원본: `C:\Users\KimHyeongJin\source\repos\ActionRPGServer\ActionRPGServer\GameRoomServer\Data\Dungeons\<DungeonId>`
- 클라이언트 원본: `C:\Users\KimHyeongJin\source\repos\ActionRPGClient\ActionRPGClient\Assets\Images\Dungeons`
- 기본 Dummy PNG도 기존 UI와 같은 출력 규칙에 따라 원본 Assets에 포함됩니다.

실행 데이터도 설치하려면 세 루트를 모두 지정합니다. Debug/Release를 자동 추측하지 않습니다.

```powershell
$roomRuntime = 'C:\Users\KimHyeongJin\source\repos\ActionRPGServer\artifacts\bin\x64\Debug'
$townRuntime = 'C:\Users\KimHyeongJin\source\repos\ActionRPGServer\ActionRPGServer\x64\Debug'
$clientRuntime = 'C:\Users\KimHyeongJin\source\repos\ActionRPGClient\ActionRPGClient\artifacts\bin\x64\Debug'
$dungeonOutput = 'C:\Users\KimHyeongJin\source\repos\ActionRPGClient\ActionRPGClient\DungeonEditor\output\FallenCitadel\automated-output-02'

node $dungeonTool --project $dungeonWork --out $dungeonOutput --server-repo $serverRepo --client-project $clientProject --sharp-module $sharpModule --room-runtime $roomRuntime --town-runtime $townRuntime --client-runtime $clientRuntime
```

이 명령도 설치 목록 확인만 합니다. 실제 설치는 같은 인자에 `--install`을 추가하고 새 출력 폴더를 지정합니다.
Release는 세 실행 루트의 마지막 폴더를 Release로 지정합니다. 각 루트에 해당 EXE가 있어야 합니다.

미니맵 PNG/SVG와 일반·보스 아이콘은 공용 ZIP의 미리보기 자료로 보존하지만 서버 원본/실행 폴더에는 설치하지 않습니다.
서버 output에는 작업 JSON·서버 JSON·보고서만 두고, 공용 ZIP과 그래픽 제작 자료는 클라이언트 또는 별도 산출물 폴더에 보관하세요.
기존 미니맵/아이콘을 포함한 설치 저널의 복원은 계속 지원합니다. 새 설치는 이미지 경로를 거부합니다.

실행 설치는 다음을 함께 처리합니다.

- GameRoomServer `Data/Dungeons/<DungeonId>`에 `Dungeon.json`과 `Maps/*.json`만 설치합니다.
- GameRoomServer `Data/Monsters`에 원본 카탈로그와 그 카탈로그가 참조하는 모든 AI 정의.
- TownServer `Data/DungeonCatalog.json`에 이미 등록된 원본 카탈로그 스냅샷.
- 클라이언트 실행 `Assets`에 던전 이미지·Dummy 이미지.
- 배치에 신규 몬스터가 있으면 클라이언트 실행 Assets에 기존 idle PNG, animations.json, 공유 animations.json이 참조하는 전체 모션 이미지.

공유 모션 JSON을 임의로 줄이거나 수정하지 않으므로 그 문서가 참조하는 전체 이미지가 의존성 범위입니다.
서버 카탈로그도 전체 문서와 모든 참조 정의를 배포합니다. 누락된 정의 때문에 서버 시작이 실패하는 것을 피하기 위한 범위입니다.
서버 원본 카탈로그, AI 정의, 클라이언트 원본 모션 이미지는 이미 존재하는 데이터를 읽으며 변경하지 않습니다.
새 ID 할당·원본 카탈로그 등록은 하지 않습니다. 미등록 배치는 오류로 중단합니다.

## 설치 검사·백업·복원

설치 전에 작업/의존성 검사와 패키지 생성을 완료합니다.
숫자 ID 중복, 잘못된 정의·외형 참조, 다른 폴더의 동일 던전 ID, 폴더와 다른 던전 ID,
대소문자만 다른 던전 폴더, 실제 이미지 크기 불일치, 경로 이탈·링크/정션 경로는 오류입니다.
기존 실행 카탈로그에 원본에 없는 ID나 다른 등록 내용이 있으면 덮어쓰지 않습니다.
덮어쓰는 설치 파일의 해시를 설치 직전에도 대조합니다. 카탈로그·원본 작업·이미지·공용 모델도 변경 여부를 다시 확인합니다.

설치 이력과 백업은 서버 저장소 `.dungeon-installs/<transactionId>`에 둡니다.
서버의 Data/Dungeons 검색 경로 밖이므로 백업이 던전으로 잘못 로딩되지 않습니다.
대상 루트별 `.dungeon-install.lock`으로 같은 도구의 동시 설치를 막습니다.
기존 파일은 `<index>.before`, 설치 데이터는 `<index>.after`, 단계와 원래 상태는 `journal.json`에 남깁니다.
파일별 교체 전에 복원 정보를 기록하고 대상과 같은 폴더의 임시 파일을 이름 변경해 교체합니다.
이미지·맵 등 의존성을 먼저 반영하고 모션 JSON·몬스터 카탈로그·Dungeon.json·TownServer 카탈로그 순으로 참조를 반영합니다.
다른 던전이나 출력에서 더 이상 참조하지 않는 기존 파일을 재귀 삭제하지 않습니다.

여러 루트를 한 번에 원자적으로 교체할 수는 없습니다. 일반 실패는 자동 복원하며,
강제 종료·전원 장애 후에는 잠금과 journal을 보존하고 명시적인 복원 명령을 사용합니다.
설치 후 사용자/다른 도구가 수정한 대상은 자동 복원으로 덮어쓰지 않고 복원 실패를 보고합니다.
해시가 다르거나 불완전한 임시 파일은 보존하고 journal의 preservedTemporaryFiles에 기록합니다.
백업·복원 실패를 설치 성공으로 표시하지 않습니다.

```powershell
$transactionDirectory = 'C:\Users\KimHyeongJin\source\repos\ActionRPGServer\.dungeon-installs\<journal에 기록된 transactionId>'
node $dungeonTool --restore $transactionDirectory --server-repo $serverRepo --client-project $clientProject --room-runtime $roomRuntime --town-runtime $townRuntime --client-runtime $clientRuntime
```

원본만 설치했던 이력을 복원할 때는 실행 루트 세 인자를 모두 생략합니다. 최초 설치와 루트 조합·경로가 같아야 합니다.
설치 프로세스가 살아 있는 잠금은 제거하지 않습니다. 복원은 기록된 데이터 파일 범위만 처리합니다.

실행 폴더 대상이 있으면 해당 EXE의 프로세스 경로를 확인하고 실행 중에는 적용하지 않습니다.
권한 문제로 프로세스 경로를 확인할 수 없으면 중단합니다. 프로그램을 자동 종료·실행·재시작하지 않습니다.
외부 프로그램의 새 실행까지 OS 차원에서 차단하는 잠금은 아니므로 설치 도중 해당 프로그램을 시작하지 마세요.
설치 결과에는 실행 대상의 재시작 필요 여부를 기록합니다.

## 결과 의미와 제한

`package-generated`: 입력과 의존성 검사 후 패키지 생성.
`installation.status=not-installed`: 설치 계획만 작성.
`installed` / `already-installed`: 선택한 파일 설치 또는 동일 해시 확인.
`failed`: 설치 전 오류 또는 복원 정보가 포함된 설치 실패. 자세한 원인은 error와 journal을 확인합니다.
`rolled-back`: 기록된 대상 파일의 이전 상태 복원.

항상 `playableVerified=false`, `gameBuildOrPlayTestPerformed=false`입니다.
원본 설치와 실행 데이터 설치는 runtimeInstallationRequested, INSTALL_PLAN의 루트/파일로 구분합니다.
던전 설치 완료가 신규 몬스터 렌더링, 서버 AI·전투·드랍·보상, 상태/프레임 동기화 구현을 뜻하지 않습니다.
현재 FallenCitadel 신규 몬스터 정의는 HP 100과 Wait 반복인 초안입니다.
이전 버전 작업 파일의 패키지 생성은 기존 모델의 변환 규칙을 따르지만, 설치는 UI에서 Data ID와 변환 결과를 확인해 schemaVersion 3으로 저장한 후 허용합니다.
서버는 시작할 때 데이터를 읽으므로 설치 후 다시 시작해야 실제 로딩을 확인할 수 있습니다. 이번 도구는 서버를 실행해 확인하지 않습니다.

큰 base64 작업 파일, 이미지 해석과 PNG 변환에는 메모리·시간이 필요합니다.
기존 105MB 작업 파일, 15MB 이미지, 8192px 한 변, 3.5MB 런타임 JSON, 128MB ZIP 제한을 유지합니다.
구현을 실행해 검증하지 않았으므로 최초 사용 시에는 설치 옵션 없이 출력·계획을 먼저 확인하는 것이 적절합니다.
