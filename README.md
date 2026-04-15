# B+ Tree Index Mini SQL

- 기존 Mini SQL 처리기에 `자동 ID`, `바이너리 저장`, `메모리 기반 B+ Tree 인덱스`를 결합한 프로젝트
- `WHERE id = ?` 및 `WHERE id` 범위 조건을 인덱스 경로로 처리
- 비인덱스 조건은 선형 탐색으로 처리
- 1,000,000건 이상 데이터 기준 성능 비교 수행

## 1. 서비스

### 1-1. 한 줄 설명

- `INSERT` 시 자동으로 ID를 부여하면 해당 ID를 B+ Tree 인덱스에 등록해 `WHERE id = ?` 조회를 빠르게 처리하는 Mini SQL 엔진

### 1-2. 프로젝트 목표

- 기존 SQL 처리기의 선형 탐색 기반 조회 구조 확장
- `WHERE id = ?` 조건에서 인덱스 사용 가능하도록 개선
- 대용량 데이터에서 인덱스 조회와 선형 탐색의 차이 검증
- 기존 SQL 처리기와 인덱스 구조의 자연스러운 연결

### 1-3. 지원 기능

- `INSERT`
- `SELECT *`
- `SELECT ... WHERE id = ?`
- `SELECT ... WHERE id > ?`, `>= ?`, `< ?`, `<= ?`
- `SELECT ... WHERE major = ?` 등 비인덱스 조건 조회
- CLI 기반 SQL 입력 및 실행
- 대량 데이터 삽입 및 성능 측정

### 1-4. 데이터 저장 구조

- 바이너리 row 포맷 사용
- 각 row를 파일 내 `row offset`으로 직접 접근
- B+ Tree는 `id -> row offset` 매핑 유지
- 인덱스 조회 시 파일 전체를 순회하지 않고 row 위치로 직접 이동

단순 B+ Tree 구조

```mermaid
%%{init: {
  "theme": "base",
  "themeVariables": {
    "background": "#0b1020",
    "primaryColor": "#111827",
    "primaryTextColor": "#f9fafb",
    "primaryBorderColor": "#f9fafb",
    "lineColor": "#e5e7eb",
    "secondaryColor": "#1f2937",
    "secondaryTextColor": "#f9fafb",
    "tertiaryColor": "#111827",
    "tertiaryTextColor": "#f9fafb"
  }
}}%%
flowchart TD
    subgraph Level0[" "]
        direction LR
        R0["Root<br/>4 | 8 | 11"]
    end

    subgraph Level1[" "]
        direction LR
        I1["Internal<br/>2 | 3"]
        I2["Internal<br/>5 | 7"]
        I3["Internal<br/>9 | 10"]
        I4["Internal<br/>12 | 13"]
    end

    subgraph Level2[" "]
        direction LR
        A1["Leaf<br/>1 | 2<br/>0x10 | 0x20"]
        A2["Leaf<br/>3<br/>0x30"]
        B1["Leaf<br/>4 | 5<br/>0x40 | 0x50"]
        B2["Leaf<br/>7<br/>0x70"]
        C1["Leaf<br/>8 | 9<br/>0x80 | 0x90"]
        C2["Leaf<br/>10<br/>0xA0"]
        D1["Leaf<br/>11 | 12<br/>0xB0 | 0xC0"]
        D2["Leaf<br/>13 | 14 | 15<br/>0xD0 | 0xE0 | 0xF0"]
    end

    R0 --> I1
    R0 --> I2
    R0 --> I3
    R0 --> I4

    I1 --> A1
    I1 --> A2
    I2 --> B1
    I2 --> B2
    I3 --> C1
    I3 --> C2
    I4 --> D1
    I4 --> D2

    classDef root fill:#166534,stroke:#dcfce7,stroke-width:3px,color:#f9fafb;
    classDef internal fill:#15803d,stroke:#dcfce7,stroke-width:2px,color:#f9fafb;
    classDef leaf fill:#22c55e,stroke:#dcfce7,stroke-width:2px,color:#052e16;
    linkStyle 0,1,2,3 stroke:#86efac,stroke-width:2.5px;
    linkStyle 4,5,6,7,8,9,10,11 stroke:#86efac,stroke-width:2px;
    class R0 root;
    class I1,I2,I3,I4 internal;
    class A1,A2,B1,B2,C1,C2,D1,D2 leaf;
```

```mermaid
%%{init: {
  "theme": "base",
  "themeVariables": {
    "background": "#0b1020",
    "primaryColor": "#111827",
    "primaryTextColor": "#f9fafb",
    "primaryBorderColor": "#f9fafb",
    "lineColor": "#e5e7eb",
    "secondaryColor": "#1f2937",
    "secondaryTextColor": "#f9fafb",
    "tertiaryColor": "#111827",
    "tertiaryTextColor": "#f9fafb"
  }
}}%%
flowchart LR
    A1["1 | 2<br/>0x10 | 0x20"] --> A2["3<br/>0x30"] --> B1["4 | 5<br/>0x40 | 0x50"] --> B2["7<br/>0x70"] --> C1["8 | 9<br/>0x80 | 0x90"] --> C2["10<br/>0xA0"] --> D1["11 | 12<br/>0xB0 | 0xC0"] --> D2["13 | 14 | 15<br/>0xD0 | 0xE0 | 0xF0"]

    classDef leaf fill:#22c55e,stroke:#dcfce7,stroke-width:2px,color:#052e16;
    linkStyle 0,1,2,3,4,5,6 stroke:#bbf7d0,stroke-width:2px;
    class A1,A2,B1,B2,C1,C2,D1,D2 leaf;
```

Leaf -> Binary Row 매핑

```mermaid
%%{init: {
  "theme": "base",
  "themeVariables": {
    "background": "#0b1020",
    "primaryColor": "#111827",
    "primaryTextColor": "#f9fafb",
    "primaryBorderColor": "#f9fafb",
    "lineColor": "#e5e7eb",
    "secondaryColor": "#1f2937",
    "secondaryTextColor": "#f9fafb",
    "tertiaryColor": "#111827",
    "tertiaryTextColor": "#f9fafb"
  }
}}%%
flowchart TD
    subgraph OffsetLevel[" "]
        direction LR
        O1["0x10"]
        O2["0x20"]
        O3["0x30"]
        O4["0x40"]
        O5["0x50"]
        O7["0x70"]
        O8["0x80"]
        O9["0x90"]
        O10["0xA0"]
        O11["0xB0"]
        O12["0xC0"]
        O13["0xD0"]
        O14["0xE0"]
        O15["0xF0"]
    end

    subgraph RowLevel[" "]
        direction LR
        D1["0x10<br/>id=1<br/>name=Kim<br/>major=CS"]
        D2["0x20<br/>id=2<br/>name=Lee<br/>major=Math"]
        D3["0x30<br/>id=3<br/>name=Park<br/>major=Physics"]
        D4["0x40<br/>id=4<br/>name=Choi<br/>major=CS"]
        D5["0x50<br/>id=5<br/>name=Jung<br/>major=Biology"]
        D7["0x70<br/>id=7<br/>name=Han<br/>major=Economics"]
        D8["0x80<br/>id=8<br/>name=Lim<br/>major=CS"]
        D9["0x90<br/>id=9<br/>name=Kang<br/>major=Math"]
        D10["0xA0<br/>id=10<br/>name=Yoon<br/>major=Design"]
        D11["0xB0<br/>id=11<br/>name=Seo<br/>major=CS"]
        D12["0xC0<br/>id=12<br/>name=Shin<br/>major=Chemistry"]
        D13["0xD0<br/>id=13<br/>name=Hwang<br/>major=History"]
        D14["0xE0<br/>id=14<br/>name=Oh<br/>major=Math"]
        D15["0xF0<br/>id=15<br/>name=Song<br/>major=CS"]
    end

    O1 -.-> D1
    O2 -.-> D2
    O3 -.-> D3
    O4 -.-> D4
    O5 -.-> D5
    O7 -.-> D7
    O8 -.-> D8
    O9 -.-> D9
    O10 -.-> D10
    O11 -.-> D11
    O12 -.-> D12
    O13 -.-> D13
    O14 -.-> D14
    O15 -.-> D15

    classDef offset fill:#111827,stroke:#cbd5e1,stroke-width:2px,color:#f9fafb;
    classDef data fill:#374151,stroke:#d1d5db,stroke-width:2px,color:#f9fafb;
    linkStyle 0,1,2,3,4,5,6,7,8,9,10,11,12,13 stroke:#d1d5db,stroke-width:2px,stroke-dasharray: 4 4;
    class O1,O2,O3,O4,O5,O7,O8,O9,O10,O11,O12,O13,O14,O15 offset;
    class D1,D2,D3,D4,D5,D7,D8,D9,D10,D11,D12,D13,D14,D15 data;
```

## 2. 파이프라인

### 2-1. 전체 처리 흐름

```mermaid
%%{init: {
  "theme": "base",
  "themeVariables": {
    "background": "#0b1020",
    "primaryColor": "#111827",
    "primaryTextColor": "#f9fafb",
    "primaryBorderColor": "#f9fafb",
    "lineColor": "#e5e7eb",
    "secondaryColor": "#1f2937",
    "secondaryTextColor": "#f9fafb",
    "tertiaryColor": "#0f172a",
    "tertiaryTextColor": "#f9fafb"
  }
}}%%
flowchart LR
    A["SQL Input"] --> B["Parser"]
    B --> C["Executor"]
    C --> D{"Query Type"}
    D -->|INSERT| E["Auto ID Assignment"]
    E --> F["Binary Row Append"]
    F --> G["B+ Tree Index Update"]
    D -->|SELECT WHERE id| H["B+ Tree Search"]
    H --> I["Direct Row Read by Offset"]
    D -->|SELECT other field| J["Linear Scan"]
    I --> K["Result Output"]
    J --> K

    classDef box fill:#111827,stroke:#f9fafb,stroke-width:2px,color:#f9fafb;
    classDef decision fill:#1e3a8a,stroke:#bfdbfe,stroke-width:2px,color:#eff6ff;
    class A,B,C,E,F,G,H,I,J,K box;
    class D decision;
```

### 2-2. INSERT 파이프라인

- SQL 입력
- Parser에서 INSERT 구문 해석
- Executor에서 다음 ID 생성
- Storage에 바이너리 row append
- append 결과로 `row offset` 획득
- B+ Tree에 `(id, row offset)` 등록

```mermaid
%%{init: {
  "theme": "base",
  "themeVariables": {
    "background": "#0b1020",
    "primaryColor": "#111827",
    "primaryTextColor": "#f9fafb",
    "primaryBorderColor": "#f9fafb",
    "lineColor": "#e5e7eb",
    "secondaryColor": "#1f2937",
    "secondaryTextColor": "#f9fafb",
    "tertiaryColor": "#111827",
    "tertiaryTextColor": "#f9fafb",
    "actorBkg": "#111827",
    "actorBorder": "#f9fafb",
    "actorTextColor": "#f9fafb",
    "signalColor": "#e5e7eb",
    "signalTextColor": "#f9fafb",
    "labelBoxBkgColor": "#111827",
    "labelBoxBorderColor": "#f9fafb",
    "labelTextColor": "#f9fafb",
    "loopTextColor": "#f9fafb",
    "noteBkgColor": "#1f2937",
    "noteBorderColor": "#f9fafb",
    "noteTextColor": "#f9fafb",
    "activationBorderColor": "#f9fafb",
    "activationBkgColor": "#1d4ed8"
  }
}}%%
sequenceDiagram
    participant U as User
    participant P as Parser
    participant E as Executor
    participant S as Storage
    participant B as B+ Tree

    U->>P: INSERT INTO ...
    P->>E: Parsed INSERT query
    E->>E: Generate next id
    E->>S: Append row in binary format
    S-->>E: Return row offset
    E->>B: Insert (id, row offset)
    E-->>U: Insert success
```

### 2-3. SELECT 파이프라인

- `WHERE id = ?` 또는 `WHERE id` 범위 조건은 B+ Tree 인덱스 경로 사용
- 인덱스 경로는 B+ Tree에서 row offset 탐색 후 offset 기반 direct read 수행
- `WHERE major = ?` 같은 비인덱스 조건은 B+ Tree를 거치지 않음
- 비인덱스 경로는 전체 row를 선형 탐색하며 조건 비교 수행

```mermaid
%%{init: {
  "theme": "base",
  "themeVariables": {
    "background": "#0b1020",
    "primaryColor": "#111827",
    "primaryTextColor": "#f9fafb",
    "primaryBorderColor": "#f9fafb",
    "lineColor": "#e5e7eb",
    "secondaryColor": "#1f2937",
    "secondaryTextColor": "#f9fafb",
    "tertiaryColor": "#111827",
    "tertiaryTextColor": "#f9fafb",
    "actorBkg": "#111827",
    "actorBorder": "#f9fafb",
    "actorTextColor": "#f9fafb",
    "signalColor": "#e5e7eb",
    "signalTextColor": "#f9fafb",
    "labelBoxBkgColor": "#111827",
    "labelBoxBorderColor": "#f9fafb",
    "labelTextColor": "#f9fafb",
    "loopTextColor": "#f9fafb",
    "noteBkgColor": "#1f2937",
    "noteBorderColor": "#f9fafb",
    "noteTextColor": "#f9fafb",
    "activationBorderColor": "#f9fafb",
    "activationBkgColor": "#166534"
  }
}}%%
sequenceDiagram
    participant U as User
    participant P as Parser
    participant E as Executor
    participant B as B+ Tree Index
    participant S as Storage

    rect rgb(17, 24, 39)
        Note over U,S: Indexed path: WHERE id = ? / WHERE id range
        U->>P: SELECT ... WHERE id ...
        P->>E: Parsed SELECT query
        E->>E: Detect id predicate
        E->>B: Search id / range
        B-->>E: row offset(s)
        E->>S: Read row(s) by offset
        S-->>E: row data
        E-->>U: Query result
    end

    rect rgb(31, 41, 55)
        Note over U,S: Non-indexed path: WHERE major = ? and other fields
        U->>P: SELECT ... WHERE major ...
        P->>E: Parsed SELECT query
        E->>E: Detect non-id predicate
        E->>S: Scan all rows
        S-->>E: matched rows
        E-->>U: Query result
    end
```

## 3. B+ Tree 인덱스 구조

- 이 섹션은 `demo.students(id, name, major, grade)` 기준으로 B+ Tree가 어떻게 동작하는지 설명합니다.
- 발표에서는 구조 설명을 먼저 하고, 시간이 남으면 `7. 코드 레벨 핵심 구현`으로 내려가 함수 단위로 설명합니다.

### 3-1. 왜 B+ Tree를 사용했는가

- 목표는 `WHERE id = ?`를 전체 파일 선형 탐색 없이 처리하는 것입니다.
- 학생 레코드는 바이너리 파일에 저장하고, B+ Tree는 `id -> row offset`만 관리합니다.
- 따라서 `id` 조건은 인덱스를 통해 빠르게 위치를 찾고, `major` 같은 다른 조건은 기존처럼 선형 탐색합니다.

```mermaid
%%{init: {
  "theme": "base",
  "themeVariables": {
    "background": "#0b1020",
    "primaryColor": "#111827",
    "primaryTextColor": "#f9fafb",
    "primaryBorderColor": "#f9fafb",
    "lineColor": "#e5e7eb",
    "secondaryColor": "#1f2937",
    "secondaryTextColor": "#f9fafb",
    "tertiaryColor": "#0f172a",
    "tertiaryTextColor": "#f9fafb"
  }
}}%%
flowchart LR
    A["WHERE id = 8"] --> B["B+ Tree Search"]
    B --> C["row offset 0x80"]
    C --> D["Read one student row"]

    E["WHERE major = 'CS'"] --> F["Linear Scan"]
    F --> G["Compare all student rows"]

    classDef index fill:#1e3a8a,stroke:#bfdbfe,stroke-width:2px,color:#eff6ff;
    classDef scan fill:#374151,stroke:#d1d5db,stroke-width:2px,color:#f9fafb;
    class A,B,C,D index;
    class E,F,G scan;
```

### 3-2. B+ Tree는 어떻게 생겼는가

- Root는 어느 자식 노드로 내려갈지 결정합니다.
- Internal node는 탐색 경로를 분기합니다.
- Leaf node는 실제 `(student id, row offset)`를 저장합니다.
- Leaf node끼리 연결되어 있어 범위 조회를 이어서 처리할 수 있습니다.

```mermaid
%%{init: {
  "theme": "base",
  "themeVariables": {
    "background": "#0b1020",
    "primaryColor": "#111827",
    "primaryTextColor": "#f9fafb",
    "primaryBorderColor": "#f9fafb",
    "lineColor": "#e5e7eb",
    "secondaryColor": "#1f2937",
    "secondaryTextColor": "#f9fafb",
    "tertiaryColor": "#111827",
    "tertiaryTextColor": "#f9fafb"
  }
}}%%
flowchart TD
    subgraph Level0[" "]
        direction LR
        R0["Root<br/>4 | 8 | 11"]
    end

    subgraph Level1[" "]
        direction LR
        I1["Internal<br/>2 | 3"]
        I2["Internal<br/>5 | 7"]
        I3["Internal<br/>9 | 10"]
        I4["Internal<br/>12 | 13"]
    end

    subgraph Level2[" "]
        direction LR
        A1["Leaf<br/>1 | 2<br/>0x10 | 0x20"]
        A2["Leaf<br/>3<br/>0x30"]
        B1["Leaf<br/>4 | 5<br/>0x40 | 0x50"]
        B2["Leaf<br/>7<br/>0x70"]
        C1["Leaf<br/>8 | 9<br/>0x80 | 0x90"]
        C2["Leaf<br/>10<br/>0xA0"]
        D1["Leaf<br/>11 | 12<br/>0xB0 | 0xC0"]
        D2["Leaf<br/>13 | 14 | 15<br/>0xD0 | 0xE0 | 0xF0"]
    end

    R0 --> I1
    R0 --> I2
    R0 --> I3
    R0 --> I4
    I1 --> A1
    I1 --> A2
    I2 --> B1
    I2 --> B2
    I3 --> C1
    I3 --> C2
    I4 --> D1
    I4 --> D2

    classDef root fill:#166534,stroke:#dcfce7,stroke-width:3px,color:#f9fafb;
    classDef internal fill:#15803d,stroke:#dcfce7,stroke-width:2px,color:#f9fafb;
    classDef leaf fill:#22c55e,stroke:#dcfce7,stroke-width:2px,color:#052e16;
    linkStyle 0,1,2,3 stroke:#86efac,stroke-width:2.5px;
    linkStyle 4,5,6,7,8,9,10,11 stroke:#86efac,stroke-width:2px;
    class R0 root;
    class I1,I2,I3,I4 internal;
    class A1,A2,B1,B2,C1,C2,D1,D2 leaf;
```

```mermaid
%%{init: {
  "theme": "base",
  "themeVariables": {
    "background": "#0b1020",
    "primaryColor": "#111827",
    "primaryTextColor": "#f9fafb",
    "primaryBorderColor": "#f9fafb",
    "lineColor": "#e5e7eb",
    "secondaryColor": "#1f2937",
    "secondaryTextColor": "#f9fafb",
    "tertiaryColor": "#111827",
    "tertiaryTextColor": "#f9fafb"
  }
}}%%
flowchart LR
    A1["1 | 2<br/>0x10 | 0x20"] --> A2["3<br/>0x30"] --> B1["4 | 5<br/>0x40 | 0x50"] --> B2["7<br/>0x70"] --> C1["8 | 9<br/>0x80 | 0x90"] --> C2["10<br/>0xA0"] --> D1["11 | 12<br/>0xB0 | 0xC0"] --> D2["13 | 14 | 15<br/>0xD0 | 0xE0 | 0xF0"]

    classDef leaf fill:#22c55e,stroke:#dcfce7,stroke-width:2px,color:#052e16;
    linkStyle 0,1,2,3,4,5,6 stroke:#bbf7d0,stroke-width:2px;
    class A1,A2,B1,B2,C1,C2,D1,D2 leaf;
```

### 3-3. B+ Tree는 어떻게 생성되는가

- 테이블이 활성화되면 먼저 인덱스를 초기화합니다.
- 바이너리 파일에 기존 학생 레코드가 있다면 전체를 한 번 스캔해 인덱스를 재구성합니다.
- 이 과정에서 가장 큰 `id`를 찾아 다음 `INSERT`에 사용할 자동 ID도 함께 준비합니다.

```mermaid
%%{init: {
  "theme": "base",
  "themeVariables": {
    "background": "#0b1020",
    "primaryColor": "#111827",
    "primaryTextColor": "#f9fafb",
    "primaryBorderColor": "#f9fafb",
    "lineColor": "#e5e7eb",
    "secondaryColor": "#1f2937",
    "secondaryTextColor": "#f9fafb",
    "tertiaryColor": "#0f172a",
    "tertiaryTextColor": "#f9fafb"
  }
}}%%
flowchart LR
    A["load_table_definition()"] --> B["activate_storage_for_table()"]
    B --> C["index_init()"]
    C --> D["binary_reader_scan_all()"]
    D --> E["build_index_callback()"]
    E --> F["g_next_id_counter = max id"]

    classDef box fill:#111827,stroke:#f9fafb,stroke-width:2px,color:#f9fafb;
    class A,B,C,D,E,F box;
```

### 3-4. INSERT 시 노드는 어떻게 추가되는가

- 학생 1명을 `INSERT`하면 먼저 새 `id`를 생성합니다.
- `(id, name, major, grade)`를 바이너리 row로 파일에 append 합니다.
- append 결과로 얻은 `row offset`을 leaf node에 함께 넣습니다.
- 노드가 가득 찬 경우 split을 수행하고, 분할 결과를 부모 노드로 올립니다.

예시 레코드

```sql
INSERT INTO demo.students (name, major, grade)
VALUES ("Kim", "CS", "3");
```

```mermaid
%%{init: {
  "theme": "base",
  "themeVariables": {
    "background": "#0b1020",
    "primaryColor": "#111827",
    "primaryTextColor": "#f9fafb",
    "primaryBorderColor": "#f9fafb",
    "lineColor": "#e5e7eb",
    "secondaryColor": "#1f2937",
    "secondaryTextColor": "#f9fafb",
    "tertiaryColor": "#111827",
    "tertiaryTextColor": "#f9fafb",
    "actorBkg": "#111827",
    "actorBorder": "#f9fafb",
    "actorTextColor": "#f9fafb",
    "signalColor": "#e5e7eb",
    "signalTextColor": "#f9fafb",
    "labelBoxBkgColor": "#111827",
    "labelBoxBorderColor": "#f9fafb",
    "labelTextColor": "#f9fafb",
    "noteBkgColor": "#1f2937",
    "noteBorderColor": "#f9fafb",
    "noteTextColor": "#f9fafb"
  }
}}%%
sequenceDiagram
    participant U as User
    participant E as Executor
    participant S as Storage
    participant B as B+ Tree

    U->>E: INSERT INTO demo.students ...
    E->>E: next_id() -> 16
    E->>S: binary_writer_append_row()
    S-->>E: row offset 0x100
    E->>B: index_insert(16, 0x100)
    B->>B: leaf insert / split if needed
    E-->>U: INSERT 1
```

### 3-5. 단건 조회는 어떻게 동작하는가

- `WHERE id = ?`는 root부터 시작해 key 범위를 비교하며 leaf까지 내려갑니다.
- leaf에서 `id`를 찾으면 대응하는 `row offset`을 얻습니다.
- 이후 바이너리 파일에서 해당 위치의 학생 row만 직접 읽습니다.

예시 쿼리

```sql
SELECT name, major
FROM demo.students
WHERE id = 8;
```

```mermaid
%%{init: {
  "theme": "base",
  "themeVariables": {
    "background": "#0b1020",
    "primaryColor": "#111827",
    "primaryTextColor": "#f9fafb",
    "primaryBorderColor": "#f9fafb",
    "lineColor": "#e5e7eb",
    "secondaryColor": "#1f2937",
    "secondaryTextColor": "#f9fafb",
    "tertiaryColor": "#111827",
    "tertiaryTextColor": "#f9fafb"
  }
}}%%
flowchart LR
    A["Root<br/>4 | 8 | 11"] --> B["Internal<br/>9 | 10"]
    B --> C["Leaf<br/>8 | 9<br/>0x80 | 0x90"]
    C --> D["0x80"]
    D --> E["id=8, name=Lim, major=CS, grade=4"]

    classDef tree fill:#166534,stroke:#dcfce7,stroke-width:2px,color:#f9fafb;
    classDef data fill:#374151,stroke:#d1d5db,stroke-width:2px,color:#f9fafb;
    class A,B,C tree;
    class D,E data;
```

### 3-6. 범위 조회는 어떻게 동작하는가

- `WHERE id >= ?`, `<= ?`는 먼저 시작 leaf를 찾습니다.
- 이후 leaf chain을 따라가며 필요한 범위의 학생 row만 순서대로 읽습니다.
- 따라서 범위 조회는 매번 root부터 다시 시작하지 않고, leaf 간 연결을 활용합니다.

예시 쿼리

```sql
SELECT id, name, major
FROM demo.students
WHERE id >= 8;
```

```mermaid
%%{init: {
  "theme": "base",
  "themeVariables": {
    "background": "#0b1020",
    "primaryColor": "#111827",
    "primaryTextColor": "#f9fafb",
    "primaryBorderColor": "#f9fafb",
    "lineColor": "#e5e7eb",
    "secondaryColor": "#1f2937",
    "secondaryTextColor": "#f9fafb",
    "tertiaryColor": "#111827",
    "tertiaryTextColor": "#f9fafb"
  }
}}%%
flowchart LR
    S["Start at lower bound<br/>Leaf 8 | 9"] --> N1["Leaf 10"]
    N1 --> N2["Leaf 11 | 12"]
    N2 --> N3["Leaf 13 | 14 | 15"]

    classDef leaf fill:#22c55e,stroke:#dcfce7,stroke-width:2px,color:#052e16;
    class S,N1,N2,N3 leaf;
```

## 4. 시연

### 4-1. CLI 기능 시연

시연 순서
1. `INSERT`로 레코드 추가
2. `SELECT *`로 전체 데이터 확인
3. `WHERE id = ?`로 단건 인덱스 조회
4. `WHERE id >= ?` 또는 `WHERE id <= ?`로 범위 조회
5. `WHERE major = ?`로 비인덱스 조건 조회

예시 SQL

```sql
INSERT INTO demo.students (name, major, grade) VALUES ("Kim", "CS", "3");
INSERT INTO demo.students (name, major, grade) VALUES ("Lee", "Math", "2");

SELECT * FROM demo.students;
SELECT name, major FROM demo.students WHERE id = 1;
SELECT * FROM demo.students WHERE id >= 1;
SELECT * FROM demo.students WHERE major = "CS";
```

### 4-2. CLI 예외 처리

- 존재하지 않는 ID 조회
- 잘못된 조건식 입력
- 지원하지 않는 SQL 형식 입력

### 4-3. 100만 건 데이터 기반 성능 비교

- 데이터 수: `1,000,000`건 이상
- 비교 A: `WHERE id = ?` -> B+ Tree 인덱스 사용
- 비교 B: `WHERE major = ?` -> 선형 탐색 사용
- 인덱스 경로와 선형 탐색 경로의 실행 시간 비교

#### 측정 예시 결과

| 항목 | 실행 시간 | 접근 경로 |
| --- | ---: | --- |
| `WHERE id = ?` | 540 ms | B+ Tree Index |
| `WHERE major = ?` | 958 ms | Linear Scan |

```mermaid
%%{init: {
  "theme": "base",
  "themeVariables": {
    "background": "#0b1020",
    "primaryColor": "#111827",
    "primaryTextColor": "#f9fafb",
    "primaryBorderColor": "#f9fafb",
    "lineColor": "#e5e7eb",
    "secondaryColor": "#1f2937",
    "secondaryTextColor": "#f9fafb",
    "tertiaryColor": "#0f172a",
    "tertiaryTextColor": "#f9fafb"
  }
}}%%
xychart-beta
    title "SELECT Latency Comparison"
    x-axis ["WHERE id = ?", "WHERE major = ?"]
    y-axis "ms" 0 --> 1000
    bar [540, 958]
```

- 속도비: `WHERE id = ?` 경로가 약 `1.77x` 더 빠름

#### 해석 포인트

- `WHERE id = ?`는 row 위치를 직접 찾기 때문에 조회 비용이 작음
- `WHERE major = ?`는 전체 row 비교가 필요해 비용이 큼
- 동일한 SELECT라도 조건에 따라 실행 경로가 달라짐

## 5. 테스트

### 5-1. 단위 테스트

- B+ Tree 삽입 검증
- key 검색 검증
- 범위 검색 검증
- 노드 분할 이후 검색 정확성 검증
- 존재하지 않는 key 조회 검증

### 5-2. 기능 테스트

- `INSERT` 후 자동 ID 증가 검증
- `SELECT *` 결과 검증
- `WHERE id = ?` 동작 검증
- `WHERE id` 범위 조건 동작 검증
- `WHERE major = ?` 선형 탐색 동작 검증

### 5-3. 통합 관점 검증

- SQL 입력부터 파싱, 실행, 저장, 조회까지 전체 흐름 검증
- 바이너리 저장 구조 전환 이후 결과 일관성 검증
- 인덱스 경로와 비인덱스 경로의 분기 동작 검증

## 6. 소감

- 추후 작성 예정

## 7. 코드 레벨 핵심 구현

- 이 섹션은 발표 시간이 남을 때 실제 함수 이름 중심으로 설명하기 위한 후보 섹션입니다.
- 3번에서 구조를 설명한 뒤, 필요하면 아래 함수 흐름으로 내려가면 됩니다.

### 7-1. INSERT 경로 핵심 함수

- `execute_statement()`에서 `INSERT` 문을 `append_insert_row()`로 연결
- `next_id()`가 자동 ID를 생성
- `binary_writer_append_row()`가 바이너리 row를 append하고 `row offset`을 반환
- `index_insert()`가 `(id, row offset)`를 인덱스에 등록
- 실제 B+ Tree 삽입은 `bpt_insert_recursive()`가 수행

### 7-2. SELECT 경로 핵심 함수

- `execute_statement()`에서 `SELECT` 문을 `run_select_query()`로 연결
- `run_select_query()`가 `is_id_equality_predicate()` / `is_id_range_predicate()`로 분기
- 단건 ID 조회는 `run_select_by_id()` -> `index_find()` -> `bpt_find()` 경로 사용
- 범위 ID 조회는 `run_select_by_id_range()` -> `bpt_lower_bound()` 경로 사용
- 인덱스 조회 이후 실제 row 읽기는 `binary_reader_read_row_at()`가 수행
- 비인덱스 조건은 `run_select_linear()` -> `binary_reader_scan_all()` 경로 사용

### 7-3. B+ Tree 관련 핵심 함수

- `BptNode` 구조체가 internal / leaf 노드 형태를 함께 정의
- `bpt_insert_recursive()`가 leaf 삽입과 internal 삽입을 모두 처리
- leaf split 시 오른쪽 노드의 첫 key를 부모로 승격
- internal split 시 중간 key를 부모로 승격
- `bpt_find()`가 equality query 탐색을 담당
- `bpt_lower_bound()`가 range query 시작 leaf를 찾음
