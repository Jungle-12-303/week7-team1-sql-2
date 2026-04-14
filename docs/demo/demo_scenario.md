# 데모 시연 시나리오 (발표자용)

## 목적
이 시연은 다음 4가지를 눈으로 확인시키는 데 목적이 있습니다.
- INSERT 시 id 자동 부여
- WHERE id = ? 경로의 인덱스 조회
- 텍스트 `.data` -> 바이너리 `.data` 자동 전환
- 테스트/벤치 실행 가능 상태

## 시연 전 준비
- 터미널은 Docker가 가능한 환경에서 실행한다.
- 현재 폴더는 프로젝트 루트(`Week7`)여야 한다.

## 1단계: 이미지 빌드 + 내부 테스트 통과 확인
### 명령어
```bash
docker build -t week7-mini-sql .
```

### 화면에서 기대할 결과
- build 로그 중 `RUN make && make test` 단계가 성공한다.
- 최종적으로 이미지 빌드 성공 메시지가 나온다.

### 발표 멘트
- "도커 빌드 안에서 컴파일과 테스트를 같이 수행해서, 운영체제 차이 없이 동일하게 검증됩니다."

## 2단계: 기본 워크플로 시연 실행
### 명령어
```bash
docker run --rm week7-mini-sql
```

### 화면에서 기대할 결과
- `INSERT 1` 출력이 2번 나타난다.
- `SELECT *` 결과 테이블에 기존 데이터 + 신규 데이터가 함께 보인다.
- `WHERE id = 2` 조건 결과가 1건으로 출력된다.

### 발표 멘트
- "INSERT 시 id를 입력하지 않아도 자동으로 부여됩니다."
- "id 조건은 인덱스 경로로, 일반 조건은 선형 스캔 경로로 분기됩니다."

## 3단계: 마이그레이션 증거 확인
### 명령어
```bash
docker run --rm --entrypoint /bin/bash week7-mini-sql -lc \
"cp -r /app/examples /tmp/demo && \
 /app/build/mini_sql /tmp/demo/db /tmp/demo/sql/demo_workflow.sql && \
 ls -l /tmp/demo/db/demo && \
 od -An -tx1 -N 32 /tmp/demo/db/demo/students.data && \
 echo '--- backup ---' && cat /tmp/demo/db/demo/students.data.text.bak"
```

### 화면에서 기대할 결과
- `students.data`와 `students.data.text.bak`가 함께 보인다.
- `students.data`는 사람이 읽기 어려운 바이너리 바이트(hex)로 보인다.
- `students.data.text.bak`는 사람이 읽을 수 있는 텍스트 row로 보인다.

### 발표 멘트
- "초기 텍스트 데이터는 백업(`.text.bak`)으로 보존하고, 활성 데이터 파일은 바이너리로 전환합니다."
- "이 방식으로 기존 데이터 손실 위험을 줄이면서 저장 포맷을 바꿉니다."

## 4단계(선택): 성능 측정 가능성 언급
### 명령어 (긴 실행 시간 주의)
```bash
# 참고: 현재 benchmark.ps1은 PowerShell 스크립트이므로 로컬/CI 환경에서 실행
# .\scripts\benchmark.ps1
```

### 발표 멘트
- "벤치마크 스크립트로 100만 건 기준 인덱스 조회와 선형 탐색을 비교할 수 있습니다."
- "시연 시간에는 핵심 기능 검증 위주로 보여드리고, 벤치 로그는 별도 첨부 가능합니다."

## Q&A 빠른 답변
- 왜 id만 인덱스 최적화했나요?
  - 과제 핵심 요구사항을 우선 충족하기 위해 id equality 경로를 명시적으로 최적화했습니다.                                       
- 마이그레이션 실패 시 데이터는?
  - 기존 텍스트 파일을 백업으로 남기고 전환합니다.
