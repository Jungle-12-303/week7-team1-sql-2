id: D03
date: 2026-04-16
type: scope-change
baseline_refs:
  - PLAN-01
  - PLAN-02
  - PLAN-03
  - PLAN-04
  - D02
reason: 학번(student_no) 스키마를 도입하고 절대 중복 금지를 보장해야 하며, 100만 건 벤치마크에서 조회 대상을 동일하게 고정해 비교 신뢰도를 높이기 위함
impact:
  - 스키마 변경: students에 student_no 컬럼 추가
  - 제약 추가: student_no UNIQUE(중복 INSERT 즉시 실패)
  - 저장/검증 경로 확장: student_no 중복 검증 경로 추가
  - 조회 경로 정책: student_no 조건은 항상 선형 탐색(비인덱스)으로 강제
  - 벤치마크 정책 변경: 랜덤 조회 제거, 고정 타깃 조회로 통일
tasks:
  - TASK-T11
  - TASK-T05
status: implemented

# D03 Student Number Unique + Deterministic Benchmark

## 변경 요약
1. `demo.students`에 `student_no` 컬럼을 추가한다.
2. `student_no`는 절대 중복되면 안 되므로 UNIQUE 제약을 강제한다.
3. 100만 건 벤치마크 스크립트는 조회 대상을 랜덤으로 뽑지 않고, 같은 타깃 row를 반복 조회하도록 바꾼다.
4. 인덱스 경로와 선형 경로 비교 시에도 같은 row를 조회하도록 쿼리 페어를 고정한다.

## 스키마/제약 규칙
- 기준 스키마(예시): `id|student_no|name|major|grade`
- `id`: 자동 증가, 내부 기본 키
- `student_no`: 외부 업무 키, UNIQUE, NOT EMPTY
- 조회 정책:
  - `id` 조건만 B+Tree 인덱스 사용
  - `student_no` 조건은 절대 B+Tree를 사용하지 않고 항상 선형 탐색
- 중복 정책:
  - INSERT 시 `student_no`가 이미 존재하면 해당 INSERT는 실패
  - 실패 시 데이터 파일/인덱스에 부분 반영이 없어야 함(원자성 유지)

## 벤치마크 스크립트 델타 요구사항
1. 데이터 생성 시 `student_no`를 결정론적으로 생성한다.
   - 예: `student_no = 2026000001 + i` 형태
2. 조회 타깃을 고정한다.
   - 예: `target_row = floor(Rows / 2)` (중간값 고정)
3. 비교 쿼리를 동일 대상 기준으로 만든다.
   - 인덱스 경로: `WHERE id = target_id`
   - 선형 경로 A(고정): 같은 row를 찾는 `WHERE student_no = target_student_no`
   - 선형 경로 B(보조): 같은 row를 찾는 `WHERE name = target_name`
4. 반복 실행(`Runs`) 동안 타깃을 바꾸지 않는다.
5. 결과 리포트에 타깃 식별자(`target_id`, `target_student_no`, `target_name`)를 명시한다.

## 수용 기준
1. 중복 `student_no` INSERT 시 명확한 에러 메시지와 함께 실패한다.
2. 중복 실패 후 row 수와 인덱스 엔트리 수가 변하지 않는다.
3. 벤치마크에서 각 run의 조회 대상이 동일함이 로그/출력으로 검증된다.
4. 100만 건 기준 성능 결과에 동일 타깃 비교임이 명시된다.
5. `WHERE student_no = ?` 실행 시 B+Tree 경로를 타지 않고 선형 탐색 경로를 사용함이 로그/테스트로 검증된다.

## 구현 메모
- `scripts/benchmark.ps1`:
  - 랜덤 `targetId` 선택 로직 제거
  - 고정 타깃 계산 및 고정 SQL 생성으로 변경
  - `WHERE id = target_id` vs `WHERE student_no = target_student_no`를 동일 타깃으로 반복 측정
  - 출력에 고정 타깃 정보 및 student_no 선형탐색 강제 여부 확인 로그 추가
- `scripts/test.ps1`:
  - `student_no` 포함 스키마로 갱신
  - 중복 `student_no` INSERT 실패 테스트 추가
  - `WHERE student_no = ?`가 선형 탐색 경로를 사용함을 검증하는 테스트 추가
- storage/executor:
  - INSERT 전에 student_no 중복 검증 수행
  - 검증 성공 시에만 바이너리 append + 인덱스(`id`) 업데이트
  - 조건 분기에서 student_no는 인덱스 분기 대상에서 명시적으로 제외
