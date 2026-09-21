# TownServer 클라이언트 패킷 개발 가이드

이 문서는 ActionRPGClient에서 TownServer 콘텐츠 패킷을 보내고 결과를 게임 스레드에
전달하는 방법을 설명한다.

## 1. 현재 데이터 흐름

```text
게임 스레드
  TownClient::Send...()
    ↓ asio::post(strand)
네트워크 스레드
  Protocol::Encode → QueuePacket → async_write

네트워크 스레드
  async_read → TownClient::HandlePacket → Protocol::Decode
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
    asio::post(strand, [this, packet = TownProtocol::Encode(inRequest)]() mutable
    {
        if (connected.load())
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
