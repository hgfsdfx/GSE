# Actor와 SceneGraph

화면에 배치되는 게임 오브젝트의 소유권과 수명을 `SceneGraph`로 통합했다.
`LevelOne`은 레벨 시작·진행과 갱신 단계 조율을 맡으며, 개별 오브젝트의 동작은 각 Actor가 처리한다.

## 배치 구조

```text
SceneGraph
├─ GroupActor (게임플레이)
│  ├─ PlayerActor
│  │  └─ SmartphoneActor
│  ├─ EnemyActor (정찰 / 장갑 / 보스)
│  ├─ ProjectileActor
│  ├─ LootActor
│  ├─ PulseActor
│  └─ CombatNumberActor
├─ ChunkActor (로드된 청크마다 하나)
│  ├─ GroundActor
│  ├─ BuildingActor
│  ├─ DeviceActor
│  └─ TrafficActor
└─ PresentationRootActor
   ├─ PresentationActor (광장·사거리 표시)
   ├─ PresentationActor (스캔·위치 표시)
   └─ PresentationActor (HUD)
```

창문·바퀴·글자 같은 도형은 오브젝트를 구성하는 메시이다. 각 삼각형을 Actor로 만들지 않는다.
HUD의 패널과 글자는 하나의 PresentationActor에서 구성하며, 기존 화면 레이아웃을 사용한다.

## 공통 제어

- `Actor::Id()`는 현재 SceneGraph 안에서의 식별자이다.
- `SetLocalPosition()`은 부모 기준 위치, `SetWorldPosition()`은 월드 위치를 설정한다.
- `WorldPosition()`은 부모 위치를 누적한다. 현재 변환은 2D 평행 이동이며 회전·스케일 계층은 포함하지 않는다.
- `active = false`는 자신과 자식의 갱신·렌더링을 막는다.
- `visible = false`는 자신과 자식의 렌더링을 막으며 갱신은 계속한다.
- `Attach(child, parent)`는 기본적으로 월드 위치를 유지하면서 부모를 바꾼다. 순환 연결은 거부한다.
- `Destroy()`는 자신과 자식을 삭제 대기 상태로 만든다. 이후 조회·갱신·그리기에서 즉시 제외한다.
- 실제 메모리 해제는 `FlushDestroyed()`에서 한다. 순회 중에는 해제하지 않는다.
- Actor는 직접 복사하거나 삭제하지 않고 SceneGraph에서 생성·삭제한다.

SceneGraph는 unique_ptr로 Actor를 소유한다. 갱신은 단계 시작 시점의 ID 목록을 사용하므로,
Actor가 Update 중 발사체나 아이템을 만들어도 현재 순회가 무효화되지 않는다.
같은 단계에서 새로 만들어진 Actor는 다음 단계 호출부터, 이후 단계의 Actor는 해당 단계부터 갱신한다.
WorldPosition 계산과 부모 참조는 SceneGraph 이동 후에도 다시 연결된다.

타입 조회는 일치하는 Actor ID 목록을 캐싱한다. 파생 타입도 조회에 포함하며 생성·삭제 시 목록을 갱신한다.
`Actors<T>()`가 반환하는 ActorView는 소유권 없는 스냅샷이다. 실제 삭제·씬 교체를 넘겨 보관하지 않는다.
장기간 참조에는 ActorId와 `Find()`를 사용한다. ID는 씬 교체 이후까지 유효한 저장용 ID가 아니다.
갱신·그리기·생성은 현재 게임처럼 메인 스레드에서 호출한다.

## 파일별 책임

| 파일 | 책임 |
| --- | --- |
| Actor.h/cpp | 위치·부모·수명·갱신·렌더링 공통 인터페이스 |
| SceneGraph.h/cpp | Actor 소유권, 계층, 타입 조회, 단계별 갱신, 레이어·깊이 정렬 |
| GameplayActors.h | 게임플레이 Actor 선언, ActorUpdateContext, GameplayEvents 인터페이스 |
| PlayerActors.cpp | 플레이어 이동, 무적·쿨타임, 스마트폰 자동 조준·발사 |
| EnemyActor.cpp | 일반 적 추적과 보스 공격 |
| ProjectileActor.cpp | 발사체 이동·추적·충돌·피해 처리 |
| LootActor.cpp | 자석 이동, 습득, 회복·성장 보상 요청 |
| EffectActors.cpp | 공격 예고와 전투 숫자의 수명 |
| PlayerStats.h/cpp | 경험치, 성장 수치와 파생 능력치 |
| NavigationGrid.h/cpp | 플레이어 주변 BFS, 경로 검사, 충돌을 고려한 이동 |
| WorldTypes.h | 저장·생성에 쓰는 좌표와 지형 설명 데이터 |
| WorldActors.h/cpp | 청크·지면·건물·장치·차량 Actor |
| WorldScene.cpp | 청크의 Actor 생성·해제, 건물 충돌 인덱스 |
| LevelOne.cpp | 레벨 초기화, 서비스 접근, 갱신 단계 조율 |
| LevelOneEncounters.cpp | 레벨 1 적 생성, 인원 보충, 보스 등장 조건 |
| LevelOneRewards.cpp | 레벨 1 처치 보상·클리어 정책과 게임플레이 이벤트 처리 |
| LevelOneSave.cpp | 기존 저장 형식 읽기·쓰기, Actor 재생성 |
| PrototypeScene.cpp | Actor별 그리기 등록, 프레젠테이션 Actor 구성, 아이소메트릭 깊이 규칙 |
| Prototype.cpp / PrototypeLevel.cpp | 입력 전달, 카메라, 해킹 조작과 실제 도형·UI 표현 |

Actor 동작 파일은 LevelOne을 직접 포함하지 않는다.
다른 레벨은 GameplayEvents를 구현하고 World·NavigationGrid·PlayerActor를 제공하여 같은 Actor를 사용할 수 있다.
보스의 이동 경계도 Actor의 arenaMinimum/arenaMaximum 설정이며 레벨 배치·불러오기에서 지정한다.

## 갱신과 렌더링

갱신 순서:

1. 전투 숫자 등 효과 수명
2. 플레이어 이동 → 주변 청크 스트리밍 → 길찾기 갱신·적 생성
3. 무기 → 적 → 범위 공격 → 발사체 → 아이템 → 배경 오브젝트
4. 삭제 대기 Actor 해제

렌더링 순서:

`Ground → GroundEffect → World → Marker → HDR/Bloom 합성 → Overlay → Hud`

각 레이어는 SceneGraph가 깊이순으로 그린다. 별도의 Item 벡터나 타입 번호 분기는 사용하지 않는다.
게임별 그리기는 ActorRenderContext에 타입별로 등록한다. 새 타입은 Register<T>()로 표현을 추가하거나 Render()를 재정의한다.
메시 캐시와 외부 .vs/.fs 셰이더는 기존 Renderer를 통해 사용한다.

## 월드와 충돌

World는 시드 기반 지형 설명과 해킹 상태를 관리한다. 실행 중 배치된 건물·장치의 위치는 Actor에서 읽는다.
로드된 청크가 사라지면 ChunkActor를 삭제하고 자식 건물·장치·차량을 함께 정리한다.
건물 충돌은 BuildingActor의 월드 좌표로 만든 청크별 인덱스를 사용한다.
위치·활성 상태를 외부에서 변경한 뒤 즉시 충돌을 조회하려면 World::RefreshColliders()를 호출한다.
일반 게임 루프는 플레이어 이동 전과 스트리밍 후 이를 호출한다.
visible만 끈 건물의 충돌은 유지한다. 충돌까지 제거하려면 active를 끄거나 삭제한다.
World는 SceneGraph를 소유하지 않으므로 연결된 SceneGraph가 살아 있는 동안 사용해야 한다.

## 확장 예

```cpp
auto& scene = level.Scene();
auto& group = scene.Spawn<GroupActor>(0);
group.SetWorldPosition({640, 640});

auto& medkit = scene.SpawnAt<LootActor>(group.Id(), WorldPoint{730, 990}, LootKind::Medkit, 32);
const ActorId id = medkit.Id();

scene.Attach(id, level.GameplayRoot()); // 월드 위치 유지
if (auto actor = scene.Find(id))
{
    actor->SetWorldPosition({750, 990});
}

scene.Destroy(id); // 순회 종료 이후 FlushDestroyed에서 해제
```

새 종류는 Actor를 상속하여 Update를 구현하고, 갱신 단계와 렌더 레이어를 선택한다.
생성과 조회는 SceneGraph를 사용하고, 그리기는 PrototypeScene.cpp의 타입 등록으로 연결한다.
여러 자식을 묶어 이동·활성화·삭제할 때는 GroupActor를 부모로 사용한다.

## 저장과 범위

기존 GSE_LEVEL_ONE 버전 1·2 읽기와 버전 2 쓰기를 유지한다.
플레이어 능력치와 적·아이템 상태를 저장하고, 읽을 때 Actor로 다시 생성한다.
적의 persistentId는 저장·발사체 목표용이며 SceneGraph의 런타임 ActorId와 구분한다.
청크 Actor는 시드로 재생성하며, 임의로 추가한 새 Actor나 건물의 런타임 이동까지 저장하는 범용 씬 직렬화는 포함하지 않는다.
새 저장 대상 타입을 추가할 때는 해당 타입의 저장 데이터와 버전 정책도 별도로 정의해야 한다.

사용자 요청에 따라 빌드·실행·테스트·렌더링 결과 확인은 수행하지 않았다.
