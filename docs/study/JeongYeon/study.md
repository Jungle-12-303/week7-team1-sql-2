# 순차 호출 흐름(SELECT)
1. `index_find` (`bptree.c`)
- B+ tree 접근 진입점이다.
- 내부에서 `bpt_find(g_index_root, id, out_ref)`를 호출한다.

2. `bpt_find` (`bptree.c`)
- 루트부터 리프까지 내려가며 `id`를 탐색한다.
- 첫 번째 `while`에서 `node`를 계속 자식으로 갱신하며 내부 노드를 내려간다.
- 리프에 도착하면 `for`로 리프 키를 순회해 일치 키를 찾는다.
- 일치 시 `*out_ref = row_ref`를 기록하고 성공 코드를 반환한다.

3. `run_select_by_id` (`storage.c`)
- 반환된 `out_ref`(row_ref)를 사용해 실제 row를 읽는다.
- 즉시 `binary_reader_read_row_at(ref, &row_values)`를 호출한다.

## 시퀀스 다이어그램
```mermaid
sequenceDiagram
    autonumber
    participant RSI as run_select_by_id (storage.c)
    participant IF as index_find (bptree.c)
    participant BF as bpt_find (bptree.c)
    participant BR as binary_reader_read_row_at (storage.c)
    participant QR as query_result_append_row (storage.c)

    RSI->>IF: index_find(id, &ref)
    IF->>BF: bpt_find(g_index_root, id, out_ref)
    Note over BF: internal node while 탐색\nnode = children[i] 반복
    Note over BF: leaf node for 순회\nkey 일치 시 *out_ref = row_ref
    BF-->>IF: return 0 (found)
    IF-->>RSI: ref가 채워진 상태로 복귀
    RSI->>BR: binary_reader_read_row_at(ref, &row_values)
    BR-->>RSI: row_values
    RSI->>QR: query_result_append_row(out, &row_values)
    QR-->>RSI: success
```


# 순차 호출 흐름(INSERT)
1. `binary_writer_append_row (storage.c)`
- binary 파일에 쓰여진 해당 row의 row_ref를 반환한다.

2. `index_insert (bptree.c)`
- id를 생성하고 row_ref를 받아와서 B+tree에 넣으러 들어간다(중복 id 필터링).

3. `bpt_insert_recursive (bptree.c)`
- B+tree에 삽입을 하는 로직. node 아래로 내려가기, 알맞은 leaf에 넣기 실행
- 만약 node의 분할이 필요하다면? => `bpt_create_node`를 호출하여 분할 시행

## 컴포넌트 다이어그램

```mermaid
flowchart LR
    ST["storage.c\nappend_insert_row"] --> BW["storage.c\nbinary_writer_append_row"]
    ST --> BI["bptree.c\nindex_insert"]
    BI --> BR["bptree.c\nbpt_insert_recursive"]
    BR -. split 필요 시 .-> BC["bptree.c\nbpt_create_node"]
```

## Root Split 전/후 비교
```mermaid
flowchart LR
    subgraph LEFT["Before (Root Full)"]
        R["Root\n[20, 40, 60]"]
        A["Leaf1\n[5,10,15]"]
        B["Leaf2\n[20,25,30]"]
        C["Leaf3\n[40,45,50]"]
        D["Leaf4\n[60,65,70]"]
        R --> A
        R --> B
        R --> C
        R --> D
    end

    subgraph RIGHT["After (Root Split)"]
        NR["New Root\n[40]"]
        IL["Internal L\n[20]"]
        IR["Internal R\n[60]"]
        A2["Leaf1\n[5,10,15]"]
        B2["Leaf2\n[20,25,30]"]
        C2["Leaf3\n[40,45,50]"]
        D2["Leaf4\n[60,65,70]"]

        NR --> IL
        NR --> IR
        IL --> A2
        IL --> B2
        IR --> C2
        IR --> D2
    end

    LEFT -. promoted key 40 .-> RIGHT
```
