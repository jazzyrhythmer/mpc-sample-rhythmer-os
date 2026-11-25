# Break Manipulator와 Acid Generator

## Break Engine

## 먼저 한 가지씩 따라 하기

| 목표 | 화면과 조작 | 확인 / 돌아가기 |
|---|---|---|
| 루프 재생 | 빈 트랙 → PLUG-IN → Break Engine. 브라우저 상단 LOAD A / N BARS를 확인하고 K1 LOAD BARS로 실제 1–4마디 지정 → 파일 선택 → 인코더 Push | 비어 있던 패턴만 Straight로 준비된다. BREAK GEN에 머무르며 BRK PERF가 선택된다. Push / B3 OPEN → PLAY. |
| 패턴 생성 | Break 트랙 → SEQ → B1 PATTERNS → SHIFT+B3 GEN → BREAK GEN → STRAIGHT / GENRE CHOPS / RANDOM CHOPS / MUTATE CHOPS / TAIL FILL → Push / B3 APPLY | 현재 패턴을 바꾸는 동작이다. SHIFT+− Undo, SHIFT++ Redo. BREAK GEN의 B1 BACK / SHIFT+Push → GEN 메인 → 한 번 더 닫기. |
| 소스 A 교체 | BRK PERF의 1·2·4·5·6페이지 → B2 LOAD A. 또는 BREAK GEN → BROWSE SAMPLE | 기존 패턴·길이·연주 값·패드 음색 설정 유지. 새 파일의 16등분으로 트림을 다시 잡는다. 같은 마디 수면 B 유지. |
| 소스 B 교체 | BRK PERF의 3 SOURCE A/B → B2 LOAD B → 파일 → Push | A와 같은 선언 마디 수를 사용. 패턴을 만들지 않는다. 로드 후 SOURCE A/B로 복귀. PLAY 중 패드 1 A / 2 B / 3 RETURN. |
| 롤 연주 | BRK PERF → 인코더 회전으로 2 ROLLS → PLAY → 롤 패드 누르고 놓기 | 누른 동안 연주한다. Record+Play일 때만 시퀀스에 기록한다. B1 GEN은 BREAK GEN으로. RETURN은 Variation/Drift 복귀이며 뒤로가기가 아니다. |
| 저장·다시 열기 | SHIFT+패드16 PROJECT → SAVE. 이후 OPEN으로 프로젝트 재열기 | 소스 참조·패턴·연주 설정을 프로젝트에 저장한다. 원본 샘플 파일을 이동/삭제하지 않는다. User Sound/User Kit 저장과 다르다. |

### 교체와 길이 변경

A의 LOAD BARS를 바꾸면 **Replace A / N bars?** 확인창이 나온다. B1 CANCEL 또는 SHIFT+Push는 취소한다. B3 LOAD A로 진행하면 패턴과 패턴 길이는 유지하고 B는 비운다. 새 길이의 Straight가 필요하면 BREAK GEN의 K1 LENGTH로 시퀀스 길이를 정한 뒤 STRAIGHT를 명시적으로 생성한다. 브라우저의 LOAD BARS는 소스 길이 선언으로 별개이며 GEN 설정을 바꾸지 않는다. Undo는 A/B·마디 수·패턴·연주 설정을 한 번에 복원한다. 초기 로드도 기존 노트/모션이 있다면 자동 Straight로 덮어쓰지 않는다.

### 파일 선택과 취소

Break 브라우저는 기존 폴더·CARD/INTERNAL·B3 PREVIEW를 사용하며 오디오 루프와 폴더만 표시한다. WAV, MP3, FLAC, Ogg, AIFF를 지원한다. K2 AUDITION으로 ORIGINAL / SYNC를 고르며 기본은 ORIGINAL이다. SYNC는 30초 미만·¼–4배 속도 범위에서만 제공한다. CALC BPM은 파일 길이와 선언 마디 수로 계산한 값으로 자동 비트 검출이 아니다. K1의 마디 수는 사용자가 선언한 길이이며 자동 검출값이 아니다. 소스 BPM은 파일 길이와 선언 마디 수로 계산한다. 샘플 파일은 수정하지 않는다.

B1 BACK은 폴더 안에서는 상위 폴더로 간다. 루트에서 A는 GEN, B는 PERF, 플러그인 선택에서 들어왔다면 CANCEL로 목록에 돌아간다. SHIFT+인코더 **Push**는 브라우저를 닫는다. 인코더 회전과 혼동하지 않는다. 로딩 화면의 B1 CANCEL / SHIFT+Push는 배정을 취소하며, 다른 트랙으로 이동한 뒤 로드가 끝나도 화면을 빼앗지 않는다. K2 AUDITION의 ORIGINAL은 원본 속도 미리듣기다. SYNC는 기존 샘플러 Beats 워프로 현재 프로젝트 템포에 맞춘 별도 모니터링이다. 읽기가 끝난 뒤 재생하며 마디 시작에 맞춰 퀀타이즈해 시작하지는 않는다. 일반 Sample Kit 브라우저와 그 로드 경로는 그대로다.


플러그인 목록에서 **Break Engine**을 선택하고 루프를 불러온다. 루프 브라우저의 K1 LOAD BARS에서 실제 길이를 1–4마디로 지정한 뒤 파일을 선택하고 Push한다. 재로드는 SEQ → B1 PATTERNS → SHIFT+B3 GEN → BREAK GEN → BROWSE SAMPLE로 진입한다. 기존처럼 16개 슬라이스와 템포에 맞는 패턴을 준비한다. 원본 녹음 파일은 제공하지 않는다. Break DSP는 원본처럼 모노이며 스테레오 루프는 합쳐서 처리한다.

이제 EDIT 또는 BREAK 메뉴의 **BRK PERF**에서 전용 연주 화면을 사용한다. 인코더를 돌리거나 누르면 다음 페이지로 이동하며, 위에 현재 페이지와 전체 페이지 수가 표시된다.

| 페이지 | 패드와 노브 |
|---|---|
| 1 CHOPS | 패드 1–16으로 슬라이스 연주. Orbit, DJ Filter, Pattern Hold |
| 2 ROLLS | 패드를 누르는 동안 Repeat·Up·Down·Shrink·Build·Stretch·Echo 연주 |
| 3 SOURCE A / B | 패드 1은 A, 패드 2는 B, 패드 3은 Return. Play Mode, Pattern Hold, DJ Filter |
| 4 LOOP VARIATIONS | Variation, Drift, Tail Fill. Variation·Drift는 LOOP 모드 + Pattern Hold OFF에서 내부 마디 경계부터 적용 |
| 5 GRAIN / STRETCH | 그레인 길이, 이음새 크로스페이드, 2–4배 Stretch |
| 6 TRACK MIX | 트랙 음량, 팬, Send A |

ROLLS의 패드 배치는 아래쪽부터 다음과 같다.

| 패드 | 기능 |
|---|---|
| 1–4 | 1/4 Repeat, Build, Stretch, Echo |
| 5–8 | 1/8 Repeat, Down, Up, Shrink |
| 9–12 | 1/16 Repeat, Down, Up, Shrink |
| 13–16 | 1/32 Repeat, Down, Up, Shrink |

SOURCE 페이지의 **LOAD B**로 두 번째 루프를 선택한다. A와 B는 같은 마디 수로 해석하며, 파일의 실제 프레임 길이가 달라도 각각 같은 구절에 맞춰 재생한다. 원본 FM-1의 고정 길이 A/B 뱅크를 사용자가 직접 불러오는 루프로 대체한 구조다.

**CHOP**은 시퀀스에 지정한 슬라이스를 재생한다. **LOOP**는 시퀀스의 게이트가 열려 있는 동안 원본의 내부 브레이크 패턴·Variation·Drift를 사용한다. 시퀀스의 슬라이스를 들으려면 CHOP을 선택한다. 소스 교체와 GEN은 선택한 연주 모드를 유지한다.

**Record + Play** 상태에서 ROLLS와 SOURCE 패드를 누르면 연주 명령과 누른 길이를 현재 시퀀스에 기록한다. A/B와 Return도 기록하며, 재생할 때 같은 명령을 실행한다. 연주한 명령을 SEQ의 선택 레인으로 표시하며, STEP EDIT와 ROLL에도 음 이름 대신 명령 이름이 나온다. 스텝을 누른 채 인코더를 돌리면 해당 스텝의 Chop·Roll·A/B·Return 명령을 바꾼다. Return은 Variation·Drift를 원래 상태로 돌린다. 기존 시퀀스를 새 패턴으로 덮어쓰지 않는다.

아래 **GEN**은 BREAK 생성 메뉴로 돌아간다. 그 메뉴의 BACK은 GEN 메인 메뉴로 이동한다. 정지 버튼을 두 번 누르는 전체 오디오 중단 동작도 유지한다.

## Break 조작 안내

### 다시 진입

Break 트랙 선택 → **SEQ → B1 PATTERNS → SHIFT + B3 GEN → BREAK GEN → BRK PERF**. BRK PERF의 **B1 GEN**은 BREAK GEN으로 돌아간다. BREAK GEN의 B1 BACK 또는 SHIFT + 엔코더는 GEN 메인으로, 한 번 더 실행하면 창을 닫는다. 별도의 “Breaker Setup” 메뉴는 없다.

### 작동 조건을 듣고 확인

- 기본 CHOP은 시퀀스의 슬라이스를 재생한다. PATTERN HOLD는 오디오 유지가 아닌 **내부 패턴 재생성 중지**이므로 CHOP에서 효과가 들리지 않는다. 트랙 Freeze와도 다르다.
- PLAY → 3 SOURCE A/B에서 K1 LOOP, K2 Hold OFF → 4 LOOP VARIATIONS에서 Variation/Drift를 올린다. 내부 마디가 바뀌는 소리를 듣고 3페이지 Hold ON으로 패턴을 유지한다. Variation/Drift가 0이면 ON/OFF 차이가 적을 수 있다. Hold가 라이브 롤과 모든 조작을 멈추는 것은 아니다.
- GRAIN/STRETCH는 스트레치된 히트에 적용된다. PLAY 중 2 ROLLS에서 **패드 3 STRETCH를 계속 누른 채** 인코더를 돌려 5페이지로 이동하고 Grain/Splice/Slow를 조절한다. 패드를 놓으면 해제된다. STRETCH를 시퀀스에 녹음한 뒤 조절해도 된다. PATTERN HOLD는 필요하지 않다. Slow는 해당 히트의 2–4배 스트레치이며 전체 템포가 아니다.
- B는 3페이지 LOAD B로 별도 파일을 넣어야 한다. 6 TRACK MIX는 트랙 음량·팬·Send A이며 패턴 생성 페이지가 아니다.
- RETURN은 Variation/Drift를 리셋한다. 패턴 삭제나 Hold 해제가 아니다. 누른 연주 패드는 놓아서 해제한다.

## Acid Generator

Acid Engine 트랙에서 SEQ → B1 PATTERNS → Shift+B3 GEN → **ACID GEN**을 선택한다. B2 SETTINGS로 두 설정 페이지를 번갈아 보며 제목의 1 OF 2 / 2 OF 2에서 현재 위치를 확인한다. 인코더로 GENERATE ACID 또는 MUTATE ACID를 선택하고 Push / B3 APPLY로 실행한다. 재진입은 첫 설정 페이지에서 시작한다.

| 설정 페이지 | 노브 |
|---|---|
| 1 / 2 | Density 0–100%, Accent 0–100%, Slide 0–100% |
| 2 / 2 | Octaves 1–3, Base Octave 0–4, Auto Mutate OFF 또는 1–16마디 |

기본값은 FM-1과 같은 Density 70, Accent 40, Slide 25, Octaves 2, Base Octave 1이다. 조성과 스케일은 프로젝트의 현재 설정을 따른다. 생성과 변형에 같은 원본 TB-3PO 함수를 사용한다.

**GENERATE ACID**는 원본 NEW처럼 새 Seed로 새 라인을 만든다. **MUTATE ACID**는 현재 라인을 변형하며 해당 트랙의 32비트 난수 흐름을 이어간다. 시드는 내부에서만 관리하며 노브·숫자 표시·Replay 메뉴는 제공하지 않는다. 생성·변형 과정의 전체 32비트 상태와 Undo/Redo·프로젝트 저장·복원은 그대로 유지한다. 다른 트랙이나 4트랙 GEN의 Seed는 공유하지 않는다.

Tie는 다음 스텝에서 다시 발음하는 노트로 바꾸지 않고, 기존 노트가 이어지는 길이로 변환한다. 패턴 경계를 넘는 Tie도 보존한다. 현재 SEQ 그리드의 1–64스텝을 사용한다. Accent는 원본의 127/96 벨로시티를 따르고 Slide는 시퀀스의 Slide 명령에 연결한다. Undo/Redo는 패턴과 그 트랙의 Seed를 함께 복구한다.

Auto Mutate는 재생 중 지정한 마디 간격마다 변형한다. 녹음·송 재생 중에는 자동 생성하지 않는다. BACK은 GEN 메인 메뉴로 돌아간다.

이 문서는 베타 기능의 조작 안내다. 실제 MPC Sample의 패드 응답, 장시간 재생, 소리 및 녹음 감각은 실기기에서 검증한다.


## 효과가 들리지 않을 때: 상태부터 확인

| 화면 / 상태 | 의미 | 조작 |
|---|---|---|
| LOOP VARIATIONS: INACTIVE | CHOP 재생 중. 내부 LOOP 패턴 변형은 들리지 않음 | SOURCE의 PLAY MODE를 LOOP로 직접 선택 |
| PATTERN HOLD ON | 내부 패턴 재생성 보류 | Hold OFF. 오디오 유지 / 트랙 Freeze / Grain 켜기가 아님 |
| READY | 정지·게이트 없음 또는 Variation/Drift가 모두 0 | 패턴 PLAY / chop 패드 게이트, Variation·Drift 한 값씩 올리기 |
| ACTIVE | LOOP·Hold OFF·재생 조건 충족 | 다음 내부 마디에서 비교 |
| GRAIN: WAITING | STRETCH 명령이 없음 | 2 ROLLS의 패드3 STRETCH를 누른 채 5 GRAIN으로 이동 |
| STRETCH ACTIVE | 실시간 또는 시퀀스에 기록한 STRETCH가 실제 엔진에 전달됨 | Grain/Splice/Slow 조정. 패드를 놓으면 종료. Pattern Hold 불필요 |

BRK PERF 내 페이지 이동은 누른 패드를 유지한다. GEN·뒤로·다른 트랙으로 나가면 해당 트랙의 실시간 연주 명령만 해제하며, 다른 트랙의 눌린 음은 끊지 않는다. 시퀀스에 기록된 명령은 패턴대로 재생한다. RETURN은 Variation/Drift 복귀이며 Hold OFF나 패턴 삭제가 아니다.

### 새 미리듣기의 범위

B3 PREVIEW와 K2 AUDITION은 트랙·소스·패턴·Send·Insert를 바꾸지 않는다. 모니터링은 프로젝트 녹음·Freeze·스템에 들어가지 않는다. SYNC는 파일 전체 읽기가 끝난 뒤 기존 Beats 워프를 사용하며 피치를 유지한다. 실제 호스트 템포를 따르지만 MIDI 싱크 계산은 바꾸지 않는다. 다른 파일로 이동·PREVIEW OFF·브라우저 닫기·트랙 이동은 미리듣기를 중단한다. Push는 별도의 소스 로드 동작이다.

READING SYNC는 읽는 중이다. 미리듣기는 최대 30초이므로 30초에 도달한 미리듣기는 전체 파일의 BPM으로 추정하지 않으며 ORIGINAL만 사용한다. 파일 이름의 숫자를 자동 BPM으로 단정하거나 LOAD BARS를 자동 변경하지 않는다. 소스 길이를 절반/두 배로 잘못 선언하면 계산 BPM도 절반/두 배가 된다.

### 박자가 밀리거나 끝에 빈틈이 있을 때

STRAIGHT부터 확인한다. 파일의 앞 무음·밀린 첫 박·뒤 패딩·잘린 시작은 16등분에 그대로 포함된다. 자동 트랜지언트 검출이나 첫 박 보정 기능으로 설명하지 않는다. 3마디도 16등분이므로 모든 어택에 슬라이스가 맞지는 않는다.

필요하면 여유 Sample Kit 트랙의 패드에 원본 전체를 로드 → 기본 TRIM에서 Start/End 조정 → SHIFT+패드13 Trim Sample로 Samples/Edits에 파생 파일 생성 → 파일 확인 → Break에서 다시 로드한다. 원본 파일은 덮어쓰지 않는다. Break의 한 슬라이스 Trim을 전체 루프 편집으로 혼동하지 않는다.

같은 Seed·설정은 같은 결과를 만든다. Mutate는 바꿀 노트가 있어야 하며 ENERGY 0이면 변화가 없을 수 있다. 생성 패턴은 SHIFT+−로 Undo, SHIFT++로 Redo한다. PROJECT SAVE/OPEN은 소스 참조·패턴·연주 설정을 저장/복원하며 User Sound/User Kit과 다르다. A 마디 수 변경으로 B를 비웠다면 기록된 B 전환 명령을 쓰기 전에 B를 다시 로드한다.

브레이크 연주용 소스는 60초 이하의 오디오 루프를 사용한다. 스트리밍 파일은 오디오 스레드 밖에서 모노 연주 버퍼를 준비한다. 일반 샘플/Audio 트랙의 긴 파일 스트리밍 제한은 변경하지 않는다.
