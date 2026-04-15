# B+ Tree Index Mini SQL

- ê¸°ì¡´ Mini SQL ì²˜ë¦¬ê¸°ì— `?ë™ ID`, `ë°”ì´?ˆë¦¬ ?€??, `ë©”ëª¨ë¦?ê¸°ë°˜ B+ Tree ?¸ë±??ë¥?ê²°í•©???„ë¡œ?íŠ¸
- `WHERE id = ?` ë°?`WHERE id` ë²”ìœ„ ì¡°ê±´???¸ë±??ê²½ë¡œë¡?ì²˜ë¦¬
- ë¹„ì¸?±ìŠ¤ ì¡°ê±´?€ ? í˜• ?ìƒ‰?¼ë¡œ ì²˜ë¦¬
- 1,000,000ê±??´ìƒ ?°ì´??ê¸°ì? ?±ëŠ¥ ë¹„êµ ?˜í–‰

## 1. ?œë¹„??

### 1-1. ??ì¤??¤ëª…

- `INSERT` ???ë™?¼ë¡œ IDë¥?ë¶€?¬í•˜ë©??´ë‹¹ IDë¥?B+ Tree ?¸ë±?¤ì— ?±ë¡??`WHERE id = ?` ì¡°íšŒë¥?ë¹ ë¥´ê²?ì²˜ë¦¬?˜ëŠ” Mini SQL ?”ì§„

### 1-2. ?„ë¡œ?íŠ¸ ëª©í‘œ

- ê¸°ì¡´ SQL ì²˜ë¦¬ê¸°ì˜ ? í˜• ?ìƒ‰ ê¸°ë°˜ ì¡°íšŒ êµ¬ì¡° ?•ì¥
- `WHERE id = ?` ì¡°ê±´?ì„œ ?¸ë±???¬ìš© ê°€?¥í•˜?„ë¡ ê°œì„ 
- ?€?©ëŸ‰ ?°ì´?°ì—???¸ë±??ì¡°íšŒ?€ ? í˜• ?ìƒ‰??ì°¨ì´ ê²€ì¦?
- ê¸°ì¡´ SQL ì²˜ë¦¬ê¸°ì? ?¸ë±??êµ¬ì¡°???ì—°?¤ëŸ¬???°ê²°

### 1-3. ì§€??ê¸°ëŠ¥

- `INSERT`
- `SELECT *`
- `SELECT ... WHERE id = ?`
- `SELECT ... WHERE id > ?`, `>= ?`, `< ?`, `<= ?`
- `SELECT ... WHERE major = ?` ??ë¹„ì¸?±ìŠ¤ ì¡°ê±´ ì¡°íšŒ
- CLI ê¸°ë°˜ SQL ?…ë ¥ ë°??¤í–‰
- ?€???°ì´???½ì… ë°??±ëŠ¥ ì¸¡ì •

### 1-4. ?°ì´???€??êµ¬ì¡°

- ë°”ì´?ˆë¦¬ row ?¬ë§· ?¬ìš©
- ê°?rowë¥??Œì¼ ??`row offset`?¼ë¡œ ì§ì ‘ ?‘ê·¼
- B+ Tree??`id -> row offset` ë§¤í•‘ ? ì?
- ?¸ë±??ì¡°íšŒ ???Œì¼ ?„ì²´ë¥??œíšŒ?˜ì? ?Šê³  row ?„ì¹˜ë¡?ì§ì ‘ ?´ë™

?¨ìˆœ B+ Tree êµ¬ì¡°

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

    R0 --> I1
    R0 --> I2
    R0 --> I3
    R0 --> I4

    classDef root fill:#166534,stroke:#dcfce7,stroke-width:3px,color:#f9fafb;
    classDef internal fill:#15803d,stroke:#dcfce7,stroke-width:2px,color:#f9fafb;
    linkStyle 0,1,2,3 stroke:#86efac,stroke-width:2.5px;
    class R0 root;
    class I1,I2,I3,I4 internal;
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
    A1["1 | 2"] --> A2["3"] --> B1["4 | 5"] --> B2["7"] --> C1["8 | 9"] --> C2["10"] --> D1["11 | 12"] --> D2["13 | 14 | 15"]

    classDef leaf fill:#22c55e,stroke:#dcfce7,stroke-width:2px,color:#052e16;
    linkStyle 0,1,2,3,4,5,6 stroke:#bbf7d0,stroke-width:2px;
    class A1,A2,B1,B2,C1,C2,D1,D2 leaf;
```

Leaf -> Binary Row ë§¤í•‘

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

## 2. ?Œì´?„ë¼??

### 2-1. ?„ì²´ ì²˜ë¦¬ ?ë¦„

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

### 2-2. INSERT ?Œì´?„ë¼??

- SQL ?…ë ¥
- Parser?ì„œ INSERT êµ¬ë¬¸ ?´ì„
- Executor?ì„œ ?¤ìŒ ID ?ì„±
- Storage??ë°”ì´?ˆë¦¬ row append
- append ê²°ê³¼ë¡?`row offset` ?ë“
- B+ Tree??`(id, row offset)` ?±ë¡

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

### 2-3. SELECT ?Œì´?„ë¼??

- `WHERE id = ?` ?ëŠ” `WHERE id` ë²”ìœ„ ì¡°ê±´?€ B+ Tree ?¸ë±??ê²½ë¡œ ?¬ìš©
- ?¸ë±??ê²½ë¡œ??B+ Tree?ì„œ row offset ?ìƒ‰ ??offset ê¸°ë°˜ direct read ?˜í–‰
- `WHERE major = ?` ê°™ì? ë¹„ì¸?±ìŠ¤ ì¡°ê±´?€ B+ Treeë¥?ê±°ì¹˜ì§€ ?ŠìŒ
- ë¹„ì¸?±ìŠ¤ ê²½ë¡œ???„ì²´ rowë¥?? í˜• ?ìƒ‰?˜ë©° ì¡°ê±´ ë¹„êµ ?˜í–‰

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

## 3. ?µì‹¬ êµ¬í˜„ ?´ìš©

- ë°œí‘œ ?œê°„??ì§§ì„ ê²½ìš° ?´ë¡  ?¤ëª… ì¤‘ì‹¬?¼ë¡œ ì§„í–‰
- ?œê°„???¨ì„ ê²½ìš° ì½”ë“œ ?ˆë²¨ ?¬ì¸?¸ê¹Œì§€ ?•ì¥ ?¤ëª…

### 3-1. INSERT ???ë™ ID ?ì„± ë°??¸ë±???±ë¡

#### ?´ë¡ 

- `INSERT` ?¤í–‰ ???¤ìŒ ID ?ë™ ?ì„±
- ?ì„±??IDë¥??¬í•¨??rowë¥?ë°”ì´?ˆë¦¬ ?¬ë§·?¼ë¡œ ?€??
- ?€??ì§í›„ row ?œì‘ ?„ì¹˜??`row offset` ?•ë³´
- `(id, row offset)`ë¥?B+ Tree??ì¦‰ì‹œ ?±ë¡

#### ì½”ë“œ ?ˆë²¨ ?¬ì¸??

- `execute_statement()`?ì„œ `INSERT` ë¬¸ì„ `append_insert_row()`ë¡??°ê²°
- `next_id()`ê°€ ?ë™ IDë¥??ì„±
- `binary_writer_append_row()`ê°€ ë°”ì´?ˆë¦¬ rowë¥?append?˜ê³  `row offset`??ë°˜í™˜
- `index_insert()`ê°€ `(id, row offset)`ë¥??¸ë±?¤ì— ?±ë¡
- ?¤ì œ B+ Tree ?½ì…?€ `bpt_insert_recursive()`ê°€ ?˜í–‰

### 3-2. ?¸ë±??ê²½ë¡œ?€ ? í˜• ?ìƒ‰ ê²½ë¡œ ë¶„ë¦¬

#### ?´ë¡ 

- `WHERE id = ?`??B+ Tree ?¸ë±???¬ìš©
- `WHERE id >= ?`, `<= ?` ??ë²”ìœ„ ì¡°ê±´?€ leaf ?œíšŒ ?¬ìš©
- `WHERE major = ?` ê°™ì? ì¡°ê±´?€ ? í˜• ?ìƒ‰ ?¬ìš©
- ì¡°ê±´ ì¢…ë¥˜???°ë¼ ?¤í–‰ ê²½ë¡œë¥?ë¶„ê¸°

#### ì½”ë“œ ?ˆë²¨ ?¬ì¸??

- `execute_statement()`?ì„œ `SELECT` ë¬¸ì„ `run_select_query()`ë¡??°ê²°
- `run_select_query()`ê°€ `is_id_equality_predicate()` / `is_id_range_predicate()`ë¡?ë¶„ê¸°
- ?¨ê±´ ID ì¡°íšŒ??`run_select_by_id()` -> `index_find()` -> `bpt_find()` ê²½ë¡œ ?¬ìš©
- ë²”ìœ„ ID ì¡°íšŒ??`run_select_by_id_range()` -> `bpt_lower_bound()` ê²½ë¡œ ?¬ìš©
- ?¸ë±??ì¡°íšŒ ?´í›„ ?¤ì œ row ?½ê¸°??`binary_reader_read_row_at()`ê°€ ?˜í–‰
- ë¹„ì¸?±ìŠ¤ ì¡°ê±´?€ `run_select_linear()` -> `binary_reader_scan_all()` ê²½ë¡œ ?¬ìš©

### 3-3. B+ Tree ?¸ë“œ êµ¬ì„± ë°©ì‹

#### ?´ë¡ 

- ?´ë? ?¸ë“œ??key?€ child pointer ë³´ìœ 
- ë¦¬í”„ ?¸ë“œ??key?€ value(`row offset`) ë³´ìœ 
- ë¦¬í”„ ?¸ë“œ ê°??°ê²°???µí•´ ë²”ìœ„ ì¡°íšŒ ì§€??
- ?¸ë“œê°€ ê°€??ì°¨ë©´ split ?˜í–‰
- split ê²°ê³¼ë¥?ë¶€ëª??¸ë“œ??ë°˜ì˜

#### ì»´í¬?ŒíŠ¸ ?¤ì´?´ê·¸??

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
flowchart TD
    A["CLI / SQL Input"] --> B["Parser"]
    B --> C["Executor"]
    C --> D["Storage Layer"]
    C --> E["B+ Tree Index"]
    D --> F["Binary .data File"]
    E --> G["id -> row offset"]

    classDef box fill:#111827,stroke:#f9fafb,stroke-width:2px,color:#f9fafb;
    classDef storage fill:#1f2937,stroke:#cbd5e1,stroke-width:2px,color:#f9fafb;
    classDef index fill:#1e3a8a,stroke:#bfdbfe,stroke-width:2px,color:#eff6ff;
    class A,B,C box;
    class D,F storage;
    class E,G index;
```

#### B+ Tree êµ¬ì¡° ?ˆì‹œ

```mermaid
flowchart TD
    R["Internal Node<br/>keys: 30, 70"]
    L1["Leaf<br/>1, 10, 20"]
    L2["Leaf<br/>30, 40, 60"]
    L3["Leaf<br/>70, 80, 90"]

    R --> L1
    R --> L2
    R --> L3
    L1 --> L2
    L2 --> L3
```

#### ì½”ë“œ ?ˆë²¨ ?¬ì¸??

- `BptNode` êµ¬ì¡°ì²´ê? internal / leaf ?¸ë“œ ?•íƒœë¥??¨ê»˜ ?•ì˜
- `bpt_insert_recursive()`ê°€ leaf ?½ì…ê³?internal ?½ì…??ëª¨ë‘ ì²˜ë¦¬
- leaf split ???¤ë¥¸ìª??¸ë“œ??ì²?keyë¥?ë¶€ëª¨ë¡œ ?¹ê²©
- internal split ??ì¤‘ê°„ keyë¥?ë¶€ëª¨ë¡œ ?¹ê²©
- `bpt_find()`ê°€ equality query ?ìƒ‰???´ë‹¹
- `bpt_lower_bound()`ê°€ range query ?œì‘ leafë¥?ì°¾ìŒ

## 4. ?œì—°

### 4-1. CLI ê¸°ëŠ¥ ?œì—°

?œì—° ?œì„œ
1. `INSERT`ë¡??ˆì½”??ì¶”ê?
2. `SELECT *`ë¡??„ì²´ ?°ì´???•ì¸
3. `WHERE id = ?`ë¡??¨ê±´ ?¸ë±??ì¡°íšŒ
4. `WHERE id >= ?` ?ëŠ” `WHERE id <= ?`ë¡?ë²”ìœ„ ì¡°íšŒ
5. `WHERE major = ?`ë¡?ë¹„ì¸?±ìŠ¤ ì¡°ê±´ ì¡°íšŒ

?ˆì‹œ SQL

```sql
INSERT INTO demo.students (name, major, grade) VALUES ("Kim", "CS", "3");
INSERT INTO demo.students (name, major, grade) VALUES ("Lee", "Math", "2");

SELECT * FROM demo.students;
SELECT name, major FROM demo.students WHERE id = 1;
SELECT * FROM demo.students WHERE id >= 1;
SELECT * FROM demo.students WHERE major = "CS";
```

### 4-2. CLI ?ˆì™¸ ì²˜ë¦¬

- ì¡´ì¬?˜ì? ?ŠëŠ” ID ì¡°íšŒ
- ?˜ëª»??ì¡°ê±´???…ë ¥
- ì§€?í•˜ì§€ ?ŠëŠ” SQL ?•ì‹ ?…ë ¥

### 4-3. 100ë§?ê±??°ì´??ê¸°ë°˜ ?±ëŠ¥ ë¹„êµ

- ?°ì´???? `1,000,000`ê±??´ìƒ
- ë¹„êµ A: `WHERE id = ?` -> B+ Tree ?¸ë±???¬ìš©
- ë¹„êµ B: `WHERE major = ?` -> ? í˜• ?ìƒ‰ ?¬ìš©
- ?¸ë±??ê²½ë¡œ?€ ? í˜• ?ìƒ‰ ê²½ë¡œ???¤í–‰ ?œê°„ ë¹„êµ

#### ì¸¡ì • ?ˆì‹œ ê²°ê³¼

| ??ª© | ?¤í–‰ ?œê°„ | ?‘ê·¼ ê²½ë¡œ |
| --- | ---: | --- |
| `WHERE id = ?` | 540 ms | B+ Tree Index |
| `WHERE major = ?` | 958 ms | Linear Scan |

#### ?´ì„ ?¬ì¸??

- `WHERE id = ?`??row ?„ì¹˜ë¥?ì§ì ‘ ì°¾ê¸° ?Œë¬¸??ì¡°íšŒ ë¹„ìš©???‘ìŒ
- `WHERE major = ?`???„ì²´ row ë¹„êµê°€ ?„ìš”??ë¹„ìš©????
- ?™ì¼??SELECT?¼ë„ ì¡°ê±´???°ë¼ ?¤í–‰ ê²½ë¡œê°€ ?¬ë¼ì§?

## 5. ?ŒìŠ¤??

### 5-1. ?¨ìœ„ ?ŒìŠ¤??

- B+ Tree ?½ì… ê²€ì¦?
- key ê²€??ê²€ì¦?
- ë²”ìœ„ ê²€??ê²€ì¦?
- ?¸ë“œ ë¶„í•  ?´í›„ ê²€???•í™•??ê²€ì¦?
- ì¡´ì¬?˜ì? ?ŠëŠ” key ì¡°íšŒ ê²€ì¦?

### 5-2. ê¸°ëŠ¥ ?ŒìŠ¤??

- `INSERT` ???ë™ ID ì¦ê? ê²€ì¦?
- `SELECT *` ê²°ê³¼ ê²€ì¦?
- `WHERE id = ?` ?™ì‘ ê²€ì¦?
- `WHERE id` ë²”ìœ„ ì¡°ê±´ ?™ì‘ ê²€ì¦?
- `WHERE major = ?` ? í˜• ?ìƒ‰ ?™ì‘ ê²€ì¦?

### 5-3. ?µí•© ê´€??ê²€ì¦?

- SQL ?…ë ¥ë¶€???Œì‹±, ?¤í–‰, ?€?? ì¡°íšŒê¹Œì? ?„ì²´ ?ë¦„ ê²€ì¦?
- ë°”ì´?ˆë¦¬ ?€??êµ¬ì¡° ?„í™˜ ?´í›„ ê²°ê³¼ ?¼ê???ê²€ì¦?
- ?¸ë±??ê²½ë¡œ?€ ë¹„ì¸?±ìŠ¤ ê²½ë¡œ??ë¶„ê¸° ?™ì‘ ê²€ì¦?

## 6. ?Œê°

- ì¶”í›„ ?‘ì„± ?ˆì •
