id: PLAN-06
goal: mini_sql CLI 사용성을 개선하기 위한 UX 중심 설계와 구현/검증 계획을 정의한다.
depends_on:
  - PLAN-03
inputs:
  - src/main.c
  - docs/plan/03-implementation-steps.md
outputs:
  - 인자 파싱 UX 개선 스펙
  - 인터랙티브 모드 오류 복구(재입력 유도) 정책
  - 테스트/데모/문서 반영 체크리스트
constraints:
  - 기존 동작(`mini_sql <db_root> <sql_file>`)은 하위호환 유지
  - 잘못된 입력이 있어도 가능한 한 프로세스를 즉시 종료하지 않고 복구 경로 제공
  - 구현 복잡도를 과도하게 올리지 않고 C 표준 라이브러리 기반으로 유지
done_when:
  - "--help/-h, short option(-d/-f/-i), 유연한 인자 순서 파싱 요구사항이 문서화되어 있다."
  - "인터랙티브 모드에서 오타/지원하지 않는 명령 처리 정책(에러 출력 후 재입력)이 명시되어 있다."
  - "CLI UX 관련 task 카드(T09)가 생성되어 있고 수용 기준이 명확하다."
owner: agent:auto-implementer
status: todo

# 06-cli-ux

## 1) 문제 정의
- 현재 CLI는 기본 실행은 가능하지만, 인자 파싱이 하드코딩 분기에 의존해 사용성이 낮다.
- `--help/-h`, short option, 옵션 순서 유연성 등 일반적인 CLI 기대치가 부족하다.
- 인터랙티브 모드에서 SQL 오타나 미지원 구문 입력 시, "종료" 대신 "에러 안내 + 다음 입력 대기"가 UX적으로 더 적합하다.

## 2) 목표 UX
- `mini_sql --help` 또는 `mini_sql -h`로 즉시 사용법/예시/종료코드 안내.
- `mini_sql -d <db_root> -f <sql_file>`, `mini_sql -d <db_root> -i` 지원.
- 옵션 순서 무관 파싱 지원: 예) `mini_sql -f a.sql -d examples/db`.
- 오류가 나도 사용자가 다시 시도하기 쉽도록 에러 메시지에 "무엇을 다시 입력하면 되는지"를 포함.

## 3) 인자 파싱 설계
- 지원 옵션
- `--db <path>`, `-d <path>`
- `--file <path>`, `-f <path>`
- `--interactive`, `-i`
- `--help`, `-h`
- 호환 모드
- positional 2개: `<db_root> <sql_file>`
- positional 1개: `<db_root>` + interactive
- 충돌 규칙
- `--file`와 `--interactive`를 동시에 주면 에러
- `--db`와 positional db_root 동시 제공 시 에러
- 알 수 없는 옵션/명령은 에러로 안내하고 usage 출력 후 종료코드 2 반환

## 4) 인터랙티브 오류 복구 정책
- parse/execute 오류는 stderr로 안내 후 세션 유지.
- 세션 종료는 사용자가 명시적으로 `exit`/`quit` 또는 EOF를 입력한 경우에만 수행.
- 입력 실수 대응 메시지 표준화
- 구문 오류: `error: <detail>. Please re-enter a valid SQL statement ending with ';'`
- 미지원 구문: `error: unsupported statement. Try INSERT or SELECT`
- 빈 입력은 무시하고 프롬프트 재표시.

## 5) UX 세부 사항
- 프롬프트 상태
- 새 문장 시작: `mini_sql> `
- 여러 줄 이어쓰기: `...> `
- 마지막 입력에 세미콜론이 없으면 실행하지 않고 계속 입력 대기.
- 인터랙티브에서 `help` 입력 시 간단 도움말 출력(옵션).

## 6) 종료 코드 정책
- `0`: 정상 실행
- `1`: 실행 중 파일/스토리지/런타임 오류
- `2`: 잘못된 CLI 인자(usage error)

## 7) 검증 계획
- 인자 조합 테스트
- `--help/-h`, short option, 옵션 순서 변경, 충돌 옵션 검증
- 인터랙티브 UX 테스트
- 오타 SQL 입력 후 세션 유지 확인
- 미지원 구문 입력 후 세션 유지 + 안내 메시지 확인
- EOF/exit/quit 종료 동작 확인

## 8) 문서/데모 반영
- README 실행 예시에 short/long option 모두 추가
- demo 문서에 "오타 입력 -> 에러 안내 -> 재입력 성공" 시나리오 추가
