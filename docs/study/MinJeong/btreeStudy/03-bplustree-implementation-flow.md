# B+ Tree 구현 방식과 함수 흐름

이 문서는 우리 프로젝트에서 B+ Tree가 코드로 어떻게 구현되어 있는지 설명합니다.

01 문서는 개념, 02 문서는 프로젝트 적용 목적을 다뤘습니다.
이 문서는 실제 구현 구조와 함수 호출 흐름을 중심으로 봅니다.

## 1. 먼저 알아둘 점

현재 프로젝트에는 B+ Tree 구현을 볼 수 있는 파일이 두 곳 있습니다.

- [`src/bptree.c`](../../../../src/bptree.c)
- [`src/storage.c`](../../../../src/storage.c)

현재 기본 빌드(`Makefile`)는 `src/storage.c` 안의 B+ Tree 구현을 사용합니다.
`src/bptree.c`는 알고리즘을 따로 공부하기 좋은 파일로 보면 됩니다.

공부할 때는 이렇게 보면 좋습니다.

| 목적 | 볼 파일 |
| --- | --- |
| B+ Tree 알고리즘만 보기 | [`src/bptree.c`](../../../../src/bptree.c) |
| SQL 저장소와 연결된 실제 흐름 보기 | [`src/storage.c`](../../../../src/storage.c) |

이 문서의 함수 설명은 알고리즘은 `src/bptree.c`, 프로젝트 연결 흐름은 `src/storage.c` 기준으로 설명합니다.

## 2. 핵심 자료구조

### 2-1. `BptNode`

관련 코드:

- [`BptNode`](../../../../src/storage.c#L11)

```c
struct BptNode {
    bool is_leaf;
    size_t size;
    uint64_t keys[BPTREE_MAX_KEYS];
    union {
        struct {
            RowRef refs[BPTREE_MAX_KEYS];
            BptNode *next;
        } leaf;
        struct {
            BptNode *children[BPTREE_MAX_KEYS + 1];
        } internal;
    } as;
};
```

이 구조체는 leaf 노드와 internal 노드를 하나의 타입으로 표현합니다.

공통 필드:

- `is_leaf`: leaf 노드인지 구분한다.
- `size`: 현재 key 개수이다.
- `keys`: 정렬된 id key 배열이다.

leaf 노드 전용 필드:

- `refs`: 각 id에 대응되는 `row_ref` 배열이다.
- `next`: 오른쪽 leaf 노드를 가리킨다. 범위 조회에서 이 포인터를 따라 다음 leaf로 이동한다.

internal 노드 전용 필드:

- `children`: 다음 단계 자식 노드 포인터 배열이다.

## 3. 전체 함수 지도

```mermaid
flowchart TD
    A[index_init] --> B[bpt_free_node]
    A --> C[bpt_create_node]

    D[index_insert] --> E[bpt_insert_recursive]
    E --> F{현재 노드가 leaf인가?}
    F -- yes --> G[leaf 삽입 또는 leaf split]
    F -- no --> H[child 선택 후 재귀 삽입]
    H --> I{child가 split 되었나?}
    I -- no --> J[그대로 종료]
    I -- yes --> K[부모에 promoted_key 삽입]
    K --> L{부모도 가득 찼나?}
    L -- no --> J
    L -- yes --> M[internal split]
    E --> N{root가 split 되었나?}
    N -- yes --> O[새 root 생성]
    N -- no --> P[삽입 완료]

    Q[index_find] --> R[bpt_find]
    R --> S[internal node를 따라 leaf까지 이동]
    S --> T[leaf keys에서 id 검색]
    T --> U[row_ref 반환 또는 not found]
```

이 그래프에서 핵심은 split 결과가 아래에서 위로 올라간다는 점입니다.
leaf가 split되면 오른쪽 leaf의 첫 key가 부모로 올라가고, internal 노드가 split되면 중앙 key가 부모로 올라갑니다.

## 4. 인덱스 초기화 흐름

관련 함수:

- [`index_init`](../../../../src/bptree.c#L249)
- [`bpt_free_node`](../../../../src/bptree.c#L45)
- [`bpt_create_node`](../../../../src/bptree.c#L34)

```mermaid
sequenceDiagram
    participant Caller as 호출자
    participant Init as index_init
    participant Free as bpt_free_node
    participant Create as bpt_create_node

    Caller->>Init: 인덱스 초기화 요청
    Init->>Free: 기존 root 제거
    Free-->>Init: 제거 완료
    Init->>Create: 새 leaf root 생성
    Create-->>Init: BptNode* 반환
    Init-->>Caller: 성공 0 또는 실패 -1 반환
```

`index_init`은 기존 트리를 버리고, 빈 leaf 노드 하나를 새 root로 만듭니다.

처음에는 root가 곧 leaf입니다.

```text
[ empty leaf root ]
```

## 5. 검색 흐름

관련 함수:

- [`index_find`](../../../../src/bptree.c#L288)
- [`bpt_find`](../../../../src/bptree.c#L61)

```mermaid
flowchart TD
    A[index_find id] --> B{index 준비됨?}
    B -- no --> C[-1 반환]
    B -- yes --> D[bpt_find root, id]
    D --> E{현재 노드가 leaf인가?}
    E -- no --> F[key 비교로 child index 선택]
    F --> G[선택한 child로 이동]
    G --> E
    E -- yes --> H[leaf의 keys 배열 선형 검색]
    H --> I{id 찾음?}
    I -- yes --> J[out_ref에 row_ref 저장 후 0 반환]
    I -- no --> K[1 반환]
```

검색은 root에서 시작합니다.
internal 노드에서는 key를 비교해서 어느 child로 내려갈지 결정합니다.
leaf에 도착하면 leaf 안의 key 배열에서 id를 찾습니다.

찾으면 `row_ref`를 반환하고, 못 찾으면 `1`을 반환합니다.

반환값 의미:

```text
0  = 찾음
1  = 없음
-1 = 인덱스 준비 안 됨 또는 오류
```

## 6. 삽입 흐름

관련 함수:

- [`execute_statement`](../../../../src/executor.c#L7)
- [`append_insert_row`](../../../../src/storage.c#L1415)
- [`next_id`](../../../../src/storage.c#L360)
- [`binary_writer_append_row`](../../../../src/storage.c#L726)
- [`index_insert`](../../../../src/storage.c#L320)
- [`bpt_insert_recursive`](../../../../src/storage.c#L146)

```mermaid
sequenceDiagram
    participant User as 사용자
    participant Exec as execute_statement
    participant Append as append_insert_row
    participant Id as next_id
    participant Writer as binary_writer_append_row
    participant Insert as index_insert
    participant Rec as bpt_insert_recursive
    participant Root as root 갱신

    User->>Exec: INSERT 요청
    Exec->>Append: INSERT 실행 요청
    Append->>Append: table/schema 확인
    Append->>Append: 입력 column/value를 table column 순서로 정리
    Append->>Id: 새 id 요청
    Id-->>Append: generated_id = 마지막 id + 1
    Append->>Append: generated_id를 id 컬럼 값으로 넣기
    Append->>Writer: 완성된 row를 .data 파일 끝에 저장
    Writer-->>Append: row_ref 반환
    Append->>Insert: generated_id -> row_ref 인덱스 등록 요청
    Insert->>Insert: 인덱스 준비 상태 확인
    Insert->>Rec: root부터 재귀 삽입
    Rec->>Rec: id가 들어갈 leaf까지 내려가기
    Rec->>Rec: leaf에 id와 row_ref 삽입
    Rec-->>Insert: split 여부와 promoted_key 반환
    alt root split 발생
        Insert->>Root: 새 internal root 생성
        Root-->>Insert: g_index_root 갱신 완료
    else root split 없음
        Insert->>Insert: 기존 root 유지
    end
    Insert-->>Append: 인덱스 삽입 결과 반환
    Append-->>Exec: affected_rows = 1
    Exec-->>User: INSERT 결과 출력
```

사용자가 `INSERT`를 요청하면 처음부터 바로 `index_insert`로 들어가는 것이 아닙니다.
먼저 `append_insert_row`가 새 row를 만들고, `next_id`로 새 id를 발급하고, `.data` 파일에 row를 저장합니다.
이때 파일 안에서 row가 저장된 위치가 `row_ref`로 반환됩니다.

그 다음에야 `index_insert(generated_id, row_ref)`가 호출됩니다.
즉 B+ Tree에는 전체 row가 아니라 아래 매핑만 들어갑니다.

```text
generated_id -> row_ref
```

`index_insert`는 실제 B+ Tree 삽입을 `bpt_insert_recursive`에 맡깁니다.
`bpt_insert_recursive`는 root부터 시작해서 id가 들어갈 leaf까지 내려가고, leaf에 `id`와 `row_ref`를 정렬된 위치에 넣습니다.
삽입 중 node가 가득 차면 split이 발생하고, 그 결과가 부모 방향으로 올라갑니다.
만약 root까지 split되면 `index_insert`가 새 internal root를 만들어 `g_index_root`를 갱신합니다.

### 6-1. `index_insert` 내부 흐름

관련 코드:

- [`index_insert`](../../../../src/storage.c#L320)
- [`bpt_insert_recursive`](../../../../src/storage.c#L146)
- [`bpt_create_node`](../../../../src/storage.c#L41)

```mermaid
flowchart TD
    A["index_insert(id, row_ref) 호출"] --> B{"인덱스가 준비됐는가?"}
    B -->|"no"| C["-1 반환"]
    B -->|"yes"| D["bpt_insert_recursive(root, id, row_ref, &split)"]

    D --> E{"재귀 삽입 결과 rc != 0?"}
    E -->|"yes"| F["rc 그대로 반환<br/>1: 중복 id<br/>-1: 오류"]
    E -->|"no"| G{"root split 발생?"}

    G -->|"no"| H["0 반환"]
    G -->|"yes"| I["새 internal root 생성"]
    I --> J{"new_root 생성 성공?"}
    J -->|"no"| K["-1 반환"]
    J -->|"yes"| L["new_root.keys[0] = promoted_key"]
    L --> M["new_root.children[0] = 기존 root"]
    M --> N["new_root.children[1] = split.right"]
    N --> O["g_index_root = new_root"]
    O --> P["0 반환"]
```

`index_insert` 자체가 leaf에 직접 값을 넣지는 않습니다.
이 함수의 핵심 역할은 세 가지입니다.

```text
1. 인덱스를 사용할 수 있는 상태인지 확인한다.
2. 실제 삽입은 bpt_insert_recursive에 맡긴다.
3. root가 split되었으면 새 root를 만들어 트리 높이를 1 늘린다.
```

`bpt_insert_recursive`가 반환한 `split`은 "아래 node가 split되었으니 부모가 새 key와 오른쪽 node를 받아야 한다"는 신호입니다.
root에는 부모가 없기 때문에, root split이 발생하면 `index_insert`가 직접 새 root를 만들어야 합니다.

```text
root split 전:
[10 | 20 | 30 | ...]

root split 후:
        [promoted_key]
       /              \
 기존 root          split.right
```

### 6-2. `index_find`와 `index_insert`의 차이

`index_find`와 `index_insert`는 둘 다 root에서 시작해 leaf까지 내려간다는 점은 같습니다.
하지만 목적과 처리해야 하는 일이 다릅니다.

| 구분 | `index_find` | `index_insert` |
| --- | --- | --- |
| 쓰이는 상황 | `SELECT ... WHERE id = ?` | `INSERT`, 기존 데이터 인덱스 재구성 |
| 목적 | id로 기존 row 위치를 찾는다 | 새 `id -> row_ref` 매핑을 추가한다 |
| leaf 도착 후 | key를 찾고 `row_ref`를 반환한다 | 정렬 위치에 key/ref를 넣는다 |
| tree 구조 변경 | 하지 않는다 | node가 가득 차면 split으로 구조가 바뀔 수 있다 |
| root 변경 가능성 | 없음 | root split 시 새 root를 만들 수 있음 |
| 반환 의미 | 찾음/없음/오류 | 성공/중복 id/오류 |

즉 `index_find`는 **찾기만 하는 함수**이고, `index_insert`는 **찾아서 넣고 필요하면 트리 구조까지 조정하는 함수**입니다.

```text
index_find:
root -> leaf -> id 찾기 -> row_ref 반환

index_insert:
root -> leaf -> id 삽입 -> 필요하면 leaf/internal/root split
```

## 7. leaf 노드 삽입 흐름

`bpt_insert_recursive`가 leaf 노드를 만났을 때의 흐름입니다.

```mermaid
flowchart TD
    A[leaf node 도착] --> B[삽입 위치 insert_at 찾기]
    B --> C{id 중복인가?}
    C -- yes --> D[1 반환]
    C -- no --> E{leaf에 빈 공간이 있는가?}
    E -- yes --> F[key/ref 배열을 오른쪽으로 밀기]
    F --> G[id와 row_ref 삽입]
    G --> H[0 반환]
    E -- no --> I[기존 key/ref + 새 key/ref를 임시 배열에 병합]
    I --> J[왼쪽 절반은 기존 leaf에 저장]
    J --> K[오른쪽 절반은 새 right leaf에 저장]
    K --> L[leaf next 포인터 연결]
    L --> M[promoted_key = right의 첫 key]
    M --> N[out_split에 split 정보 저장]
    N --> O[0 반환]
```

leaf 노드가 가득 차지 않았다면 단순히 정렬 위치에 삽입합니다.

leaf 노드가 가득 찼다면 split합니다.
이때 오른쪽 leaf의 첫 key가 부모로 올라갈 key가 됩니다.

```text
promoted_key = right->keys[0]
```

## 8. internal 노드 삽입 흐름

`bpt_insert_recursive`가 internal 노드를 만났을 때의 흐름입니다.

```mermaid
flowchart TD
    A["internal node 도착"] --> B["id가 들어갈 child index 선택"]
    B --> C["선택한 child에 재귀 삽입"]
    C --> D{"child split 발생?"}
    D -->|"no"| E["0 반환"]
    D -->|"yes"| F{"현재 internal node에 빈 공간이 있는가?"}
    F -->|"yes"| G["promoted_key와 right child를 현재 노드에 삽입"]
    G --> H["0 반환"]
    F -->|"no"| I["기존 key, child와 승격 key, child를 임시 배열에 병합"]
    I --> J["mid 계산"]
    J --> K["왼쪽 key와 child는 기존 노드에 저장"]
    K --> L["오른쪽 key와 child는 새 right internal에 저장"]
    L --> M["promoted_key = merged_keys[mid]"]
    M --> N["out_split에 split 정보 저장"]
    N --> O["0 반환"]
```

internal 노드의 역할은 leaf까지 내려가는 길을 안내하는 것입니다.
child가 split되면 그 split 결과를 현재 internal 노드에 반영해야 합니다.

현재 internal 노드도 가득 찼다면 다시 split하고, 중앙 key를 부모로 올립니다.

### 8-1. k=4 예시로 보는 root까지 올라가는 split

아래 그림은 이해를 위해 한 node의 최대 key 수를 `4개`로 가정한 예시입니다.
실제 프로젝트 코드는 `BPTREE_MAX_KEYS`가 `31`이므로, 여기의 `k=4`는 흐름을 보기 위한 축소 예시입니다.

![k=4 internal split cascade](image/03-internal-split-cascade-k4.svg)

아래 예시는 실제로 가능한 트리 모양을 기준으로 잡았습니다.
`75`를 삽입하기 전, root와 `75`가 내려갈 internal node인 `C1`, 그리고 실제 삽입 대상 leaf가 모두 가득 차 있다고 가정합니다.

```text
root:
[50 | 100 | 150 | 200]

root children:
C0, C1, C2, C3, C4

id=75가 내려갈 대상 internal C1:
[60 | 70 | 80 | 90]

C1 children:
L10, L11, L12, L13, L14

id=75가 실제로 들어갈 leaf L12:
[70 | 71 | 72 | 74]
```

`75`는 root에서 `50 이상 100 미만` 범위이므로 `C1`로 내려갑니다.
그리고 `C1` 안에서는 `70 이상 80 미만` 범위이므로 `L12`로 내려갑니다.

먼저 `75`는 leaf에 들어가야 합니다.
하지만 leaf가 가득 차 있으므로 임시로 병합하면 아래처럼 됩니다.

```text
[70 | 71 | 72 | 74 | 75]
```

이 leaf를 둘로 나누면 다음과 같습니다.

```text
left leaf  = [70 | 71]
right leaf = [72 | 74 | 75]
```

B+ Tree의 leaf split에서는 오른쪽 leaf의 첫 key가 부모에게 전달됩니다.

```text
promoted_key = 72
```

부모 internal node `C1`은 이 `72`를 받아야 합니다.
하지만 `C1`도 이미 가득 차 있었습니다.

```text
기존 C1:
[60 | 70 | 80 | 90]

72를 받은 뒤의 임시 상태:
[60 | 70 | 72 | 80 | 90]
```

`k=4`인데 key가 5개가 되었으므로 internal node도 split됩니다.
중앙 key인 `72`는 위 부모, 즉 root로 올라갑니다.

```text
left internal  = [60 | 70]
promoted_key   = 72
right internal = [80 | 90]
```

이제 root가 `72`를 받아야 합니다.
그런데 root도 이미 가득 차 있었습니다.

```text
기존 root:
[50 | 100 | 150 | 200]

72를 받은 뒤의 임시 상태:
[50 | 72 | 100 | 150 | 200]
```

root도 key가 5개가 되었으므로 다시 split됩니다.
중앙 key인 `100`이 새 root가 됩니다.
이때 root가 가지고 있던 child들도 함께 나뉩니다.

여기서 `C0`, `C2`, `C3`, `C4`는 새로 만들어 넣은 노드가 아닙니다.
root가 split되기 전부터 이미 가지고 있던 child들을 설명하기 위해 붙인 이름입니다.
`C1-left`, `C1-right`만 이번 삽입 과정에서 기존 `C1`이 split되면서 생긴 두 internal child입니다.

root가 처음에 key를 4개 가지고 있었다면 child는 5개였습니다.

```text
기존 root:
[50 | 100 | 150 | 200]

기존 root children:
C0, C1, C2, C3, C4
```

그런데 `id=75`는 `50 이상 100 미만` 범위에 들어가므로 `C1` 쪽으로 내려갑니다.
삽입 과정에서 `C1`이 split되면, `C1` 하나가 `C1-left`, `C1-right` 둘로 나뉩니다.
그래서 root가 임시로 가지는 child는 5개에서 6개가 됩니다.

```text
임시 root keys:
[50 | 72 | 100 | 150 | 200]

임시 root children:
C0, C1-left, C1-right, C2, C3, C4
```

즉 depth를 맞추려고 임의로 child를 추가한 것이 아닙니다.
원래 있던 child들과, split 때문에 새로 생긴 오른쪽 child를 정렬 순서에 맞게 다시 배치한 것입니다.

root split 이후에는 아래처럼 됩니다.

```text
새 root:
[100]

새 root의 왼쪽 child:
[50 | 72]
  children = C0, C1-left, C1-right

새 root의 오른쪽 child:
[150 | 200]
  children = C2, C3, C4
```

여기서 오른쪽 child가 `[100 | 150 | 200]`이 아니라 `[150 | 200]`인 이유는, 이 단계가 **internal node split**이기 때문입니다.
leaf split에서는 오른쪽 leaf의 첫 key가 부모로 **복사**되어 leaf에도 남지만, internal split에서는 중앙 key가 부모로 **이동**하고 split된 internal node 안에서는 빠집니다.

```text
leaf split:
부모에 올라간 key가 leaf에도 남을 수 있다.

internal split:
부모에 올라간 중앙 key는 split된 internal node에서는 빠진다.
```

따라서 `100`은 새 root의 separator key가 되고, 오른쪽 internal child는 `100`보다 큰 범위를 맡는 `[150 | 200]`이 됩니다.
다만 실제 데이터 key `100`이 있다면, 그 값은 internal node가 아니라 leaf node 쪽에 남아 있을 수 있습니다.

여기서 주의할 점은 `[150 | 200]` 아래가 비어 있는 것이 아니라는 점입니다.
`[150 | 200]`은 `C2`, `C3`, `C4`라는 child internal node들을 가집니다.
마찬가지로 `[50 | 72]`도 `C0`, `C1-left`, `C1-right`를 child로 가집니다.

즉 구조를 더 펼쳐 쓰면 아래처럼 이해할 수 있습니다.

```text
                         [100]
                        /     \
                 [50 | 72]   [150 | 200]
                 /   |   \     /    |    \
               C0  C1L  C1R   C2   C3    C4
              / \   / \   / \  / \  / \   / \
            leaf leaf leaf leaf leaf leaf leaf leaf
```

그림에서도 이 부분을 생략하지 않고 leaf level까지 펼쳐 두었습니다.
실제 B+ Tree에서는 `C0`, `C1L`, `C1R`, `C2`, `C3`, `C4` 아래에 모두 leaf node들이 있고, 모든 leaf node의 depth는 동일하게 유지됩니다.
또 leaf node들은 `next` 포인터로 왼쪽에서 오른쪽으로 이어집니다.

즉 최악의 경우 삽입 하나가 아래처럼 위로 연쇄 전달될 수 있습니다.

```text
leaf split
-> 부모 internal에 promoted_key 전달
-> 부모 internal도 가득 차서 internal split
-> root에 promoted_key 전달
-> root도 가득 차서 root split
-> 새 root 생성
```

이때 중요한 점은 **아무 key나 마음대로 부모로 올리는 것이 아니라, split 대상 node의 중앙 key를 기준으로 나눈다**는 것입니다.
leaf split에서는 오른쪽 leaf의 첫 key가 부모에게 복사되고, internal split에서는 중앙 key가 부모로 올라갑니다.

## 9. 프로젝트 연결 흐름

B+ Tree 함수는 단독으로 존재하지 않고, 저장소 흐름에 연결됩니다.

### 9-1. 기존 데이터로 인덱스 재구성

관련 함수:

- [`activate_storage_for_table`](../../../../src/storage.c#L1024)
- [`binary_reader_scan_all`](../../../../src/storage.c#L813)
- [`build_index_callback`](../../../../src/storage.c#L995)
- [`index_insert`](../../../../src/storage.c#L320)

```mermaid
sequenceDiagram
    participant Active as activate_storage_for_table
    participant Init as index_init
    participant Scan as binary_reader_scan_all
    participant Callback as build_index_callback
    participant Insert as index_insert

    Active->>Init: 빈 인덱스 생성
    Init-->>Active: 준비 완료
    Active->>Scan: 바이너리 파일 전체 scan 요청
    loop 각 row
        Scan->>Callback: row_ref + row values 전달
        Callback->>Insert: id -> row_ref 삽입
        Insert-->>Callback: 삽입 결과 반환
        Callback-->>Scan: 계속 scan
    end
    Scan-->>Active: scan 완료
    Active-->>Active: max_id를 next_id 기준으로 저장
```

프로젝트의 인덱스는 메모리 기반이기 때문에 테이블을 활성화할 때 기존 데이터로 인덱스를 다시 만듭니다.

### 9-2. INSERT 후 인덱스 등록

관련 함수:

- [`append_insert_row`](../../../../src/storage.c#L1415)
- [`binary_writer_append_row`](../../../../src/storage.c#L726)
- [`index_insert`](../../../../src/storage.c#L320)

```mermaid
sequenceDiagram
    participant Append as append_insert_row
    participant Id as next_id
    participant Writer as binary_writer_append_row
    participant Insert as index_insert

    Append->>Id: 새 id 요청
    Id-->>Append: generated_id 반환
    Append->>Writer: row 바이너리 append
    Writer-->>Append: row_ref 반환
    Append->>Insert: generated_id -> row_ref 삽입
    Insert-->>Append: 삽입 결과 반환
```

새 row가 저장되면, 저장 위치인 `row_ref`가 바로 B+ Tree에 등록됩니다.

### 9-3. SELECT WHERE id 조회

관련 함수:

- [`run_select_query`](../../../../src/storage.c#L1546)
- [`is_id_equality_predicate`](../../../../src/storage.c#L1192)
- [`run_select_by_id`](../../../../src/storage.c#L1246)
- [`index_find`](../../../../src/storage.c#L348)
- [`binary_reader_read_row_at`](../../../../src/storage.c#L784)

```mermaid
flowchart TD
    A[run_select_query] --> B{WHERE id = 숫자?}
    B -- no --> C[run_select_linear]
    B -- yes --> D[run_select_by_id]
    D --> E[index_find]
    E --> F{id 찾음?}
    F -- no --> G[빈 결과]
    F -- yes --> H[row_ref 반환]
    H --> I[binary_reader_read_row_at]
    I --> J[QueryResult에 row 추가]
```

id 조건이면 B+ Tree에서 `row_ref`를 찾고, 그 위치의 row 하나만 읽습니다.

### 9-4. SELECT WHERE id 범위 조회

관련 함수:

- [`is_id_range_predicate`](../../../../src/storage.c#L1208)
- [`run_select_by_id_range`](../../../../src/storage.c#L1260)
- [`bpt_lower_bound`](../../../../src/storage.c#L108)
- [`bpt_leftmost_leaf`](../../../../src/storage.c#L88)
- [`append_row_by_ref`](../../../../src/storage.c#L1225)

```mermaid
flowchart TD
    A["run_select_query"] --> B{"WHERE id >, >=, <, <= 숫자?"}
    B -->|"no"| C["run_select_linear"]
    B -->|"yes"| D["run_select_by_id_range"]

    D --> E{"op가 > 또는 >= 인가?"}
    E -->|"yes"| F["bpt_lower_bound로 시작 leaf/index 찾기"]
    F --> G["현재 leaf의 key들을 오른쪽으로 확인"]
    G --> H["조건에 맞는 row_ref를 append_row_by_ref로 읽기"]
    H --> I{"leaf.next가 있는가?"}
    I -->|"yes"| J["leaf = leaf.next, index = 0"]
    J --> G
    I -->|"no"| K["범위 조회 종료"]

    E -->|"no: < 또는 <="| L["bpt_leftmost_leaf로 가장 왼쪽 leaf 찾기"]
    L --> M["왼쪽부터 key 확인"]
    M --> N{"key가 범위를 벗어났는가?"}
    N -->|"yes"| K
    N -->|"no"| O["row_ref를 append_row_by_ref로 읽기"]
    O --> P{"현재 leaf에 다음 key가 있는가?"}
    P -->|"yes"| M
    P -->|"no"| Q{"leaf.next가 있는가?"}
    Q -->|"yes"| R["다음 leaf로 이동"]
    R --> M
    Q -->|"no"| K
```

예를 들어 leaf들이 아래처럼 연결되어 있다고 가정합니다.

```text
[1 | 5 | 9] -> [12 | 20 | 35] -> [42 | 50 | 60] -> [70 | 80]
```

`WHERE id >= 10`은 `bpt_lower_bound`로 `12`가 있는 leaf 위치를 찾은 뒤 오른쪽으로 계속 이동합니다.

```text
시작: [12 | 20 | 35]의 12
읽기: 12, 20, 35
next: [42 | 50 | 60]
읽기: 42, 50, 60
next: [70 | 80]
읽기: 70, 80
```

`WHERE id <= 50`은 가장 왼쪽 leaf부터 읽다가 `50`보다 큰 key를 만나면 멈춥니다.

```text
시작: [1 | 5 | 9]
읽기: 1, 5, 9
next: [12 | 20 | 35]
읽기: 12, 20, 35
next: [42 | 50 | 60]
읽기: 42, 50
60에서 조건을 벗어나므로 중단
```

이것이 B+ Tree에서 leaf 연결 리스트가 중요한 이유입니다.
단건 조회는 leaf에서 key 하나를 찾고 끝나지만, 범위 조회는 시작 leaf를 찾은 뒤 leaf들의 `next`를 따라가며 여러 row를 읽습니다.

## 10. 이 구현에서 중요한 반환 구조

삽입 중 split 여부는 `BptSplitResult`로 부모에게 전달됩니다.

관련 코드:

- [`BptSplitResult`](../../../../src/storage.c#L27)

```c
typedef struct {
    bool split;
    uint64_t promoted_key;
    BptNode *right;
} BptSplitResult;
```

필드 의미:

- `split`: 현재 노드가 split되었는지 여부
- `promoted_key`: 부모로 올릴 key
- `right`: split 후 새로 생긴 오른쪽 노드

이 구조체 덕분에 child 노드의 split 결과가 부모 노드로 전달됩니다.

```text
child split
-> promoted_key와 right node 생성
-> parent가 그 정보를 받아 자신의 key/child 배열에 삽입
```

## 11. 이 문서에서 기억할 것

우리 프로젝트의 B+ Tree 구현은 다음 흐름으로 이해하면 됩니다.

1. `index_init`으로 빈 트리를 만든다.
2. `index_insert`가 `bpt_insert_recursive`로 leaf까지 내려가 id와 row_ref를 넣는다.
3. 노드가 가득 차면 split하고, `BptSplitResult`로 부모에게 승격 정보를 전달한다.
4. `index_find`는 internal node를 따라 leaf까지 내려가 id를 찾고 row_ref를 반환한다.
5. `run_select_by_id_range`는 leaf의 `next` 연결을 따라 id 범위에 해당하는 row_ref들을 순서대로 읽는다.
6. 저장소 계층은 이 row_ref로 바이너리 파일의 특정 위치를 바로 읽는다.

한 문장으로 요약하면:

```text
B+ Tree 구현은 id를 정렬된 key로 관리하고, leaf node에 저장된 row_ref와 leaf.next 연결을 통해 단건 row 위치 또는 범위 row 위치들을 빠르게 찾도록 만든 구조이다.
```
