id: PLAN-02
goal: 자동 ID 부여, B+ 트리 인덱스 등록, WHERE id 인덱스 조회의 기술 설계를 결정 완료 상태로 만든다.
depends_on:
  - PLAN-01
inputs:
  - 기존 SQL 처리기의 INSERT/SELECT 실행 경로
  - docs/assignment-guide.md
outputs:
  - 데이터 흐름 설계
  - API/인터페이스 초안
  - 실패/예외 처리 기준
constraints:
  - 기존 실행기 구조를 크게 깨지 않는다.
  - id 전용 인덱스 최적화에 집중한다.
  - 인덱스 미사용 경로(선형 탐색)는 유지한다.
done_when:
  - "INSERT와 SELECT(id)의 호출 흐름이 단계별로 명시돼 있다."
  - "필수 인터페이스(함수 수준)가 최소 5개 이상 정의돼 있다."
  - "오류 상황과 fallback 규칙이 정의돼 있다."
owner: agent:auto-implementer
status: todo

# 02-tech-design

## 핵심 데이터 흐름
1. `INSERT` 요청 수신
2. `next_id` 생성(자동 증가)
3. 레코드 저장(기존 저장 경로 사용)
4. `index_insert(id, row_ref)` 호출
5. 커밋 성공 응답

6. `SELECT ... WHERE id = ?` 요청 수신
7. 조건이 `id = 상수` 형태인지 판별
8. 참이면 `index_find(id)` 호출 후 row_ref로 즉시 조회
9. 거짓이면 기존 선형 탐색 경로 사용

## 인터페이스(초안)
- `uint64_t next_id(void);`
- `int index_init(void);`
- `int index_insert(uint64_t id, RowRef ref);`
- `int index_find(uint64_t id, RowRef* out_ref);`
- `bool is_id_equality_predicate(const Query* q);`
- `int run_select_by_id(uint64_t id, ResultSet* out);`
- `int run_select_linear(const Query* q, ResultSet* out);`

## 상태와 메타데이터
- `next_id`는 런타임 카운터로 관리한다.
- 인덱스 값은 `id -> row_ref(또는 row_offset)` 매핑으로 저장한다.
- id 중복은 허용하지 않는다(자동 생성 정책으로 충돌 방지).

## 실패/예외 처리
- 인덱스 초기화 실패: 시스템 시작 실패로 처리
- 인덱스 삽입 실패: 해당 INSERT를 실패 처리하고 오류 반환
- id 조건 파싱 실패: 인덱스 경로로 가지 않고 선형 탐색으로 fallback
- id 미존재: 정상적으로 빈 결과 반환
