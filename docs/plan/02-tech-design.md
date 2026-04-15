id: PLAN-02
goal: 바이너리 저장 전환, 자동 ID 부여, B+ 트리 인덱스 등록, WHERE id 인덱스 조회의 기술 설계를 결정 완료 상태로 만든다.
depends_on:
  - PLAN-01
inputs:
  - 기존 SQL 처리기의 INSERT/SELECT 실행 경로
  - docs/assignment-guide.md
outputs:
  - 데이터 흐름 설계
  - 바이너리 레코드 포맷 정의
  - API/인터페이스 초안
  - 실패/예외 처리 기준
constraints:
  - 기존 실행기 구조를 크게 깨지 않고 저장 계층 중심으로 변경한다.
  - id 전용 인덱스 최적화에 집중한다.
  - 인덱스 미사용 경로(선형 탐색)는 유지한다.
done_when:
  - "INSERT와 SELECT(id)의 호출 흐름이 바이너리 경로 기준으로 명시돼 있다."
  - "필수 인터페이스(함수 수준)가 최소 8개 이상 정의돼 있다."
  - "바이너리 포맷과 row_ref 규칙이 정의돼 있다."
  - "오류 상황과 fallback 규칙이 정의돼 있다."
owner: agent:auto-implementer
status: done
# 02-tech-design

## 핵심 데이터 흐름
1. `INSERT` 요청 수신
2. `next_id` 생성(자동 증가)
3. 레코드를 바이너리 포맷으로 직렬화
4. `.data` 바이너리 파일에 append 저장
5. 저장된 레코드의 `row_ref(byte offset)` 획득
6. `index_insert(id, row_ref)` 호출
7. 커밋 성공 응답

8. `SELECT ... WHERE id = ?` 요청 수신
9. 조건이 `id = 상수` 형태인지 판별
10. 참이면 `index_find(id)` 호출 후 row_ref로 바이너리 레코드 즉시 조회
11. 거짓이면 바이너리 파일 선형 스캔 경로 사용

## 바이너리 레코드 포맷(v1)
- 파일 단위: `.data`는 바이너리 파일로 관리
- 레코드 단위:
  - `uint32 field_count`
  - 각 필드마다 `uint32 byte_length + raw bytes`
- 문자열은 UTF-8 바이트 그대로 저장(텍스트 escape 불필요)
- `row_ref`는 레코드 시작 바이트 오프셋으로 정의

## 텍스트 -> 바이너리 마이그레이션
1. 기존 텍스트 `.data`를 한 줄씩 파싱
2. 각 row를 바이너리 레코드로 변환
3. 새 바이너리 파일에 순서대로 append
4. 변환된 row를 기준으로 `id -> row_ref` 인덱스 재구축
5. 검증 통과 후 바이너리 파일을 활성 파일로 전환

## 인터페이스(초안)
- `uint64_t next_id(void);`
- `int index_init(void);`
- `int index_insert(uint64_t id, RowRef ref);`
- `int index_find(uint64_t id, RowRef* out_ref);`
- `int binary_writer_append_row(const StringList* values, RowRef* out_ref);`
- `int binary_reader_read_row_at(RowRef ref, StringList* out_values);`
- `int binary_reader_scan_all(RowCallback cb, void* ctx);`
- `int migrate_text_data_to_binary(const char* text_path, const char* bin_path);`
- `bool is_id_equality_predicate(const Query* q);`
- `int run_select_by_id(uint64_t id, ResultSet* out);`
- `int run_select_linear(const Query* q, ResultSet* out);`

## 상태와 메타데이터
- `next_id`는 런타임 카운터로 관리한다.
- 인덱스 값은 `id -> row_ref(byte offset)` 매핑으로 저장한다.
- id 중복은 허용하지 않는다(자동 생성 정책으로 충돌 방지).

## 실패/예외 처리
- 인덱스 초기화 실패: 시스템 시작 실패로 처리
- 바이너리 파일 열기/쓰기 실패: 해당 INSERT 실패 처리 후 오류 반환
- 인덱스 삽입 실패: 해당 INSERT를 실패 처리하고 오류 반환
- id 조건 파싱 실패: 인덱스 경로로 가지 않고 선형 탐색으로 fallback
- id 미존재: 정상적으로 빈 결과 반환
- 마이그레이션 중 데이터 손상 감지: 전환 중단 및 기존 텍스트 파일 유지
