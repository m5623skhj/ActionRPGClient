# TownServer 클라이언트 패킷 개발 가이드

이 문서는 ActionRPGClient에서 TownServer 콘텐츠 패킷을 보내고 결과를 게임 스레드에
전달하는 방법을 설명한다.

## 0. Google 인증과 타운 TLS 입장

클라이언트는 로그인 화면으로 시작한다. 시스템 브라우저의 Google Desktop OAuth
Authorization Code/PKCE(S256)·loopback 콜백으로 ID 토큰을 얻으며, 매 시도 새 state를 검증한다.
Auth가 발급한 nonce를 Google 인증 요청에 그대로 전달하고 실제 ID 토큰 검증은 Auth가 수행한다.
Google access token을 게임 인증에 사용하지 않는다. 로그인 입력과 캐릭터 이름/종류 선택은 별개다.

순서는 /v1/challenges → Google ID 토큰 → /v1/login → /v1/tickets →
타운 TLS → AdmissionTicketRequest=36 → 성공 AdmissionResult=37(result=0) →
EnterTownRequest=1 → EnterTownResponse=2다. 응답 2를 받기 전에는 Game을 생성하지 않는다.
패킷 36의 body는 ID 2바이트 + 문자열 길이 2바이트 + 티켓 64바이트이며,
패킷 37의 body는 ID 2바이트 + 결과 1바이트다. 기존 1~35 정의는 유지한다.
기준 계약은 서버 커밋 665e459955a80a9fa923cccf5d0fab9fa1eba483이며,
검토한 서버 e4f0569a6f30b7e914eee77abef5250b0b1cea23에서도 이 wire 계약은 같다.

### 연결 설정

실제 클라이언트 Assets/Data/AuthClient.json을 사용한다. 빌드의 기존 CopyRuntimeAssets가
실행 파일 옆 Assets에 배포한다. 설정은 기동 시 읽으며 바꾼 뒤 재기동해야 한다.
기본 파일은 미설정 상태다. 임의 서버를 목록에 채우거나 인증을 우회하지 않는다.

| 필드 | 실제 설정 |
|---|---|
| authUrl | HTTPS origin, 경로/쿼리/사용자 정보 없음. 실제 Auth 인증서의 호스트명과 포트 |
| googleClientId | Google **Desktop app** client ID. Auth의 ACTIONRPG_GOOGLE_CLIENT_ID와 동일 |
| townCaFile | 타운 인증서의 신뢰 CA PEM 파일. 절대 경로 또는 Assets 기준 상대 경로 |
| playerName | 기본 게임 이름, 빈 값은 기존 Player-PID. 입장 이름은 UTF-8 1~32바이트 |
| characterId | 기존 캐릭터 1~3 중 기본 선택 |
| servers | 실제 타운 객체 배열. 각 객체는 serverId, name, hostname, port |
| servers[].serverId | Auth 등록 ID 및 해당 타운 ACTIONRPG_TOWN_ID와 일치 |
| servers[].hostname | DNS 호스트명 또는 IP. 타운 인증서 SAN과 일치 |
| servers[].port | 실제 TLS 리스너 포트, 1~65535 |

Auth·Google HTTPS는 WinHTTP의 **Windows 인증서 저장소**로 체인과 호스트명을 검증한다.
사설 Auth CA를 쓰면 운영 환경에서 Windows 신뢰 배포가 별도로 필요하다.
타운 TLS는 OpenSSL/Asio가 townCaFile의 CA를 사용해 체인·호스트명을 검증하며 TLS 1.2 이상만 허용한다.
CA 파일만 설정해도 Windows HTTPS 신뢰가 바뀌지는 않는다. 인증서 검사 무시 옵션은 없다.
서버 개인 키, 타운 비밀 키, DB 연결 문자열은 이 설정이나 클라이언트에 넣지 않는다.
OpenSSL 의존성은 vcpkg manifest와 Debug/Release 링크에 선언했다. 2026-10-06에
OpenSSL 3.6.1#3 설치와 클라이언트 Debug x64 컴파일·링크 성공을 확인했다.

### 상태·취소·세션 정리

인증 작업은 전용 worker에서 수행하고 mutex 큐로 결과를 UI 스레드에 전달한다.
HTTPS는 요청별 15초 한도, Google 브라우저 대기는 challenge 300초 한도,
타운 TLS/승인/EnterTown 전체는 티켓 잔여 시간과 15초 중 작은 한도를 사용한다.
취소는 기다리는 UI를 즉시 빠져나온다. 완료 여부가 불확실해도 HTTP를 자동 재시도하지 않는다.
ready=false에는 티켓을 타운에 보내지 않고 기존 접속 종료 대기를 표시한다.
사용자가 다시 입장을 누르면 새 티켓을 발급한다. 평문 TCP나 기존 자동 재접속은 사용하지 않는다.

Google ID 토큰·게임 토큰·티켓은 메모리에만 두고 로그/URL/설정에 쓰지 않는다.
게임 토큰은 최대 8시간이며 자동 갱신·영구 저장·재기동 자동 로그인은 없다.
로그아웃·계정 전환 시 타운/던전 연결과 기존 Game을 정리하고 로컬 토큰을 제거한다.
서버 로그아웃 실패/불확실한 결과도 화면에 표시하며 로컬 로그아웃을 되돌리지 않는다.
계정 전환 후 다음 Google 로그인에는 계정 선택을 요청한다.
타운 변경은 게임 토큰만 유지하고 Game/연결을 정리한 뒤 목적 타운의 새 티켓을 발급한다.
타운 간 캐릭터·스킬·진행 상태 보존은 서버 계약에 포함되지 않는다.
503은 서버 준비 중으로, 403은 거절로 안내한다. TCP 종료만으로 중복 로그인/정지 등 원인을 단정하지 않는다.

Auth 결과, 타운 수신 이벤트와 송신 예약은 시도 ID를 확인한다.
이전 시도의 콜백/입력이 새 Game에 섞이지 않게 큐를 비우고 타운 stream·읽기·쓰기 버퍼를
연결별 shared_ptr 수명으로 보호한다. WinHTTP callback 상태는 HANDLE_CLOSING까지 보존한다.
UI는 RequestStop으로 연결 종료를 요청하며 던전 종료 future를 기다리지 않고 매 tick 확인한다.
프로세스 종료에서는 worker를 정리하고 join한다.

### 검증 상태

서버/클라이언트 패킷 정의, API 경로·입력·응답·수명, UI 전환과 취소 후 수명을 정적으로 검토했다.
2026-10-06에 NASM 3.01 실행 및 CMake 검색을 확인하고 OpenSSL을 vcpkg 바이너리 캐시에서 설치했다.
Debug x64 빌드가 성공했으며, 임시 NASM PATH와 manifest 강제 옵션 없이 일반 빌드도 통과했다.
첫 컴파일에서는 기존 RUDP 공용 헤더 NetServerSerializeBuffer.h의 C4828 인코딩 경고가 남았다.
최초 NASM 검색 실패 원인은 재현하지 못했으며 전역 PATH와 vcpkg 도구 스크립트는 변경하지 않았다.
Release 빌드·기능 테스트·클라이언트/서버 실행·Google/Auth/Town 실제 왕복은 수행하지 않았다.
실제 HTTPS 주소, 타운 ID/호스트/포트, CA 배포 및 Google Desktop 등록/동의 화면/테스트 계정은
운영 설정이 필요하다. 서버 문서상 실제 DB 마이그레이션 적용과 인증 왕복도 아직 미검증 상태다.
실행 검증에서는 로그인/거절/503/ready=false, 각 단계 취소, 역순 완료, TLS 거절,
로그아웃 실패, 마을·던전에서 계정 전환/타운 변경 및 대기 중 창 닫기를 확인해야 한다.

근거: [Google Desktop OAuth](https://developers.google.com/identity/protocols/oauth2/native-app),
[Google OIDC nonce](https://developers.google.com/identity/openid-connect/openid-connect),
[WinHTTP 종료와 callback 수명](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpclosehandle).

## 1. 현재 데이터 흐름

```text
게임 스레드
  TownClient::Send...()
    ↓ asio::post(strand)
네트워크 스레드
  Protocol::Encode → QueuePacket → async_write

네트워크 스레드
  TLS async_read → TownClient::Impl::Session::HandlePacket → Protocol::Decode
    ↓ PushEvent (mutex 보호)
게임 스레드
  ConsumeEvents → GameWorld::ProcessNetworkEvents
```

네트워크 콜백에서 `Player`, `GameWorld`, UI를 직접 수정하지 않는다. 디코딩한 결과는
`TownEvent`에 넣고 게임 스레드에서 처리한다.

## 2. 패킷 wire format

```text
[uint32 bodySize, big-endian]
[uint16 packetType, big-endian][payload]
```

- body 최대 크기: 1MiB
- 클라이언트 송신 대기열 최대 크기: 1MiB
- 문자열: `[uint16 UTF-8 byteLength][bytes]`
- 구조체 메모리를 그대로 보내지 않고 필드를 하나씩 기록한다.
- 서버와 클라이언트의 enum 값, 자료형, 필드 순서가 정확히 같아야 한다.

현재는 프로토콜 버전 협상 없이 서버와 클라이언트를 함께 배포하는 방식이다. 양쪽 코드가
다르면 malformed packet으로 연결이 종료될 수 있다.

## 3. C2S 패킷 추가

거래 요청 `TradeRequest`를 예로 든다.

### 3.1 TownProtocol 정의

`Network/TownProtocol.h`의 `PacketType` 마지막에 새 ID를 추가하고 요청 구조체를 선언한다.
기존 ID는 변경하거나 재사용하지 않는다.

```cpp
enum class PacketType : std::uint16_t
{
    // 기존 값 유지
    TradeRequest = 7,
    TradeResult = 8
};

struct TradeRequest
{
    std::uint32_t requestId{};
    std::uint64_t targetPlayerId{};
};

std::vector<std::uint8_t> Encode(const TradeRequest& inPacket);
```

`Network/TownProtocol.cpp`에서 서버 디코더와 같은 필드 순서로 기록한다.

```cpp
std::vector<std::uint8_t> Encode(const TradeRequest& inPacket)
{
    PacketWriter writer(PacketType::TradeRequest);
    writer.WriteUInt32(inPacket.requestId);
    writer.WriteUInt64(inPacket.targetPlayerId);
    return writer.Finish();
}
```

현재 클라이언트 `PacketWriter`에 필요한 정수 함수가 없다면 서버 구현과 동일한 big-endian
함수를 먼저 추가한다.

### 3.2 TownClient 송신 API

`TownClient.h`에 공개 함수를 추가한다.

```cpp
void SendTradeRequest(const TownProtocol::TradeRequest& inRequest);
```

구현은 `SendMovement()`와 같은 패턴을 사용한다.

```cpp
void TownClient::SendTradeRequest(const TownProtocol::TradeRequest& inRequest)
{
    asio::post(impl->strand, [this, attempt = impl->attempt.load(),
        packet = TownProtocol::Encode(inRequest)]() mutable
    {
        if (impl->connected.load() && attempt == impl->attempt.load())
        {
            QueuePacket(std::move(packet));
        }
    });
}
```

UI나 게임 로직은 `TownClient`의 소켓, strand, send queue에 직접 접근하지 않는다.

## 4. S2C 패킷 추가

### 4.1 결과 구조체와 디코더

`TownProtocol.h`에 서버 구조체와 동일한 결과 구조체 및 디코더를 선언한다.

```cpp
struct TradeResult
{
    std::uint32_t requestId{};
    std::uint8_t resultCode{};
};

std::optional<TradeResult> DecodeTradeResult(
    const std::vector<std::uint8_t>& inPacket);
```

디코더는 다음을 모두 확인한다.

- 예상 `PacketType`
- 필요한 모든 필드가 존재하는지
- bool과 enum 값이 허용 범위인지
- `reader.Finished()`가 참인지

### 4.2 TownClient 수신 등록

`TownClient::HandlePacket()`에 case를 추가한다.

```cpp
case TownProtocol::PacketType::TradeResult:
    if (auto packet = TownProtocol::DecodeTradeResult(receiveBody))
    {
        PushEvent(std::move(*packet));
    }
    else
    {
        HandleDisconnect();
    }
    break;
```

형식이 잘못된 패킷은 연결을 종료한다. 서버가 정상적으로 거절한 콘텐츠 요청은
`resultCode`가 실패인 정상 패킷으로 처리한다.

### 4.3 TownEvent 등록

`TownClient.h`의 variant에 결과 타입을 추가한다.

```cpp
using TownEvent = std::variant<
    TownProtocol::EnterTownResponse,
    TownProtocol::PlayerAppear,
    TownProtocol::PlayerMove,
    TownProtocol::PlayerDisappear,
    TownProtocol::TradeResult>;
```

### 4.4 게임 스레드에서 처리

단순 월드 이벤트라면 `GameWorld::ProcessNetworkEvents()`의 `std::visit`에 분기를 추가한다.
거래창·파티창처럼 UI 수명이 별도라면 `GameWorld`에 모든 콘텐츠를 넣지 말고, 소비한 이벤트를
전용 controller/model로 전달한다.

```cpp
else if constexpr (std::is_same_v<EventType, TownProtocol::TradeResult>)
{
    tradeController.HandleResult(inEvent);
}
```

## 5. 요청과 응답 상태 관리

- 응답이 필요한 요청에는 증가하는 `requestId`를 포함한다.
- UI는 `requestId`별 pending 상태를 관리하고 결과를 받으면 제거한다.
- 버튼 연타로 같은 명령이 중복 전송되지 않도록 pending 상태에서 입력을 제한한다.
- 재화·아이템 변경은 클라이언트가 미리 확정하지 않고 서버 성공 결과 이후 반영한다.
- 위치처럼 예측이 필요한 데이터만 명시적으로 예측하고 서버 결과로 보정한다.
- 연결이 끊어지면 pending 요청을 실패 처리하거나 재접속 후 서버 상태를 다시 조회한다.
- 재접속 시 이전 세션의 `requestId` 결과가 새 UI 상태에 적용되지 않도록 세션 단위를 구분한다.

## 6. 스레드 안전성

- `TownClient`의 socket과 send queue는 network strand에서만 접근한다.
- `connected`는 `std::atomic_bool`이다.
- 수신 이벤트 목록은 `eventMutex`로 보호한다.
- `ConsumeEvents()`로 가져온 뒤에는 게임 스레드가 이벤트의 소유자다.
- 대용량 이미지 로드, 파일 I/O, UI 처리 등을 네트워크 strand에서 실행하지 않는다.
- `TownClient`를 캡처하는 비동기 작업은 `Stop()`과 객체 수명을 고려한다.

## 7. 서버와 함께 수정할 파일

새 패킷 하나를 추가할 때 최소 확인 목록:

| 위치 | C2S 요청 | S2C 응답 |
| --- | --- | --- |
| 서버 `Protocol.h/.cpp` | Decode | Encode |
| 서버 `PlayerSession.cpp` | switch case | 해당 없음 |
| 서버 `TownInstance`/콘텐츠 클래스 | 검증·처리 | 결과 Send |
| 클라이언트 `TownProtocol.h/.cpp` | Encode | Decode |
| 클라이언트 `TownClient.h/.cpp` | Send API | HandlePacket + PushEvent |
| 클라이언트 `TownEvent` | 해당 없음 | variant 타입 추가 |
| 게임/UI controller | 요청 호출 | 이벤트 소비 |

서버의 전체 콘텐츠·패킷 설계 원칙은 TownServer 저장소의
`ActionRPGServer/TownServer/DEVELOPMENT.md`를 참고한다.

## 8. 검증 체크리스트

1. 서버와 클라이언트의 패킷 ID와 필드 순서를 비교한다.
2. encode/decode round trip을 검사한다.
3. 빈 값, 최대값, 초과값, 잘린 body를 검사한다.
4. 입장 전 요청과 존재하지 않는 대상 요청을 검사한다.
5. 정상 실패가 연결 종료가 아닌 결과 코드로 전달되는지 확인한다.
6. 요청 직후 연결 종료와 자동 재접속을 확인한다.
7. 클라이언트 두 개 이상으로 실제 콘텐츠 흐름을 검사한다.
8. Debug/Release x64를 모두 빌드한다.

## 9. 관련 파일

- `Network/TownProtocol.h/.cpp`: 클라이언트 패킷 정의와 직렬화
- `Network/TownClient.h/.cpp`: 비동기 연결, 송수신, 이벤트 큐
- `Game/GameWorld.cpp`: 현재 월드 이벤트 소비 위치
- `App/Application.cpp`: TownClient 생성 및 접속 시작
