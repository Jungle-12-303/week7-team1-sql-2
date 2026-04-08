# wk06-mini-sql

간단한 파일 기반 mini SQL 실행기입니다. SQL 스크립트 파일을 읽어 `INSERT`/`SELECT`를 처리하고, 결과를 `.schema`/`.data` 파일로 저장합니다.

## 지원 기능

- `INSERT INTO [schema.]table (col1, col2, ...) VALUES (...)`
- `SELECT * FROM [schema.]table`
- `SELECT col1, col2 FROM [schema.]table`
- `WHERE column = value` (단일 조건만 지원)
- SQL 파일 주석
  - `-- line comment`
  - `/* block comment */`

예시:

```sql
INSERT INTO demo.students (id, name, major) VALUES (1, 'Alice', 'DB');
SELECT * FROM demo.students;
SELECT id, name FROM demo.students WHERE id = 1;
```

---

## 실행 파이프라인
```mermaid
flowchart LR
    A["SQL 파일"] --> B["Parser+Tokenizer (src/parser.c)"]
    B --> C["Executor (src/executor.c)"]
    C --> D["Storage (src/storage.c)"]
    D --> E["파일 시스템 (.schema / .data)"]
    C --> F["CLI 출력"]
```

상세 흐름:

```mermaid
sequenceDiagram
    participant U as "사용자"
    participant CLI as "mini_sql"
    participant P as "Parser (Tokenizer+Parser)"
    participant X as "Executor"
    participant S as "Storage"
    participant F as "Files"

    U->>CLI: SQL 파일 실행 요청
    CLI->>P: SQL 텍스트 전달
    P->>P: 토큰화 + AST 유사 구조 생성
    P-->>CLI: SQLScript(구문 트리)
    CLI->>X: statement 실행
    X->>S: INSERT / SELECT 처리
    alt INSERT
        S->>F: .data append
        F-->>S: 성공/실패
    else SELECT
        S->>F: .data, .schema read
        F-->>S: 행 목록
    end
    X-->>CLI: ExecutionResult
    CLI-->>U: 출력
```

---

## 모듈 구조(현재)

```mermaid
flowchart TB
  subgraph CLI
    MAIN["src/main.c"]
  end

  subgraph Parse
    PARSER["src/parser.c"]
  end

  subgraph Execute
    EXEC["src/executor.c"]
    STORAGE["src/storage.c"]
  end

  subgraph Shared
    COMMON["src/common.c"]
  end

  MAIN --> PARSER
  PARSER --> EXEC
  EXEC --> STORAGE
  STORAGE --> COMMON
  PARSER --> COMMON
```

핵심 파일:

- `src/main.c`
  - CLI 진입점, 인자 파싱, SQL 파일 로드, 실행 결과 출력.
- `src/parser.c`
  - tokenizer 코드 + 파서 코드 통합.
  - `parse_sql_script()`가 문자열을 토큰화하고 문장 단위 구조(내부 Statement/SQLScript 형태)로 변환.
- `src/executor.c`
  - 파싱된 statement를 실행 타입별로 라우팅.
  - INSERT/SELECT 실행 결과를 `ExecutionResult`로 정리.
- `src/storage.c`
  - `.schema` / `.data` 파일 읽기/쓰기와 조회 로직.
  - 파일 경로 생성, 데이터 검색(기본 WHERE) 처리.
- `src/common.c`
  - 문자열 리스트, 파일 입출력, 경로 유틸(부모 디렉터리 생성) 등 공통 유틸리티.


---

## INSERT / SELECT 로직

파일 기반 DB이기 때문에 `INSERT`와 `SELECT`는 모두 `.schema`와 `.data` 파일을 기준으로 동작합니다.  
핵심은 `INSERT`는 "스키마 순서에 맞춰 한 줄을 추가"하는 과정이고, `SELECT`는 "스키마를 기준으로 컬럼 위치를 해석한 뒤 `.data`를 순회"하는 과정입니다.

### INSERT

```mermaid
%%{init: {'themeVariables': {'fontSize': '20px'}}}%%
flowchart LR
    A["INSERT SQL 입력"] --> B["InsertStatement 생성"]
    B --> C["테이블 경로 계산"]
    C --> D[".schema 로드"]
    D --> E["컬럼명-스키마 매핑"]
    E --> F["값 순서를 스키마 기준으로 재배치"]
    F --> G["escape 후 한 줄로 직렬화"]
    G --> H[".data 끝에 append"]
```

INSERT 동작 단계:

1. `parser.c`가 `INSERT INTO ... VALUES ...`를 `InsertStatement`로 파싱합니다.
2. `storage.c`가 대상 테이블의 `.schema` / `.data` 경로를 계산합니다.
3. `.schema`를 읽어 실제 테이블 컬럼 순서를 확인합니다.
4. INSERT에 들어온 컬럼명을 스키마 컬럼과 매핑합니다.
5. 값을 스키마 순서대로 다시 정렬하고, 빠진 컬럼은 빈 문자열로 채웁니다.
6. `|`, `\`, 개행 같은 문자를 escape해서 한 줄 텍스트로 직렬화합니다.
7. 완성된 row를 `.data` 파일 끝에 추가합니다.

### SELECT

```mermaid
%%{init: {'themeVariables': {'fontSize': '20px'}}}%%
flowchart LR
    A["SELECT SQL 입력"] --> B["SelectStatement 생성"]
    B --> C["테이블 경로 계산"]
    C --> D[".schema 로드"]
    D --> E["조회 컬럼 / WHERE 인덱스 결정"]
    E --> F[".data를 한 줄씩 읽기"]
    F --> G["row를 컬럼 값으로 복원"]
    G --> H{"WHERE 만족?"}
    H -- "아니오" --> F
    H -- "예" --> I["필요한 컬럼만 projection"]
    I --> J["QueryResult에 행 추가"]
    J --> F
    F --> K["표 형태로 출력"]
```

SELECT 동작 단계:

1. `parser.c`가 `SELECT ... FROM ... WHERE ...`를 `SelectStatement`로 파싱합니다.
2. `storage.c`가 `.schema`를 읽어 전체 컬럼 목록을 확보합니다.
3. `SELECT *`인지, 특정 컬럼만 조회하는지에 따라 projection 인덱스를 준비합니다.
4. `WHERE column = value`가 있으면 비교할 컬럼 인덱스를 먼저 찾습니다.
5. `.data` 파일을 한 줄씩 읽어 각 row를 다시 컬럼 값 목록으로 복원합니다.
6. WHERE 조건을 통과한 row만 선택합니다.
7. 필요한 컬럼만 뽑아 `QueryResult`에 누적합니다.
8. 마지막에 `executor.c`가 결과를 테이블 형식으로 출력합니다.

---

## 파일 기반 DB 레이아웃

예시 DB 루트:

```text
db_root/
  demo/
    students.schema
    students.data
```

`schema`만 바로 쓰는 경우:

```text
db_root/
  students.schema
  students.data
```

### `.schema`
컬럼은 `|` 구분 텍스트

```text
id|name|major|grade
```

### `.data`
한 줄이 한 row이며, `|` 구분 텍스트.

```text
1|Alice|Database|A
2|Bob|AI|B
```

escape 규칙:

- `|` → `\|`
- `\` → `\\`
- 개행(`\n`) / 캐리지(`\r`)을 문자열에 저장할 때 이스케이프

---

## 빌드/실행/테스트

### 빌드

```powershell
.\scripts\build.ps1
```

출력:
- `build\mini_sql.exe`
- `build\test_runner.exe`

### 실행(데모)

```powershell
.\scripts\demo.ps1
```

직접 실행:

```powershell
.\build\mini_sql.exe examples\db examples\sql\demo_workflow.sql
```

또는

```powershell
.\build\mini_sql.exe --db examples\db --file examples\sql\demo_workflow.sql
```

### 테스트

```powershell
.\scripts\test.ps1
```

실행 대상:
- `tests\test_runner.c` 정적 테스트
- `scripts\test.ps1` 통합 테스트

---

## 프로젝트 트리

```text
.
+ include/
  - common.h
  - executor.h
  - parser.h    
  - storage.h
+ src/
  - common.c
  - executor.c
  - parser.c
  - storage.c
  - main.c
+ tests/
  - test_runner.c
+ scripts/
  - build.ps1
  - demo.ps1
  - test.ps1
+ examples/
  - db/
  - sql/
```

---
