# 오디오 디렉터리

게임 전용 XAudio2 재생 경로를 사용합니다. 현재 음원은 등록되어 있지 않아 조용히 동작합니다. 새 음원은 이 작업에서 생성하지 않았습니다.

지원 형식은 RIFF WAV의16비트 PCM, 모노/스테레오,8~192kHz이며 파일당 최대64MiB입니다. 압축 WAV, WAVE_FORMAT_EXTENSIBLE, OGG/MP3는 지원하지 않습니다. 배경음악도 이 형식으로 준비합니다. Windows SDK의 xaudio2.lib만 추가하며 별도 패키지·코덱이 필요하지 않습니다.

음원은 기존 `Assets/Audio/Music/` 또는 `Assets/Audio/Sfx/` 경로에 추가하고 `Assets/Data/assets.ini`의 `[Audio]`에 `Assets/` 기준 상대 경로로 등록합니다. 다음 예시는 파일을 추가한 뒤 주석을 해제할 등록 값이며 현재 파일이 있다는 뜻은 아닙니다.

```ini
[Audio]
StageBgm=Audio/Music/stage_01.wav
DirectionalStrike=Audio/Sfx/directional_strike.wav
```

`StageBgm`은 캐릭터 입장 후 반복 재생하며 마을·던전에서 같은 곡을 사용합니다. 효과음은 `Assets/Data/effects.ini`의 기존 `[DirectionalStrike]`에서 `audio_asset=DirectionalStrike`로 연결합니다. 마을은 앞차기 활성화 성공 시, 던전은 로컬 플레이어의 서버 확정 새 앞차기 skillSequence 수신 시 한 번 재생합니다. 최초 상태·같은 순서 재수신·맵/epoch 변경에서는 재생하지 않습니다. 등록 변경은 클라이언트를 다음 실행할 때 읽습니다. 음원을 등록하지 않으면 해당 경로를 건너뛰며 파일/형식/장치 오류는 게임 옵션에 표시합니다.

ESC → 게임 옵션에서 마스터·효과음·배경음악을0~100%,5% 단위로 변경합니다. 게임 내부 mastering voice와 분류별 submix에 즉시 적용하며 최종 gain은 마스터×분류 값입니다. 시스템 전체나 다른 프로그램의 볼륨을 수정하지 않습니다. 세션 값은 로그아웃 후에도 유지하고 설정 파일은 새로 저장하지 않습니다.

재생 버퍼는 voice가 파괴될 때까지 유지합니다. 효과음은 최대16개 동시 재생하고 완료 voice는 게임 스레드에서 정리합니다. 로그아웃/캐릭터 전환/종료 때 재생 voice와 버퍼를 해제하며 Application 종료 때 submix·mastering voice·엔진을 해제합니다. 오디오 callback에서 게임/UI 상태를 접근하지 않습니다.

이번 구현은 SDK 선언·프로젝트 XML·자원 수명·입력 배치의 정적 대조만 진행했습니다. 실제 음원 재생, 볼륨별 출력, 음소거, 장치 부재, 반복 재생/종료와 전체화면 전환 중 동작은 실행 검증이 필요합니다.
