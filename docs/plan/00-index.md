id: PLAN-00
goal: docs/plan 문서만으로 자동 구현기가 읽기/실행 순서와 완료 판정을 안정적으로 수행한다.
depends_on: []
inputs:
  - docs/assignment-guide.md
  - docs/base-project-evaluation.md
  - docs/plan/*.md
outputs:
  - 실행 순서가 고정된 계획 문서 집합
  - 단계별 작업 카드(tasks/*)
  - 텍스트 기반 저장소에서 바이너리 저장소로 전환된 구현 계획
constraints:
  - 구현 언어는 C로 제한한다.
  - 범위는 B+ 트리 인덱스 연동 과제 요구사항을 벗어나지 않는다.
  - 모든 계획 문서는 동일한 헤더 키를 사용한다.
done_when:
  - "자동 구현기가 00~05 문서를 순서대로 읽고 실행할 수 있다."
  - "각 문서의 depends_on이 유효하고 순환 의존이 없다."
  - "각 문서의 done_when 항목이 참/거짓으로 판정 가능하다."
owner: agent:auto-implementer
status: done
# 00-index

## 문서 읽기 순서
1. `00-index.md`
2. `01-scope-constraints.md`
3. `02-tech-design.md`
4. `03-implementation-steps.md`
5. `04-test-benchmark.md`
6. `05-readme-demo.md`

## 실행 순서
1. 범위와 제약 고정 (`01`)
2. 기술 설계 확정 (`02`)
3. 구현 단계 실행 (`03`)
4. 테스트와 벤치마크 실행 (`04`)
5. README/데모 반영 (`05`)

## 작업 카드 매핑
- `tasks/T01.md`: 범위/제약 확정
- `tasks/T02.md`: 바이너리 저장 구조 + 자동 ID/인덱스 설계
- `tasks/T03.md`: 바이너리 INSERT 경로 + 인덱스 등록 연동
- `tasks/T04.md`: WHERE id 인덱스 조회 분기
- `tasks/T05.md`: 테스트/벤치마크 구현
- `tasks/T06.md`: README/데모 업데이트
- `tasks/T07.md`: 텍스트 데이터 -> 바이너리 데이터 마이그레이션 검증
- `tasks/T08.md`: WHERE id 범위 조회(`>`, `>=`, `<`, `<=`) 지원

## 자동 실행 규칙
- 규칙 1: `depends_on`이 완료된 문서/카드만 실행한다.
- 규칙 2: 각 단계 종료 시 `done_when`을 체크하고 통과한 경우에만 다음 단계로 진행한다.
- 규칙 3: 실패 시 원인과 재시도 조건을 해당 문서의 체크 항목과 함께 기록한다.
