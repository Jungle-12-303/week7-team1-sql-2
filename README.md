# Week7 Mini SQL (Binary + Auto ID + Index Path)

이 프로젝트는 기존 텍스트 기반 `.data` 저장을 바이너리 포맷으로 전환하고,
`INSERT` 시 자동 ID를 부여한 뒤 `WHERE id = ?`를 인덱스 경로로 빠르게 처리하도록 확장한 버전입니다.

## 1. 목표와 범위
- `.data` 텍스트 저장 -> 바이너리 저장 전환
- `INSERT` 시 `id` 자동 부여
- 자동 부여된 `id -> row_ref(byte offset)` 인덱스 등록
- `WHERE id = ?` 인덱스 조회 분기
- 텍스트 데이터 자동 마이그레이션(일회성)
- 테스트/벤치/데모 문서 반영

## 2. 저장 포맷
바이너리 레코드 포맷(v1):
- `uint32 field_count`
- 각 필드: `uint32 byte_length + raw bytes(UTF-8)`

`row_ref`는 레코드 시작 바이트 오프셋입니다.

## 3. 실행 흐름
- INSERT: `auto id 생성 -> binary append -> index_insert`
- SELECT (`WHERE id = ?`): `index_find -> row_ref direct read`
- SELECT (그 외): 바이너리 파일 선형 스캔

## 4. 마이그레이션
실행 시 `.data`가 텍스트 포맷으로 감지되면:
1. `students.data.bin.tmp`에 바이너리 변환
2. 기존 텍스트를 `students.data.text.bak`로 백업
3. 바이너리 파일을 `students.data`로 교체

검증 스크립트:
- `scripts/verify_migration.ps1`

## 5. 테스트
단위 테스트(`tests/test_runner.c`)에서 다음을 검증합니다.
- 파서 INSERT/SELECT-WHERE 파싱
- `index_init/index_insert/index_find`
- 자동 ID 증가와 `WHERE id` 조회
- 일반 조건 선형 스캔(`WHERE major = ?`)
- 없는 id 조회 빈 결과
- 텍스트->바이너리 마이그레이션 후 조회 일치

실행:
```powershell
.\scripts\test.ps1
```

## 6. 벤치마크 (100만 건)
스크립트:
- `scripts/benchmark.ps1`

기본:
```powershell
.\scripts\benchmark.ps1
```

측정 항목:
- Case A: `WHERE id = ?` (인덱스)
- Case B: `WHERE major = ?` (선형 탐색)
- Case C: 텍스트 삽입 시뮬레이션 대비 바이너리 삽입 시간
- 평균, p95, speedup

## 7. 실행 방법
빌드:
```powershell
.\scripts\build.ps1
```

데모:
```powershell
.\scripts\demo.ps1
```

직접 실행:
```powershell
.\build\mini_sql.exe examples\db examples\sql\demo_workflow.sql
```

## 8. 4분 데모 스크립트 + Q&A
데모(4분):
- 0:00~0:40: 문제/목표(텍스트->바이너리, 자동ID, id 인덱스)
- 0:40~1:20: 저장 포맷과 row_ref 설명
- 1:20~2:20: INSERT + WHERE id + WHERE major 시연
- 2:20~3:20: 테스트/마이그레이션 검증
- 3:20~4:00: 벤치 결과 요약

Q&A(예상):
1. 왜 `id`만 인덱스 최적화했나?
- 과제 핵심 경로를 먼저 확실히 검증하기 위해서입니다.
2. 인덱스 실패 시 처리?
- 오류 반환 후 질의는 일반 선형 스캔 경로로 처리 가능합니다.
3. 텍스트 대비 바이너리 장점?
- 파싱 비용 감소, row_ref 기반 직접 접근, 인덱스 경로 성능 개선.
