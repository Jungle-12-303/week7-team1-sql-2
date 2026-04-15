# Delta 운영 가이드

이 폴더는 초기 계획(00~07)을 수정하지 않고, 사후 변경사항만 누적 기록하기 위한 공간입니다.

## 원칙
- 기준선(Baseline): `docs/plan/00~07`은 원본으로 유지
- 변경 기록: 신규/변경 요구는 반드시 `deltas/*.md`로 추가
- 추적성: 각 Delta는 관련 Task/Plan과 날짜를 명시

## 파일 네이밍
- `YYYY-MM-DD-DNN-<slug>.md`
- 예: `2026-04-15-D01-cli-history.md`

## 최소 템플릿
- id
- date
- type (feature|bugfix|scope-change|doc)
- baseline_refs
- reason
- impact
- tasks
- status

## 상태 정의
- proposed: 제안됨
- accepted: 반영 승인
- implemented: 구현 완료
- verified: 검증 완료
- archived: 종료
