id: D02
date: 2026-04-15
type: feature
baseline_refs:
  - PLAN-06
  - PLAN-07
  - D01
reason: INSERT 결과 피드백 강화 및 중복 키 입력 시 사용자 안내 개선
impact:
  - storage/executor 인터페이스 확장(INSERT 결과 echo 데이터 전달)
  - 사용자 메시지 정책 강화(duplicate key 안내)
  - 출력 포맷 변경(INSERT 결과 하단 row 데이터 표시)
tasks:
  - TASK-T10
status: implemented

# D02 Insert Feedback & Duplicate Key Guide

## 변경 요약
1. 중복된 키 입력(duplicate key) 상황에서 사용자에게 "이미 입력된 값" 안내를 제공한다.
2. `INSERT 1` 출력 직후 실제 입력된 row 데이터를 컬럼-값 형태로 함께 보여준다.

## 수용 기준
1. duplicate key 에러 발생 시 한국어 힌트에 중복 입력 안내가 포함된다.
2. INSERT 성공 시 `입력된 row:` 블록이 출력되고 컬럼별 값이 표시된다.
3. 기존 SELECT/에러 처리 동작은 유지된다.

## 구현 메모
- `append_insert_row`가 INSERT 결과 echo 데이터(StringList)를 executor로 반환
- `print_execution_result`에서 INSERT 결과 상세 출력
- `error_hint_ko`에 duplicate key 매핑 추가
