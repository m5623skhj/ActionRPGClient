# 인증 및 타운 네트워크

2026-10-14 중복 접속 정리 대기·종료 사유 연동을 포함한 클라이언트 소스 기준입니다. 이 문서는 클라이언트의 인증·타운 TLS·콘텐츠 이벤트 연결을 설명합니다. 서버 인증·DB 설정은 [AuthServer](../../ActionRPGServer/ActionRPGServer/AuthServer/DEVELOPMENT.md), 타운 규칙은 [TownServer](../../ActionRPGServer/ActionRPGServer/TownServer/DEVELOPMENT.md), 마이그레이션은 [DB 작업 문서](../../ActionRPGServer/docs/workflows/DATABASE_MIGRATIONS.md)에서 관리합니다.

## 로그인에서 타운 입장까지

```text
로그인 화면
 -> Auth POST /v1/challenges
 -> 시스템 브라우저 Google Desktop OAuth
 -> loopback 콜백의 code -> Google ID token
 -> Auth POST /v1/login -> gameToken
 -> 타운 선택
 -> Auth POST /v1/tickets -> ready=true/false인 단기 ticket
 -> Town TLS
 -> AdmissionTicketRequest(36)
 -> SessionStatus(52, code=1)이면 같은 연결에서 기존 접속 정리 대기
 -> AdmissionResult(37, result=0) 이후에만 다음 단계 허용
 -> CharacterListRequest/Response(38/39)
 -> 저장된 캐릭터 선택 또는 CharacterCreateRequest/Response(40/41)
 -> CharacterSelectRequest/Response(42/43)
 -> EnterTownResponse(2) -> SkillStateResponse(29) -> InventoryStateResponse(45) + ItemDefinitionsResponse(48)
 -> Playing -> Game 생성
```

Google Authorization Code/PKCE(S256), 매 시도 새 state, Auth challenge의 nonce를 사용합니다. 콜백은 `127.0.0.1` 동적 포트의 `/oauth2/callback`입니다. Auth가 ID 토큰을 검증하고 계정을 결정합니다. Google access token을 게임 인증에 사용하지 않습니다.

Auth는 로그인과 타운 티켓만 담당합니다. 타운 인증 후 계정 소유의 저장 캐릭터 목록을 조회하고 선택해야 월드에 입장합니다. 생성 화면은 종류 ID 1~3과 UTF-8 1~32바이트 이름을 받습니다. 영속 characterId와 ownerGeneration은 목록에서 받은 값으로 선택하며 서버가 소유권을 검사합니다. 기존 EnterTownResponse의 playerId는 런타임 ID, characterId는 캐릭터 종류 ID입니다. 영속 ID와 혼용하지 않습니다.

타운과 캐릭터 목록은 ↑↓·클릭·휠로 선택하고 Enter로 연결/입장합니다. 캐릭터 생성 화면에서 이름과 종류를 입력합니다. 조회 실패 시 이전 목록을 유지하되 선택을 비활성화합니다. 생성/선택 결과가 불명확하면 변경 요청을 자동 재전송하지 않습니다. Google/티켓/타운·캐릭터 처리 대기 중 Esc는 연결을 취소하고, 타운 선택의 Esc는 로그아웃합니다. 계정 전환·연결 취소 시 캐릭터 캐시와 영속 ID를 지웁니다.

## 인벤토리와 장비

메뉴의 인벤토리를 열면 장비·재료·소모품·퀘스트 탭이 각각 8×5칸으로 표시됩니다. 무기·상의·하의·신발·반지·목걸이·팔찌는 각각 한 부위를 사용합니다. 가방과 장비창의 아이템 위에 마우스를 올리면 이름·설명·수량/최대 스택·장착 조건과 미충족 사유를 정보창에 표시합니다. 긴 내용은 해당 아이콘 위에서 휠로 스크롤하며, 빈 칸이나 아이콘 밖으로 이동하거나 탭을 바꾸거나 인벤토리를 닫으면 정보창을 숨깁니다. 장비 가방 우클릭은 장착/교체, 장비창 우클릭은 해제, 비장비 가방 우클릭은 해당 instanceId의 수량 1개 사용 요청입니다. 교체와 빈 칸 검사는 서버가 판정합니다. 던전에서도 사용할 수 있으며 전투는 계속되고 로컬 이동·공격 입력은 차단됩니다.

InventoryStateRequest/Response(44/45), InventoryOperationRequest/Response(46/47), ItemDefinitionsResponse(48)는 JSON 문자열 패킷입니다. 요청은 최대 4096바이트, 일반 응답은 60000바이트, 정의 응답은 16384바이트입니다. characterId·revision·ownerGeneration은 10진 문자열, requestId는 CSPRNG 32바이트의 소문자 64자리 hex, instanceId는 소문자 32자리 hex입니다. 패킷 ID는 서버가 생성한 TownPacket.generated.h와 일치해야 합니다.

상태가 먼저 도착하고 definitionBatchCount만큼 정의 배치를 받은 후, requestId·characterId·revision이 모두 일치하는 완전한 상태만 게임 스레드에서 적용합니다. 서로 다른 요청의 정의를 섞지 않습니다. 장착 조건과 비장비 최대 스택은 서버 정의를 사용하고, 서버 확정 전에는 슬롯·수량을 바꾸지 않습니다. 실패·타임아웃에는 기존 표시를 보존하고 최신 상태를 다시 조회합니다. DB 조회 실패를 빈 가방으로 처리하지 않습니다.

사용·판매·버리기 버튼과 버리기 수량/영구 삭제 확인 UI는 표시하지 않습니다. 서버의 버리기·판매 API는 유지합니다. 연결 끊김·최신 상태 확인 중·변경 요청 대기 중에는 우클릭 변경 요청을 보내지 않습니다. 사용 요청은 지정 instanceId와 수량 1개를 보내며, 투척에만 우클릭 순간의 facingLeft를 추가합니다. 아이템·HP·피해·투사체를 클라이언트가 먼저 변경하지 않습니다. 아이템 효과·장비 능력치 실제 적용, 상점·드랍 오브젝트·정렬은 이번 범위에 없습니다. 아이콘 파일이 없어도 서버 아이템을 숨기거나 제거하지 않고 이름 표시로 대체합니다. 2026-10-09 서버 Items.json에 낡은 총(WornPistol)과 설익은 딸기(UnripeStrawberry) 정의를 추가했습니다. 최초 아이템 작업은 정의와 아이콘 추가였으며, 2026-10-09 후속 승인으로 HP 회복·돌멩이 투척의 서버 사용 결과 연결을 추가했습니다. 자동 지급은 포함하지 않습니다. 클라이언트는 기존 정보창에서 서버의 이름·설명을 표시하고 수량을 임의로 추가하지 않습니다.

소비 아이템 정의의 useRequiredLevel/useCooldownSeconds를 정보창에 표시합니다. 설익은 딸기는 HP 30 회복·레벨 1·정의별 5초, 돌멩이(Stone)는 피해 10·레벨 1·최대 스택 20·정의별 1초입니다. 기존 무기의 장비 능력치 실제 적용은 후속 범위입니다. 돌멩이 아이콘이 없으면 기존 이름 표시를 사용하며 새 PNG나 키 할당을 추가하지 않습니다.

InventoryOperationRequest(46)의 조회는 {action:"UseStatus",requestId:"원래 요청 ID"}만 보냅니다. Pending은 처리 중이며 타임아웃·연결 끊김에도 원래 사용 ID를 유지합니다. 조회는 한 번씩 보내고, 응답이 없으면 조회만 재시도합니다. 새 Use를 자동 재전송하지 않습니다. 최종 Succeeded는 서버 인벤토리와 전투 상태로 확인하고, Cancelled는 서버가 확정한 아이템 상태를 다시 조회합니다. ConsumedUnknown은 소비 유지·효과 적용 여부 확인 불가로 안내하며 성공이나 환불로 표현하지 않습니다. DB/조회 장애와 불명확한 None 결과도 미실행으로 간주하지 않습니다. 서버의 성공한 UseStatus 조회에서 result=None/useState=None과 인벤토리가 함께 온 경우만 사용 기록 없음으로 처리해 잠금을 해제하고 최신 상태를 조회합니다. DB 상태가 이미 Applied/Cancelled/ConsumedUnknown이어도 result=Pending이면 서버 정리 완료까지 요청 잠금을 유지합니다. 초기 Use/useState=None의 UseLedgerFull은 던전 사용 기록 한도 거절로 안내하고 최신 상태를 조회합니다. Pending·무응답·연결 끊김을 겪은 요청은 뒤늦은 거절만으로 완료하지 않으며, StaleOwner는 소유권 확인 실패로 안내하고 사용 결과 잠금을 유지합니다. SpawnBlocked를 미소비 초기 거절로 취급하지 않고 DB 확정 Cancelled를 기다립니다.

45/47 응답의 cooldownsComplete=true인 cooldowns 배열은 정의별 전체 쿨타임 목록입니다. 각 행은 definitionId, cooldownState(Pending/Active/None), cooldownServerTimeMs, cooldownReadyAtMs, cooldownRemainingMs를 가집니다. 시각은 UTC epoch 밀리초 10진 문자열입니다. Pending의 만료 시각·남은 시간은 null, None은 만료 시각 null·남은 시간 0입니다. complete=false/cooldowns=null 또는 필드 누락은 기존 시간을 지우지 않고 재조회합니다. 완전한 빈 배열만 전체 대기 시간 없음으로 적용합니다. 쿨타임은 DB 최종 변경 SQL의 UTC 시각 샘플을 기준으로 하며 커밋 후 노출됩니다. 실시간 전투의 steady_clock serverTimeMs와 혼용하지 않습니다. 화면의 잔여 시간은 서버 샘플부터 표시용으로만 감소하고 동일 정의의 아이콘에 회색/잔여값을 표시합니다. 캐릭터 전환 시 초기화하고 방 이동·연결 끊김에서는 캐시를 보존한 뒤 서버의 전체 상태로 재확인합니다.

재접속의 CharacterSelectResponse(43, Pending)는 이전 아이템 사용 복구 대기로 표시합니다. pendingUseRequestId는 진단용 원래 사용 ID로 검증하며 선택 requestId와 혼동하지 않습니다. 같은 캐릭터 선택 요청을 유지하고 10초 선택 타임아웃을 중단합니다. 서버가 5초 간격으로 복구를 재조회하며, 최종 43 Succeeded → 2 EnterTownResponse 순서 후에만 입장합니다. ESC 취소와 연결 끊김 처리는 유지합니다. 클라이언트는 입장 전 UseStatus나 재선택 요청을 자동 전송하지 않고 복구 효과도 재실행하지 않습니다. 서버가 소유권·원래 방의 fencing을 확인하지 못하면 대기가 계속될 수 있습니다.

후속 사용 처리는 서버·DB V6 계약과 함께 사용해야 합니다. 이 클라이언트 작업에서 DB 마이그레이션이나 실제 사용을 실행해 검증하지 않았습니다.

빌드·기능 테스트·DB 접속/적용은 이번 구현에서 수행하지 않았습니다. 2026-10-08 총괄 확인 기준 실제 DB는 head 4이며 V5와 신규 프로시저 권한은 미적용 상태입니다. 사용자가 V5 마이그레이션과 권한을 별도로 적용한 뒤 새 서버와 클라이언트를 함께 사용해야 합니다.

## HUD 자원과 아이템 표시 규격

2026-10-11: 마을의 기존 SkillStateResponse는 hp/maxHp/mp/maxMp를 전달합니다. DungeonWorld 초기 자원과 전투 JSON/HUD1은 던전 자원이며 별도 캐시에 적용합니다. 늦은 마을 응답은 던전 HP·MP를 바꾸지 않습니다. MP의 두 필드가 null이면 미제공으로 표시하고, 최대값 0에서도 비율 나눗셈을 하지 않습니다. 기본 MP 100은 서버 Combat 정의의 값이며 클라이언트에 100 대체값을 넣지 않습니다.

던전의 tempoScale은 월드 규칙에서 받습니다. 타운의 기본 cooldown/버프·마감 시간과 UI 응답은 그대로 유지하고, 던전은 서버 reserve 단계에서 실제 cooldown 밀리초로 환산하여 기존 wall UTC ledger 마감을 기록합니다. 클라이언트가 받은 cooldownReadyAtMs·cooldownRemainingMs는 실제 시간이며 다시 감속하지 않습니다. 던전 상세창의 기본 cooldown만 수신 tempo로 환산하고 마을에서는 배율1을 사용합니다. 전투 시간/실제 시간의 나머지 단위는 [COMBAT_CLIENT.md](COMBAT_CLIENT.md)의 던전 전투 템포 절을 따릅니다.

아이템 퀵슬롯은 6칸이며 숫자 1~6 또는 해당 칸 우클릭으로 지정 instanceId를 1개 사용합니다. 사용 효과가 정의된 소모품만 인벤토리에서 드래그해 등록할 수 있습니다. Shift+우클릭은 등록 해제입니다. 등록은 실행 중 접속 캐릭터 ID별 메모리에만 유지하고, 스택 소진/제거 때 다른 스택으로 자동 교체하지 않습니다. 서버 권한·사용 레벨·쿨타임·Pending 및 동일 요청 ID 조회를 기존 인벤토리 요청 경로로 처리합니다. 메뉴/스킬 편집/파티/입장·클리어 대기 등 UI가 입력을 점유할 때 숫자 키로 사용하지 않으며, 아이템 숫자 키를 스킬 커맨드 기록에 섞지 않습니다.

2026-10-12 최신 정정 반영: 아이템6칸은 HP 구체 왼쪽에 한 줄로 배치합니다. 공통 `HudLayout::ITEM_SLOT_COUNT=6`, `SLOT_SIZE=40px`, `ICON_PADDING=3px`, `SLOT_GAP=4px`를 퀵슬롯·8×5 가방·7개 장비 칸·드래그에 사용합니다. 스킬도40px 칸/34px 아이콘으로 통일하며12칸 배치·키·저장 정책은 [COMBAT_CLIENT.md](COMBAT_CLIENT.md)의 HUD 절을 따릅니다. 원본64×64 RGBA PNG 전체 캔버스를 종횡비 유지·중앙 정렬로34×34px(배율0.53125)에 넣고1920에서는 전체UI1.5배로51px에 표시합니다. 아이템별 크롭·확대나 중간 PNG 사본은 만들지 않습니다. 짙은 갈색·검은 외곽·황동 안쪽 선·상단 하이라이트를 스킬과 통일합니다. 숫자 키/부위는 왼쪽 아래, 수량은 오른쪽 위, 재사용 대기는 가운데에 기준11px 글자(1920은16.5px)로 표시합니다. 긴 수량은 K/M/B,60초 이상 남은 시간은 올림한 분(m), 처리 중은 대기이고 툴팁은 정확한 수량·초를 유지합니다. 빈 장비 부위 이름과 아이콘 누락 시 기존 이름 표시도 유지합니다.

인벤토리는1280 기준 최대478×336px,1920에서는717×504px로 전체1.5배 표시합니다. 슬롯40/60px·아이콘34/51px·본문14/21px가 함께 커지며 인벤토리만 별도로 축소하지 않습니다. 내부 배치와 포인터는1280 기준 좌표로 정규화한 뒤 같은 viewport를 써서 표시·클릭·드래그를 맞춥니다. 가방·장비 내용이 넘치면 viewport에 잘라 표시하고 Shift+휠로 기준 한 행44px(1920의66px)씩 스크롤합니다. viewport 밖 칸 클릭·우클릭·드래그 시작은 차단합니다. 기존 아이콘 위 일반 휠은 정보창 스크롤입니다. 두 해상도와 letterbox의 입력/렌더 역변환은 정적으로 대조하며 실제 실행은 하지 않습니다.

## 연결 설정과 신뢰

기동 시 실행 파일 옆 `Assets/Data/AuthClient.json`을 읽습니다. 원본 [AuthClient.json](Assets/Data/AuthClient.json)은 빈 템플릿으로 보존합니다. 로컬 실행기 [RunLocalTest.bat](../../ActionRPGServer/RunLocalTest.bat)가 `%LOCALAPPDATA%/ActionRPG/LocalTest/settings.json`의 공개 client 항목을 매 실행 실제 EXE 옆 설정에 공급합니다. UTF-8 JSON의 최대 크기는 32KiB이며 최초 Google Desktop ID는 실제 등록값을 입력합니다. playerName·characterId는 인증 신원이나 입장 캐릭터를 결정하지 않습니다. 생성 화면에서 이름을 새로 입력하고 종류 1~3을 선택합니다. 별도 override 기능이나 게임 명령줄 인증 인자는 사용하지 않습니다.

로컬 실행기가 클라이언트 프로세스에만 선택적 환경변수 ACTIONRPG_GOOGLE_CLIENT_ID와 ACTIONRPG_GOOGLE_DESKTOP_CLIENT_SECRET을 전달합니다. 클라이언트는 시작 시 두 값을 읽고 자기 환경에서 제거합니다. secret이 있으면 두 값 모두 1~1024자의 공백 없는 printable ASCII여야 하며, 환경의 ID가 JSON의 googleClientId와 정확히 일치해야 합니다. 잘못된 값이나 ID 불일치는 로그인 설정 오류로 처리합니다. 검증된 secret은 메모리에만 두고 Google 토큰 교환 form에 URL 인코딩해 client_secret으로 추가합니다. secret이 없으면 기존 PKCE 흐름을 유지합니다. JSON·명령줄·로그·오류 메시지에는 secret을 기록하지 않으며, PKCE·loopback·Auth ID 토큰 검증은 그대로 사용합니다.

재빌드의 CopyRuntimeAssets와 에셋 미러 동기화는 빈 원본 설정을 다시 복사할 수 있으므로, 이후 실행기로 사용자 설정을 재공급합니다. 사용자 설정의 타운 CA 절대 경로를 그대로 사용합니다. 실행용 JSON에는 아래 공개 필드만 포함하며, DB 접속 정보·타운 등록키·인증서 개인 키는 공급하지 않습니다. 로컬 Auth HTTPS의 인증서 신뢰 준비는 실행기에서 CurrentUser 범위로 담당하고, 타운 TLS는 아래 townCaFile을 사용합니다. 변경된 설정은 클라이언트 재시작 시 읽습니다.

| 필드 | 의미·조건 |
| --- | --- |
| `authUrl` | HTTPS origin. 경로·쿼리·사용자 정보 없이 실제 Auth 호스트·포트 |
| `googleClientId` | Google Desktop app ID. Auth의 `ACTIONRPG_GOOGLE_CLIENT_ID`와 일치 |
| `townCaFile` | 타운 신뢰 CA PEM. 절대 경로 또는 Assets 기준 상대 경로 |
| `playerName` | 기존 설정 호환 필드. 저장 캐릭터 이름을 덮어쓰지 않음 |
| `characterId` | 생성 화면의 기본 캐릭터 종류 1~3 |
| `servers[]` | 타운 목록. 각 항목에 `serverId/name/hostname/port` |
| `serverId` | Auth에 등록한 ID와 타운의 `ACTIONRPG_TOWN_ID` |
| `hostname/port` | 타운 인증서 SAN과 일치하는 DNS/IP 및 TLS 포트 1~65535 |

Auth·Google HTTPS는 WinHTTP의 Windows 인증서 저장소로 검증합니다. Town TLS는 Asio/OpenSSL과 `townCaFile`로 체인·호스트명을 검증하며 TLS 1.2 이상을 사용합니다. 타운 CA 파일 설정은 Windows의 Auth HTTPS 신뢰를 변경하지 않습니다. 인증서 검사 무시·평문 fallback은 구현하지 않았습니다. 서버 개인 키·타운 비밀·DB 접속 문자열은 클라이언트 설정에 넣지 않습니다.

토큰과 티켓은 메모리에만 두며 설정·로그·URL에 기록하지 않습니다. 게임 토큰은 응답 수명 최대 8시간, 티켓은 최대 30초, Google challenge는 최대 300초입니다. 요청 시작 시각을 기준으로 만료를 보수적으로 판단합니다. 토큰 영구 저장·자동 갱신·재기동 자동 로그인은 없습니다.

실행기가 여는 두 클라이언트는 서로 다른 실제 Google 계정으로 수동 로그인합니다. 브라우저가 같은 계정을 자동 선택하면 타운 선택 화면의 계정 전환을 사용해 다음 로그인에서 계정을 선택합니다. 같은 계정의 새 로그인은 기존 토큰을 무효화하므로 기존 클라이언트의 재로그인이 필요할 수 있습니다. playerName·characterId를 바꾸는 것은 계정 전환이 아닙니다.

## 새 PC 인증 준비와 문제 해결

MySQL·Auth·Town·Room·클라이언트를 새 PC에 모두 로컬 설치하는 전체 순서는 [서버 통합 설치·문제 해결 가이드](../../ActionRPGServer/docs/workflows/LOCAL_DEVELOPMENT_SETUP.md)를 따릅니다. 서버는 v145, 클라이언트는 v143 도구 집합으로 Debug/x64를 준비합니다. 원격 서버 접속과 기존 DB 데이터 이관은 별도 절차입니다.

로컬 실행기의 공개 연결 설정은 `%LOCALAPPDATA%/ActionRPG/LocalTest/settings.json`, 보호된 자격 증명은 같은 폴더의 `credentials.dpapi`에 둡니다. Google Desktop ID/secret 쌍은 보호된 객체에 저장하며 기존 DB·타운 자격 증명을 보존합니다. 기존 프로필에 쌍이 없으면 실행기가 secret을 숨김 입력으로 한 번 요청합니다. secret은 1~1024자의 공백 없는 printable ASCII이고, 다른 Google client ID에 연결된 secret은 사용하지 않습니다.

DPAPI 파일과 인증서 개인 키는 다른 PC나 Windows 사용자에게 복사해 재사용하지 않습니다. 새 PC에서는 실행기의 최초 설정으로 사용자 프로필·로컬 인증서·CurrentUser 신뢰 등록을 새로 준비하고, Google 자격 증명과 서버 설정을 해당 PC에서 입력합니다. 기존 settings.json의 절대 경로·인증서 thumbprint도 새 PC에 그대로 적용하지 않습니다. DB 데이터 이전은 이 인증 프로필 이전과 별개이며 서버 문서를 따릅니다.

Auth HTTPS는 Windows 인증서 저장소의 신뢰를 사용합니다. Town TLS는 실행용 JSON의 townCaFile PEM과 hostname/SAN 일치를 사용합니다. 한쪽 CA 설정만 변경해서 다른 쪽 인증서 오류를 해결할 수 없습니다. 로컬 CA 신뢰는 실행기가 CurrentUser 범위로 준비합니다. 원격 배포는 운영자가 제공한 CA와 인증서의 실제 호스트 이름을 사용하며 인증서 검사를 끄지 않습니다.

### 실제 사례: Google 토큰 교환 HTTP 400

실제 게임 화면에서 `[Google 토큰 교환] HTTP 400 / OAuth: invalid_request / Google 클라이언트 자격 증명 누락`이 확인됐습니다. 마지막 사유는 응답 설명이 정확히 `client_secret is missing.`일 때만 내부 분류로 표시됩니다. 일반 invalid_request만으로 이 원인을 확정하지 않습니다. 수정 전 token form에는 client_secret이 없었으며, 수정 후 실행기가 검증된 ID/secret 쌍을 전달하고 클라이언트가 secret을 URL 인코딩해 추가합니다. PKCE(S256)·state·nonce·동일 loopback redirect_uri와 Auth에 대한 ID 토큰 제출은 유지합니다.

**오류 안내만 추가한 이전 EXE에는 secret 전달 코드가 없습니다.** 런처·소스가 수정되어도 그 EXE를 계속 실행하면 같은 400이 발생할 수 있습니다. 아래 명령은 이 문서 폴더에서 사용자가 확인할 수 있는 시각 대조 예시입니다.

```powershell
Get-Item ./ActionRPGClient/Network/AuthSettings.cpp, ./ActionRPGClient/Network/AuthClient.cpp, ./artifacts/bin/x64/Debug/ActionRPGClient.exe |
    Select-Object FullName, LastWriteTime
```

EXE가 수정 소스보다 오래됐다면 **Debug | x64 다시 빌드**가 필요합니다. 시각이 최신인 것만으로 코드 반영을 보장하지 않으므로 빌드 성공·실제 EXE 경로를 함께 확인합니다. 런처는 `ActionRPGClient/artifacts/bin/x64/Debug/ActionRPGClient.exe`를 실행합니다. Release EXE나 저장소 루트 artifacts의 라이브러리 출력과 혼동하지 않습니다. 다시 빌드한 뒤에는 수정된 실행기로 공개 JSON과 프로세스 환경을 재공급합니다.

2026-10-07 사용자가 수정 적용 후 실제 Google 로그인과 정상 입장을 확인했습니다. 에이전트가 빌드·실행·DB 접속으로 재검증한 것은 아닙니다.

| 증상 | 확인 및 대응 |
| --- | --- |
| 브라우저에 콜백 수신 안내가 나오지만 게임에서 실패 | 콜백 수신 뒤에도 Google 토큰 교환·Auth 검증이 남습니다. 게임의 실패 단계·HTTP 숫자·허용된 OAuth 코드만 확인합니다. |
| Google 토큰 교환 400 + 자격 증명 누락 | 위 실제 사례의 소스/EXE 시각·Debug x64 재빌드·수정 런처의 ID/secret 공급을 확인합니다. secret 값이나 응답 원문을 공유하지 않습니다. |
| 로그인 설정 오류 | 실행 EXE 옆 공개 JSON, ID 일치, secret 길이·문자 범위를 로컬에서 확인합니다. 원본 JSON에 secret을 추가하지 않습니다. |
| Auth 준비 단계 또는 로그인 검증의 503 | 서버 준비·배포 상태를 확인한 뒤 새로 시도합니다. Google secret 누락으로 단정하지 않습니다. |
| Auth 로그인 검증·타운 티켓의 403 | 표시된 실패 단계를 기준으로 서버의 거절 원인을 확인합니다. Google 토큰 교환 오류와 구분합니다. |
| TLS 실패·HTTP 결과 미확인 | 실제 주소·연결 상태·호스트 이름·Auth Windows 신뢰·Town CA 파일을 각각 확인합니다. |
| vcpkg manifest disabled 안내 | 그 문구만으로 빌드 실패 원인을 확정하지 않습니다. 첫 error/LNK 줄을 확인하고 manifest 사용과 MSBuild 연동을 확인합니다. |
| LNK1168: EXE를 쓰기용으로 열 수 없음 | 실행 중이거나 남아 있는 ActionRPGClient 프로세스를 확인해 종료한 뒤 다시 빌드합니다. 창을 닫은 것과 프로세스 종료는 별도로 확인합니다. |
| 재빌드 후 주소·ID 설정이 비어 있음 | 빈 원본 템플릿이 복사될 수 있습니다. RunLocalTest로 실행용 설정을 다시 공급합니다. |
| 같은 계정의 다른 클라이언트가 끊김 | 새 로그인이 이전 토큰을 무효화할 수 있습니다. 두 실제 계정을 사용하고 필요하면 계정 전환 후 다시 로그인합니다. |

문의 시에는 단계·HTTP 상태·허용된 OAuth 코드·고정된 분류 사유와 빌드 구성/실행 경로를 남깁니다. secret·인증 코드·토큰·error_description·응답 원문·verifier·nonce·state는 포함하지 않습니다.

## 취소·실패·세션 전환

HTTPS 전체 요청 한도는15초이고 초기 타운 TLS·티켓 응답 대기는 티켓 잔여 시간과15초 중 작은 한도를 적용합니다. 서버가 SessionStatus.code=1을 보낸 경우에만 기존 접속 정리 대기로 전환합니다. 서버 대기 한도는30초이며, 클라이언트는 첫 대기 알림부터 최종 사유 전달 여유5초를 포함한35초로 연결을 제한합니다. 반복 알림으로 이 제한을 연장하지 않습니다. 캐릭터 선택·목록 및 선택 후 월드 입장 확인은 기존10초 한도를 유지합니다. 브라우저 대기는 challenge 만료까지입니다. UI 취소는 대기 화면을 즉시 빠져나오고 늦게 완료된 결과는 시도 ID로 거릅니다. 불확실한 HTTP 결과를 자동 재시도하지 않습니다.

`ready=false`도 받은 유효 티켓으로 Town에 연결합니다. Pending은 인증/게임 입장 성공이 아니며 같은 연결에서 서버 정리 결과를 기다립니다. 대기 중에는 캐릭터 목록·게임 패킷을 보내지 않고 AdmissionResult.result=0 뒤에만 정상 캐릭터 선택을 시작합니다. 대기 알림의 retryAfterMs는 서버 안내 값으로 보관하며 클라이언트가 재조회·새 티켓 발급에 사용하지 않습니다. 대기 취소·시간 초과는 현재 연결과 로컬 인증을 정리하고 로그인 화면으로 돌아갑니다. 재로그인·티켓 자동 재발급은 하지 않습니다. HTTP 오류는 로그인 준비·Google 브라우저 인증·Google 토큰 교환·Auth 로그인 검증·타운 입장 티켓·로그아웃 단계를 구분합니다. HTTP 응답을 받은 거절에는 상태 숫자를 표시하고 Google 토큰 교환은 허용 목록과 일치하는 OAuth error 코드만 추가합니다. Google error가 invalid_request이고 error_description이 정확히 client_secret is missing.과 일치할 때만 내부 enum으로 분류해 고정 문구 “Google 클라이언트 자격 증명 누락”을 추가합니다. 다른 설명은 버립니다. 응답 원문·error_description·인증 코드·토큰·verifier·nonce·state는 출력하지 않습니다. Auth의 503은 준비 중으로 안내하며, 오류 코드만으로 client_secret 누락 등 특정 원인을 단정하지 않습니다. 브라우저의 콜백 수신 문구는 게임에서 검증이 진행 중임을 알리고 로그인 성공으로 표시하지 않습니다. 명시적인 SessionStatus 종료 코드2~8은 고정된 사유 문구로 표시합니다. 단순 EOF·연결 종료만으로 중복 로그인·정지 등 상세 이유를 추측하지 않습니다. 기존 TCP 자동 연결·자동 재접속 경로는 제거되었습니다.

SessionStatus는 기존ID1~51 뒤에 추가한52입니다. body는 ID:u16 + code:u8 + retryAfterMs:u32의7바이트이고 기존big-endian·외부4바이트 길이 framing을 유지합니다. code1=WaitingForPriorOwner,2=Replaced,3=AdmissionTimeout,4=AuthUnavailable,5=AuthorizationExpired,6=LoggedOut,7=ServerTransfer,8=AccountUnavailable입니다. 디코더는 코드 범위·잘린 데이터·여분 바이트를 검증하며 code1은 승인 전 상태에서만 허용합니다. 서버 소유 TownPacketDefine.yml의 동일 생성기를 사용하되 서버 담당은 서버 TownPacket.generated.h만, 클라이언트 담당은 클라이언트 헤더만 갱신합니다. 클라이언트는 수동 TownProtocol 디코더를 연결합니다. 이전 클라이언트에는 이 대기 흐름이 없으므로 같은 계약으로 배포합니다.

code2는 “다른 곳에서 접속하여 연결이 종료되었습니다”를 표시합니다. 코드3~8도 시간 초과·인증 서버 불가·인증 만료·로그아웃·서버 이동·계정 확인 불가를 구분합니다. 종료 사유는 TownConnectionInfo의 mutex 보호된 스냅샷으로 게임 스레드에 전달하고, 세션 close 이후의 읽기/쓰기/타이머 콜백이 덮어쓰지 않습니다. LoginFlow는 사유를 복사한 뒤 시도ID·로컬 토큰·Town/Dungeon 연결·캐릭터 문맥을 정리합니다. 종료 안내가 일반 연결 끊김이나 Auth logout 완료 문구로 바뀌지 않습니다. 서버에 의해 교체된 접속은 이전 토큰으로 /logout을 보내지 않고 로컬 상태만 정리하며 새 접속의 소유권을 건드리지 않습니다. 던전 정리는 기존 비동기 완료 확인을 사용합니다.

| 메뉴 | 처리 |
| --- | --- |
| 로그아웃 | Town/Dungeon 종료 요청·Game 폐기·Auth logout·로컬 토큰 제거 |
| 계정 전환 | 같은 정리 후 다음 Google 로그인에 계정 선택 요청 |
| 타운 변경 | Game/연결을 정리하고 유효한 gameToken만 유지. 목적 타운의 새 티켓 발급 |
| 게임 종료/창 닫기 | Application 소멸 과정에서 Auth worker·Town I/O·RUDP worker 정리 |

서버 logout 실패·불확실한 결과는 화면에 표시하며 로컬 로그아웃을 되돌리지 않습니다. 던전 종료는 `RequestStop` 후 매 tick `IsStopComplete`를 확인하여 UI 스레드의 blocking join을 피합니다. 프로세스 최종 정리는 별도입니다.

Auth 결과·타운 송신·수신 이벤트에는 시도 ID가 적용됩니다. 새 시도에서 이전 이벤트를 비우고, 타운 stream·읽기/쓰기 버퍼는 연결별 shared_ptr 수명을 유지합니다. WinHTTP callback 상태는 HANDLE_CLOSING까지 유지합니다. 새 Game에 이전 계정의 이벤트를 적용하지 않습니다.

## 서버·DB와 클라이언트의 책임

AuthServer/DB는 Google 계정 식별과 로그인 상태를 관리하고, TownServer는 접속 중 캐릭터·스킬 진행값을 결정합니다. 클라이언트에는 DB 호출·캐릭터 저장/복원 API가 없습니다. 로컬 파일은 스킬 **단축키 배치**만 저장하며 학습 권한·레벨·SP의 원본이 아닙니다.

2026-10-06 대조한 `TownInstance::EnterOnStrand`는 `progressionPolicy.Create()`로 입장 진행값을 만듭니다. 따라서 이 시점의 코드만으로 타운 변경·재로그인 시 캐릭터 진행값의 DB 복원을 보장할 수 없습니다. 영속 저장/이관의 추가 구현과 배포 상태는 서버·DB 담당 문서를 기준으로 확인해야 합니다. 클라이언트의 타운 변경 UI 구현을 DB 저장 구현으로 해석하지 않습니다.

## 패킷 원본과 framing

타운 패킷 ID·필드 선언의 원본은 서버 [Tool/TownPacketDefine.yml](../../ActionRPGServer/Tool/TownPacketDefine.yml)입니다. 양쪽 `TownPacket.generated.h`를 생성하고, 실제 바이트 인코딩/디코딩은 클라이언트 `TownProtocol.*`와 서버 `Protocol.*`에서 구현합니다. 생성 파일은 직접 수정하지 않습니다.

```text
[uint32 bodySize, big-endian]
[uint16 packetType, big-endian][payload]
문자열: [uint16 UTF-8 byteLength][bytes]
```

구조체 메모리를 그대로 보내지 않습니다. body와 송신 대기열은 각각 최대 1MiB, 수신 이벤트 큐는 최대 4096개입니다. 한 문자열은 uint16 길이 범위이므로 body 한도와 혼동하지 않습니다. malformed packet이나 큐 초과는 연결 실패로 처리합니다. 정상 콘텐츠 거절은 결과 코드로 처리합니다. 타운 framing에는 버전 협상이 없으므로 서버·클라이언트를 같은 계약으로 배포해야 합니다.

입장 티켓 패킷 36의 body는 ID 2 + 문자열 길이 2 + 64바이트 ticket = 68바이트, 패킷 37은 ID 2 + result 1 = 3바이트입니다.

| ID | 현재 패킷 |
| --- | --- |
| 1~6 | EnterTownRequest/Response, MoveInput, PlayerAppear/Move/Disappear |
| 7~11 | ConfirmDungeonJoin, EnterDungeonRequest/Response, MapChanged, DungeonSelectionOpen |
| 12~18 | PartyInviteRequest/Answer, PartyLeaveRequest, PartyKickRequest, PartyInvitation, PartySnapshot, PartyOperationResult |
| 19~24 | PartySettingsRequest, PartyDirectoryPageRequest/Unsubscribe/Page/Changed, PartyCreateRequest |
| 25~26 | DungeonCompletionRequest/Response |
| 27~29 | SkillStateRequest, LearnSkillRequest, SkillStateResponse |
| 30~35 | PartyDetailRequest/Response, PartyJoinRequest/Answer/RequestUpdate, PartyKicked |
| 36~37 | AdmissionTicketRequest, AdmissionResult |
| 38~43 | CharacterList/Create/Select Request/Response |
| 44~48 | InventoryState/Operation Request/Response, ItemDefinitionsResponse |
| 49~51 | ChatRequest, ChatResult, ChatMessage |
| 52 | SessionStatus |
| 53~56 | QuestStateRequest/Page, QuestActionRequest/Result |

마을 TCP/TLS의 big-endian framing을 던전 NetBuffer나 실시간 payload 직렬화에 적용하지 않습니다. 던전 계약은 [COMBAT_CLIENT](COMBAT_CLIENT.md)에서 따로 설명합니다.

## 송수신과 게임 이벤트

```text
게임 스레드 Send API
 -> TownProtocol::Encode (호출 스레드)
 -> asio::post(strand), 시도 ID/입장 완료 확인
 -> QueuePacket -> TLS async_write

Town network strand
 -> TLS async_read -> Impl::Session::HandlePacket -> Decode
 -> PushEvent (mutex 큐, 시도 ID 확인)
 -> ConsumeEvents -> GameWorld::ProcessNetworkEvents (게임 스레드)
```

네트워크 스레드에서 Player·GameWorld·UI·렌더러를 직접 변경하지 않습니다. 연결 상태·시도 ID는 atomic, 이벤트·상태 묶음은 mutex로 보호합니다. 각 연결의 소켓·송신 큐·deadline은 strand에서 접근합니다. 파일 로드·UI 처리·게임 보간은 strand 밖의 게임 스레드에서 합니다.

LoginFlow가 연결 ready를 확인한 후 Game이 입장 응답 이벤트를 소비합니다. 던전에서도 Town 연결은 입장 승인·파티·스킬·클리어/복귀 제어를 위해 유지합니다. 클리어 후 RUDP 정리 중 수신한 타운 이벤트는 보류했다가 초기화 완료 후 처리합니다.

## 파티·스킬·던전 콘텐츠 연결

- 공개 파티 목록은 페이지·revision을 사용하고, 상세 응답은 선택 partyId와 현재 UI 상태를 확인합니다. 가입 요청은 requestId별 Update와 Answer로 처리합니다.
- 초대와 가입 요청은 서로 다른 계약입니다. 승인/거절 대기 중 중복 전송을 막고, 추방 알림은 `PartyKicked`로 표시합니다. `PartyOperationResult`에는 requestId가 없으므로 다른 대기 요청을 임의로 완료시키지 않습니다.
- 던전 입장 중부터 파티 UI와 스킬 학습을 제한합니다. 서버도 예약 상태를 포함해 허용 여부를 검사합니다.
- `SkillStateResponse.payload`는 `characterId/progression/skillTrees/playerSkills/result` JSON입니다. 클라이언트 ID·카탈로그 일치와 트리/진행값을 검증합니다.
- `LearnSkillRequest`는 skillId와 expectedSkillLevel을 보냅니다. 서버 응답으로만 SP·스킬 레벨을 확정합니다. 10초 무응답은 상태 재조회로 복구합니다.
- 던전 입구는 서버가 `DungeonSelectionOpen`을 보내고 클라이언트가 `EnterDungeonRequest(zoneId,dungeonId)`를 보냅니다. 제공된 목록만 표시합니다.
- RUDP challenge는 타운 `ConfirmDungeonJoin`으로 확인합니다. 클리어 선택은 `DungeonCompletionRequest(roomId,retry)`와 Response로 제어합니다.

## 새 패킷을 연결하는 순서

1. 서버 담당과 ID·방향·필드·범위·거절 결과·세션/중복 처리 규칙을 확정합니다. 현재 ID 1~37은 재사용하지 않습니다.
2. 서버 YAML 원본을 갱신하고 기존 [패킷 생성 도구](../../ActionRPGServer/Tool/README.md)로 양쪽 헤더를 생성합니다.
3. 서버 Protocol의 Encode/Decode와 PlayerSession 라우팅·도메인 검증을 구현합니다.
4. 클라이언트 TownProtocol의 Encode/Decode에 같은 필드 순서와 자료형을 사용합니다. 타입·범위·필수 필드·잔여 바이트를 검사합니다.
5. TownClient Send API에서 인코딩한 패킷을 strand로 보내고 기존 시도 ID·ready 검사를 유지합니다.
6. `Impl::Session::HandlePacket`에서 디코딩한 이벤트를 `TownEvent` variant로 전달하고 GameWorld 또는 해당 UI 모델에서 소비합니다.
7. 해당 계약에 필요한 요청 식별·pending·시간 초과·연결 정리를 구현합니다. 기존 모든 요청에 requestId가 있다고 가정하지 않습니다.
8. 생성 결과·직렬화·콜백 수명을 정적으로 대조한 뒤 양쪽 빌드/실제 왕복 검증을 별도 수행합니다.

새 UI 기능에 네트워크 소켓 접근을 노출하지 않으며, 새 요청 ID를 예시 숫자로 임의 확정하지 않습니다. 접속이 끊기면 pending을 정리하고 새 티켓/로그인 흐름으로 복구해야 합니다.

## 검증 기록과 남은 확인

이번 문서 갱신은 최신 소스·생성 계약·설정·경로를 정적으로 대조한 작업입니다. 2026-10-06 이전 작업에서는 NASM 3.01 실행/CMake 검색, OpenSSL 3.6.1#3 바이너리 캐시 설치, Debug x64 컴파일·링크와 임시 PATH/manifest 강제 옵션 없는 일반 빌드 성공을 확인했습니다. 최초 NASM 검색 실패 원인은 재현하지 못했고 전역 PATH·vcpkg 도구 스크립트는 수정하지 않았습니다. 첫 컴파일의 기존 RUDP 공용 헤더 `NetServerSerializeBuffer.h` C4828 경고는 별개입니다.

이번에는 Release 빌드·기능 테스트·클라이언트/서버 실행·실제 인증 왕복·DB 확인을 수행하지 않았습니다. 이전 빌드 성공을 서버 준비나 DB 마이그레이션 적용의 근거로 사용하지 않습니다.

별도 실행 확인에는 로그인/거절/503/ready=false, 단계별 취소·역순 완료·만료·TLS 거절, 로그아웃 실패, 마을/던전에서 계정·타운 전환, 대기 중 창 닫기, 파티 동시 요청과 스킬 응답 지연이 포함됩니다. 실제 주소·CA·Google 등록 및 서버·DB 배포 상태는 운영 설정과 담당 문서에서 확인합니다.

코드 근거: [LoginFlow](ActionRPGClient/App/LoginFlow.cpp), [AuthClient](ActionRPGClient/Network/AuthClient.cpp), [AuthHttps](ActionRPGClient/Network/AuthHttps.cpp), [AuthSettings](ActionRPGClient/Network/AuthSettings.cpp), [TownClient](ActionRPGClient/Network/TownClient.cpp), [TownProtocol](ActionRPGClient/Network/TownProtocol.cpp), [TownEvent](ActionRPGClient/Network/TownClient.h), [GameWorld](ActionRPGClient/Game/GameWorld.cpp).

## 채팅 (2026-10-12 추가 계약)

Town TLS의 기존 4바이트 body length 및 big-endian 필드 인코딩을 사용합니다. 원본은 서버 `Tool/TownPacketDefine.yml`이며 클라이언트 `TownPacket.generated.h`는 같은 생성기로 갱신합니다. 기존 ID 1~48은 유지합니다.

| ID | 패킷 | 필드 순서 |
|---|---|---|
| 49 | ChatRequest | sequence u32, channel u8, message UTF-8 string |
| 50 | ChatResult | sequence u32, result u8, actual channel u8, messageId u64, retryAfterMs u32 |
| 51 | ChatMessage | messageId u64, requestSequence u32, actual channel u8, senderPlayerId u64, senderCharacterId u64, senderName string, message string |

문자열은 u16 UTF-8 바이트 길이 접두사입니다. General=1, Party=2. 결과는 Succeeded=0 / InvalidChannel=1 / InvalidMessage=2 / RateLimited=3 / NotInParty=4 / NotEntered=5 / StaleSequence=6 / SequenceConflict=7 / Unavailable=8입니다. 서버의 수신 계약은 200 Unicode code points·UTF-8 800바이트이며 서버가 빈 문장, 제어·개행·bidi 문자 및 전송 빈도(초당 2회, burst 5)를 검증합니다. 클라이언트 실제 입력 한도는 50 Unicode code points·UTF-8 200바이트로 더 엄격하게 제한합니다. 한도 도달 시 별도 안내를 출력하지 않고 추가 입력만 버립니다. 입력줄의 IME 조합 미리보기도 같은 한도를 적용합니다. 서버 계약 원본과 수신 코덱은 변경하지 않으므로 이전 클라이언트의 긴 메시지를 받아도 기록 내용은 유지합니다.

마을 일반은 현재 같은 맵의 근방 3×3 섹터, 파티는 장소 무관 현재 파티원입니다. 던전 및 입장 전환 중 일반 요청은 서버가 Party로 확정합니다. 클라이언트도 실제 입력 채널을 파티로 표시하며 파티가 없으면 전송을 막고 안내합니다. 일반 탭의 과거 기록 열람은 가능하며 일반 탭에서 보낸 본인 메시지가 Party로 승인되면 파티 탭으로 이동합니다. 다른 채널로 대체 전송하지 않습니다.

요청 순번은 Town 연결별로 1부터 증가하고 새 연결에서 초기화합니다. 성공 ACK는 별도 본문 표시를 만들지 않습니다. 본인도 서버 ChatMessage로만 한 번 표시합니다. 수신 이벤트는 기존 attempt로 태그된 TCP 큐를 거쳐 게임 스레드에서 적용합니다. messageId는 현재 Town 수명의 단조 순번이며 번호 누락은 채널/AOI 수신 범위에 따른 정상 상태입니다. 이미 적용한 ID와 그 이전 ID는 보관에서 제거된 뒤에도 재표시하지 않습니다. 새 Game/연결에서는 기록·중복 기준을 새로 만들며 서버 재시작 순번을 이어 쓰지 않습니다. 전송 대기 결과는 최대 8건·5초, 자동 재전송은 하지 않습니다. 실패·연결 종료·응답 만료는 입력줄에 안내합니다.

UI는 공통 200건 deque에서 전체/일반/파티로 필터링합니다. 기본 400×180, 기록 14px, 탭 24px, 입력줄 28px이며 1920에서 공통 배율 1.5를 적용합니다. 평소 글씨만, hover/입력/드래그 중에는 반투명 배경입니다. 일반은 흰색, 파티는 하늘색이며 기록·입력 채널·작성 본문·말풍선에 같은 의미 색상을 적용합니다. 전송 실패/제한 안내는 금색, 조작 도움말과 전체 탭은 회색입니다. 얇은 검은 글자 외곽선과 선택 탭 밑줄로 밝은 맵에서도 구분합니다. 우측 스크롤바·휠, 빈 배경/상단 드래그로 이동, 하단 4px 핸들로 높이를 조절합니다. 높이 120~360 및 화면/HUD 상한을 적용하고 과거 기록을 읽는 동안 새 메시지는 읽던 행을 유지합니다.

Enter로 입력·전송, Esc로 취소합니다. 채팅 입력 중 Tab은 일반 → 파티 → 일반 순환이며 선택 기록 탭도 해당 채널로 전환합니다. 작성 중인 본문·IME 조합은 유지하고 길게 누를 때 자동 반복으로 채널이 바뀌지 않습니다. 전체 탭은 마우스로 선택하는 기록 필터입니다. 던전에서는 Tab으로 일반 기록을 열어도 입력 채널 표시와 실제 전송은 파티로 유지합니다. 조합 중 Esc는 조합부터 취소합니다. 일반 한글 입력에서는 Enter 한 번으로 IME 조합을 확정한 뒤 전송합니다. 후보 선택 창이 열렸을 때의 Enter는 IME 후보 선택만 처리하고 전송하지 않습니다. 전송 요청은 Win32 메시지 처리가 끝나고 확정 문자열 이벤트가 큐에 들어온 뒤 추가하며, 확정 실패·새 키 입력·포커스 취소는 보류한 전송을 취소합니다. IME 조합 결과와 새로운 preedit가 함께 도착하면 둘을 별도로 유지하며 WM_CHAR의 CR은 중복 전송을 만들지 않습니다. IME 조합/확정 결과를 별도로 받고 UTF-16 surrogate pair와 UTF-8 바이트 제한을 확인합니다. 채팅 입력 동안 이동·공격·스킬·아이템·커맨드 키는 차단하며 메뉴/로그인/슬롯 드래그가 우선합니다. 입력줄 바깥 클릭은 입력을 취소하고 기존 UI로 전달합니다. 캡처된 채팅 포인터 이벤트는 퀵슬롯으로 다시 전달하지 않습니다.

현재 화면에 보이는 발신 캐릭터에만 4초 말풍선을 붙입니다. 말풍선은 12px, 최대 본문 폭 220px·측정 기준 3줄, 어두운 배경/밝은 얇은 테두리입니다. 넘치는 본문은 surrogate pair를 유지해 줄이고 말줄임표로 표시하며 전체 내용은 기록에서 볼 수 있습니다. 머리 위 위치는 발에서 고정 100px 또는 더 큰 달리기 시트의 최대 높이 대신 각 캐릭터의 대기 스프라이트 renderHeight/anchor 또는 frame pivot/scale로 계산하고 점프 높이·카메라를 적용합니다. 머리 위 10px 간격을 두며 화면 위쪽에 공간이 없으면 캐릭터 위에 겹쳐 내리지 않고 말풍선을 숨깁니다. 다른 장소나 화면 밖 발신자는 기록만 표시합니다. 방/맵 이동과 연결 종료에서는 말풍선을 비우며 기록은 같은 Game 세션 동안 유지합니다. 채팅과 말풍선은 월드 위, 기존 메뉴/인벤토리/스킬 창 아래에 렌더링합니다. 채팅 시간은 실제 시간이며 던전 0.7 전투 템포를 적용하지 않습니다.

이번 검증은 생성 선언·코덱 필드 순서·스레드 소유·입력/배율/렌더 경로의 정적 검토입니다. 빌드·실행·IME 실기·시각 캡처·두 클라이언트 AOI/파티 전송 테스트는 수행하지 않았습니다.

## 중복 접속 연동의 정적 확인 범위

2026-10-14: ready=false 티켓 보존, AdmissionResult0 이전 송신 제한, SessionStatus의 패킷 크기/필드와 코드 매핑, 첫 대기 알림 기준 제한·반복 알림 미연장, 취소/종료 시도ID 정리와 종료 사유 유지 경로를 소스로 대조했습니다. 기존 게임 스레드/strand·mutex 전달을 유지하고 DB/던전 소유권 해제 확인을 우회하지 않습니다. 빌드·실행·기능 테스트·DB 조회/적용·프로세스 종료·커밋·푸시·실행 폴더 배포는 수행하지 않았습니다. 실제 두 클라이언트의 교체, 정리 지연/실패, 패킷 전달 직후 EOF, 마을/던전 종료, 대기 취소/시간 초과 및 모든 종료 코드의 표시·서버 일회용 티켓/소유권 판정은 실행 미검증입니다.


## 퀘스트 v1

- TownPacketDefine.yml의 기존 1~52는 유지하며 53 QuestStateRequest, 54 QuestStatePage, 55 QuestActionRequest, 56 QuestActionResult를 사용한다. 네 패킷 모두 외부 BE u32 길이 + body BE u16 ID/u16 UTF-8 JSON 길이이며 요청 2 KiB, 응답 14 KiB/page, 최대 48 page·합계 512 KiB다. 클라이언트 generated 헤더는 서버 YAML로 생성한다.
- 퀘스트 정의는 서버 StatePage에서 수신하며 별도 로컬 Quests.json은 배포하지 않는다. 정의는 id/version/title/description/category/repeat/accept/complete/event/steps/fixedRewards/choiceRewards이며 수락 당시 snapshot을 표시한다. 현재 catalog 64종 외에 이전 배포의 live snapshot 16종을 받을 수 있어 최대 80행/정의를 허용한다. 목표는 StatePage.objectives의 현재 단계 current/required/done을 사용하며 DB counters를 재계산하지 않는다. 목표 이름은 definition.steps[].objectives[].description을 우선 사용하고 선택 StatePage.objectives[].description은 보조로만 사용한다.
- 같은 requestId/snapshotId/저장 characterId/revision/serverNowMs/pageCount/trackedQuestIds의 모든 페이지가 모인 뒤에만 상태를 교체한다. 중복·과거 snapshot과 revision 역행을 거부하고 부분 수신은 기존 표시만 보존한다. Item/SkillPoints 및 정확히 한 choiceRewards 묶음을 표시한다. 미보유 보상·제출 아이템도 itemDefinitions가 필요하다. 퀘스트 itemDefinitions의 페이지·전체 개수 방어 상한은 8192이며, 캐릭터 보유 스택 상한 167과 별개다. 정의별 8 KiB·페이지 14 KiB·전체 512 KiB 바이트 상한은 유지한다. turnInItems는 모든 HoldItem의 합산 제출 목록이며 수령 시 차감한다. AcquireItem은 신규 획득 카운트이며 별도 제출하지 않는다.
- 저장 캐릭터 ID는 TownClient::GetSelectedCharacterId()를 사용한다. 외형용 localCharacterId와 구분한다. 캐릭터 전환 시 페이지·선택·추적 표시·요청 컨텍스트를 초기화하고 접속 attempt가 다른 TCP 이벤트는 기존 큐에서 차단한다. 수신 스레드는 payload만 enqueue하고 모든 모델·UI 적용은 게임 스레드에서 수행한다.
- Accept/Abandon/Report/Claim/Track/Talk은 서버 권한을 사용한다. 요청은 CSPRNG requestId 64 lowercase hex 및 expectedRevision decimal string이며, 한 요청이 대기하는 동안 새 변경 요청을 차단한다. 10초 무응답·Busy·DB 장애는 지급 실패로 확정하지 않는다. 사용자가 결과 확인을 누르면 원래 ID·revision·choice를 포함한 동일 JSON만 다시 보내며 새 Claim을 자동 생성하지 않는다. 확정 결과 후 완전한 상태를 다시 조회한다. Claim 확정 후 가방과 성장 상태도 기존 경로로 갱신한다.
- 퀘스트·인벤토리는 character_state의 revision을 공유한다. 완성 QuestStatePage/연관 ActionResult에서 관찰한 더 높은 revision마다 InventoryUi::ObserveRevision으로 가방 표시를 무효화하고 기존 경로로 조회한다. 같은 revision은 다시 무효화하지 않고 진행 중인 사용 요청 ID·원문은 보존한다. 반대로 인벤토리에서 높은 revision을 관찰하면 퀘스트도 최신 snapshot을 요구하고 성장 상태 갱신 시 퀘스트 재조회를 예약한다. 프레임별 조회 루프는 추가하지 않는다.
- ESC 메뉴의 퀘스트창은 1280 기준 880×500이며 HudLayout/UiScaleScope로 1920에서 1.5배 표시하고 입력에 같은 역변환을 사용한다. 오른쪽 추적은 캐릭터별 최대 3퀘스트의 현재 단계 요약이다. 하단 아이템 6개·6×2 스킬과 분리하며 메뉴·클리어·선택창에서는 추적을 숨긴다. 퀘스트창에서는 이동·공격을 차단하고 전투는 계속된다. 포기는 확인 창을 거친다.
- NPC 엔진·새 아트 없이 서버 NPC 위치에 안내소 이름과 !/? 마커를 그린다. 마을에서 거리 96 이내의 클릭/대화만 요청하며 수락·보고·수령의 NPC 조건도 확인한다. 실제 거리·자격·단계·이벤트 기간·제출/공간·중복 지급은 서버가 최종 판정한다. 지도·던전 전환 시 NPC 접점을 초기화한다.
- 이번 구현은 소스 정적 검토 범위다. 빌드·게임/서버 실행·기능 테스트·DB 접속/마이그레이션 적용·실행 폴더 배포·커밋/푸시는 수행하지 않았다.
