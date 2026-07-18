# 02 · 게임플레이 스펙

## 장르 & 핵심

탑다운 / 사이드 가능 — **탑다운 2D 액션 로그라이크** 채택.
한 손에 들어오는 조작, 한 판 안에 완결되는 상승 여정, 죽으면 메타 진행만 남는 구조.

## 코어 루프

```
방 진입 → (어둠) 빛 반경으로 탐색 → 적 등장/처치 → 보상(무기/유물/추억 조각)
   → 출구 선택(분기) → 다음 방 … → 바이옴 보스 → 다음 바이옴 → 읽기 헤드(엔딩)
```

- 한 방 클리어 30초~90초. 한 바이옴 8~10방 + 보스. 4바이옴.
- **1회 완주 런 ≈ 25~30분** (4바이옴 × 6~7분). 사망/재도전 포함 시 첫 경험 30분 쉽게 초과.

## 조작 (누구나 — 1분 안에 이해)

| 입력 | 동작 |
|---|---|
| WASD / 방향키 | 이동 |
| 마우스 / 스페이스 | 공격(주무기) |
| Shift | 대시(짧은 무적, 쿨다운) |
| E | 줍기/상호작용 |
| Q | 추억 조각 버리기(무게 관리) |
| Tab | 인벤토리(용량 게이지) |

게임패드도 지원(좌스틱 이동, 우버튼 공격, 트리거 대시). 튜토리얼은 텍스트 한 줄 + 첫 방 안내.

## 플레이어 스탯

- **무결성(HP):** 피격 시 감소. 0이면 데이터 손상 = 사망.
- **빛 반경:** 시야/안전 영역. 추억 조각 보유량에 비례해 밝아짐.
- **무게(용량):** 0 ~ **1.44MB**. 무기·유물·추억 조각이 KB 차지.

## ⭐ 용량/무게 시스템 (테마의 핵심 메커닉)

가방 총용량 = **1,474,560 bytes (1.44MB)**, UI엔 `1.44MB`로 표기.

| 보유량 | 효과 |
|---|---|
| 가벼움(<40%) | 이동·대시 빠름, 빛 약함 |
| 적정(40~80%) | 균형 |
| 무거움(80~100%) | 빛 밝고 강하지만 이동·대시 둔화, 회피 어려움 |
| 초과 시도 | 줍기 불가 — 무엇을 버릴지 선택 강제 |

→ **무엇을 끝까지 가져갈 것인가**가 전투 전략이자 서사. 추억 조각은 전투엔 직접 도움 안 되지만 **무게를 차지**하고 **엔딩을 좌우**한다. "살아남기 위해 추억을 버릴 것인가"가 주제.

## 무기 (주무기, 택1 장착 + 교체)

각 무기는 KB 비용 보유. 절차적 변형(접두사)으로 다양성 확보.

| 무기 | 특성 | KB(예시) |
|---|---|---|
| 포인터 블레이드 | 근접 광선검, 빠른 연타 | 64 |
| 비트 캐논 | 차징 직선 관통 빔 | 96 |
| 패킷 스프레이 | 단거리 산탄 | 80 |
| 루프 글레이브 | 부메랑형 회전 칼날 | 88 |
| 널 랜스 | 느리지만 강한 관통 창 | 72 |
| 에코 완드 | 유도 탄, 약함 다발 | 84 |

접두사(절차 생성): `과열된`(화상 DoT), `차가운`(둔화), `깨진`(치명타↑ 명중↓), `압축된`(KB절감) 등 → 조합으로 빌드 다양성.

## 유물 (패시브, 누적)

런 중 획득해 쌓이는 패시브. 각 KB 차지 → 무게 압박과 빌드 선택.

- **체크섬:** 일정 주기로 무결성 1 회복.
- **디프래그 코어:** 빈 용량이 많을수록 이동속도 보너스.
- **오버클럭:** 공격속도↑, 무결성 최대치↓.
- **백업 비트:** 사망 시 1회 부활(소모).
- **루미넌스:** 빛 반경↑, 어둠 속 적 가시화.
- **압축 알고리즘:** 모든 아이템 KB −20%.
- **배드섹터 부적:** 저무결성일 때 피해량↑.

## 적 (바이옴별)

스켈레탈 리그 재사용 + 색/스케일/행동 변형.

| 바이옴 | 잡몹 | 행동 |
|---|---|---|
| ① 배드 섹터 | 부패 슬라임, 글리치 박쥐 | 느림/돌진, 어둠에 숨음 |
| ② 잃어버린 트랙 | 메모리 망령, 잔상 추격자 | 플레이어 과거 위치 추적 |
| ③ 단편화 지대 | 조각 골렘, 포인터 터렛 | 분열, 원거리 사격 |
| ④ 부트 레코드 | 커널 파수꾼, 삭제 드론 | 패턴 강제, 영역 부정 |

어둠 활용: 빛 밖 적은 **윤곽(실루엣)만** → 루미넌스/조명 아이템의 가치 부여.

## 보스 (바이옴 끝, 4종)

| 보스 | 컨셉 | 패턴 핵심 |
|---|---|---|
| **부패충 Rot** | 데이터 갉는 벌레 군체 | 분열·포위, 어둠 확산 |
| **메아리 Echo** | 아이 옛 게임 보스의 잔상 | 플레이어 행동 1.5초 지연 복제 |
| **단편기 Defrag** | 공간 재배치 수문장 | 바닥 타일 셔플, 안전지대 이동 |
| **삭제 NULL** | 부패의 핵(최종) | 빛 흡수(어둠 페이즈) → 화면 정화 페이즈 |

## 추억 조각 (스토리·엔딩 트리거)

방·보스에서 드롭되는 수집형. 무게를 차지하지만 **줍는 순간 짧은 회상 컷**(빛 연출)이 재생되고, **끝까지 보존한 양·종류가 엔딩을 결정.**

- 종류: 일반 조각(다수) + **핵심 조각(스토리 키, 소수, 각 바이옴 보스 드롭).**
- 자세한 내용·장면은 [04-story.md](04-story.md) 참고.

## 난이도 & 메타 진행 (30분+ 보장 / 리플레이)

- **영구 해금:** 런 중 모은 "데이터 조각" 재화로 시작 무기/유물/캐릭터 변형 해금.
- **난이도 3단계** + **시드 입력**(데일리/공유) → 코어 유저 장기 플레이.
- **뉴게임+:** 트루 엔딩 후 해금. 부패 가속, 보상 강화.

### 30분 페이싱 표

| 구간 | 누적 시간 | 내용 |
|---|---|---|
| 튜토리얼 + 1방 | 0~2분 | 조작·빛·용량 개념 학습 |
| 바이옴 ① | 2~9분 | 8방 + 부패충 |
| 바이옴 ② | 9~16분 | 8방 + 메아리, 핵심 조각 회상 시작 |
| 바이옴 ③ | 16~23분 | 미로 + 단편기, 무게 압박 정점 |
| 바이옴 ④ | 23~29분 | 부트 레코드 + NULL |
| 엔딩/에필로그 | 29~31분 | 분기 연출 |

→ 무사 완주 시 ~31분. 대부분 첫 플레이는 도중 사망 → 재도전으로 **30분 보장 충족.**

## 세이브

- 메타 진행(해금·재화·최고 기록·시드)만 영구 저장.
- 런 중 세이브 없음(로그라이크 원칙). 일시정지/이어하기는 세션 한정.
- 저장 형식은 [05-tech-stack.md](05-tech-stack.md) 참고.
## 추억 선택의 bounded 규칙 (Mac-first)

추억 이벤트는 런에만 존재하며 `start_run()`에서 초기화된다. 선택은 `E` 보관 또는 `Q` 버리기 두 가지뿐이다. Echo/Corrupted 이벤트의 태그는 Courage/Kinship/Promise 세 가지이며, Corrupted의 정예 특성은 한 개만 대기시킨다. 대기 특성이 이미 있거나 보관으로 용량(원시 64KB, Compression 사용 시 유효 51KB)을 초과하면 `E`는 아무 상태도 바꾸지 않고 거부된다. `Q`는 이 경우에도 처리할 수 있다.

보관/버리기 누계는 각 태그별 8비트 카운터로 기록하고 포화되며, 전투 효과에 사용하는 보관 수만 2로 제한한다. 결정 로그는 최대 8개인 고정 링이며 각 바이트는 `(decision << 4) | (event_type << 2) | tag`로 인코딩된다. 이벤트는 한 번만 `RESOLVED`가 되며, 범위를 벗어난 입력·지원하지 않는 결정·반복 입력은 실패하고 카운터, 로그, HP, 바이트, 드롭을 변경하지 않는다. 추억 상태는 세이브하지 않는다.

엔딩의 주요 척추는 변경하지 않는다. 코어 0개는 빈손(BAD), 1~3개는 표준, 4개는 TRUE 엔딩이며 승리 수·`true_clear`·NG+ 해금 규칙도 그대로다. 주요 엔딩 뒤 코다는 보관 수가 가장 큰 태그를 사용하고 동률은 Courage → Kinship → Promise 순서다. 보관이 없고 버린 기록만 있으면 discard-only 코다, 기록이 전혀 없으면 코다가 없다.

## Mac 전용 진단 fixture

진단 바이너리는 `DD_DEBUG_BUILD=1`로 만든다. 아래 명령은 모두 저장소 안에서 실행하며, save fixture는 명령의 `HOME`과 `--isolated-profile`이 정확히 같고 `.dd-agent-owned-profile` 표식이 있는 `build/evidence/mac-first/save/<case>/home`만 허용한다.

```sh
DD_DEBUG_BUILD=1 sh tools/build_mac.sh
build/DisketteDungeon_diag_mac --seed 12345 --difficulty 1 --weapon 0 --ngplus 0 --action fixture-modifiers --clean-profile
build/DisketteDungeon_diag_mac --seed 12345 --difficulty 1 --weapon 0 --ngplus 0 --jump 2,4 --action fixture-haste --clean-profile
build/DisketteDungeon_diag_mac --seed 12345 --difficulty 1 --weapon 0 --ngplus 0 --action fixture-endings --clean-profile

HOME="$PWD/build/evidence/mac-first/save/valid-v1/home" build/DisketteDungeon_diag_mac --isolated-profile "$PWD/build/evidence/mac-first/save/valid-v1/home" --action fixture-save-roundtrip --expect v1
HOME="$PWD/build/evidence/mac-first/save/valid-v2/home" build/DisketteDungeon_diag_mac --isolated-profile "$PWD/build/evidence/mac-first/save/valid-v2/home" --action fixture-save-roundtrip --expect v2
HOME="$PWD/build/evidence/mac-first/save/bad-v1/home" build/DisketteDungeon_diag_mac --isolated-profile "$PWD/build/evidence/mac-first/save/bad-v1/home" --action fixture-save-reject --expect v1
HOME="$PWD/build/evidence/mac-first/save/bad-v2/home" build/DisketteDungeon_diag_mac --isolated-profile "$PWD/build/evidence/mac-first/save/bad-v2/home" --action fixture-save-reject --expect v2
```

`fixture-save-roundtrip`는 production `meta_load()` → `meta_save()` V2 rewrite → 메모리 clear → production `meta_load()` 순서로 검증하며 68바이트/체크섬 offset 64를 보존한다. `fixture-save-reject`는 체크섬이 틀린 V1/V2를 기본값으로 거부하고 파일을 다시 쓰지 않는다. `fixture-modifiers`, `fixture-haste`, `fixture-endings`는 각각 Courage/Kinship/Promise·용량, Haste의 AI 시간 영역, 코어/코다 매트릭스를 JSONL로 내보내고 고정 불변식을 위반하면 실패한다.

이 명령과 결과는 macOS/Metal 개발 증거일 뿐이며 Windows 실행, MinGW/공식 크기, native first-ten, OpenGL 패키징, PresentMon 검증을 주장하지 않는다.
