# 페이지, 페이지 테이블, 페이지 폴트와 B+ Tree

이 문서는 B+ Tree를 공부하다가 함께 등장할 수 있는 `page`, `page table`, `page fault` 같은 개념을 정리합니다.

결론부터 말하면, **페이지 테이블과 페이지 폴트는 운영체제의 메모리 관리 개념**이고, **B+ Tree의 page는 DB나 파일 시스템에서 데이터를 일정한 크기 단위로 나누어 관리하는 개념**입니다.
둘 다 `page`라는 말을 쓰지만, 같은 계층의 개념은 아닙니다.

## 1. page란

`page`는 큰 데이터를 일정한 크기의 조각으로 나눈 단위입니다.

운영체제에서는 메모리를 page 단위로 관리합니다.

```text
프로그램이 보는 가상 메모리
-> page 단위로 나뉨
-> 실제 물리 메모리 frame과 연결됨
```

DB나 파일 시스템에서도 데이터를 page 단위로 관리할 수 있습니다.

```text
테이블 파일 또는 인덱스 파일
-> page 단위로 나뉨
-> 한 page 안에 여러 row 또는 여러 key 저장
```

즉 `page`는 공통적으로 **큰 저장 공간을 일정한 크기로 나눈 조각**이라고 이해하면 됩니다.

## 2. 운영체제의 page table

운영체제에서 `page table`은 가상 주소와 물리 주소를 연결하는 표입니다.

프로그램은 실제 물리 메모리 주소를 직접 쓰지 않고, 가상 주소를 사용합니다.
운영체제와 CPU는 page table을 이용해 이 가상 주소가 실제 물리 메모리 어디에 있는지 찾습니다.

```text
가상 주소
-> page number 확인
-> page table 조회
-> 물리 메모리 frame 위치 확인
```

예를 들어 아래처럼 볼 수 있습니다.

```text
Virtual page 3 -> Physical frame 10
Virtual page 4 -> Physical frame 2
Virtual page 5 -> 아직 메모리에 없음
```

여기서 page table은 B+ Tree의 internal node처럼 검색 경로를 안내하는 구조가 아닙니다.
운영체제가 메모리 주소 변환을 하기 위한 표입니다.

## 3. page fault란

`page fault`는 프로그램이 접근하려는 page가 현재 물리 메모리에 없을 때 발생합니다.

예를 들어 프로그램이 어떤 가상 주소에 접근했는데, page table을 확인해보니 해당 page가 메모리에 올라와 있지 않을 수 있습니다.

```text
프로그램이 page 5 접근
-> page table 확인
-> page 5가 물리 메모리에 없음
-> page fault 발생
```

그러면 운영체제는 디스크나 swap 영역에서 필요한 page를 읽어와 물리 메모리에 올립니다.
그 후 page table을 갱신하고, 프로그램 실행을 이어갑니다.

```text
page fault
-> 필요한 page를 디스크에서 읽음
-> 물리 메모리에 올림
-> page table 갱신
-> 원래 명령 재시도
```

즉 page fault는 오류라기보다는, **필요한 page가 아직 메모리에 없어서 운영체제가 가져오는 사건**입니다.

## 4. DB에서 말하는 page

DB에서 말하는 page는 보통 디스크 파일을 읽고 쓰는 단위입니다.

DB는 row나 index key를 하나씩 디스크에서 읽기보다, 여러 데이터를 묶은 page 단위로 읽는 경우가 많습니다.

```text
DB 파일
-> page 0
-> page 1
-> page 2
...
```

각 page 안에는 여러 row나 여러 index entry가 들어갈 수 있습니다.

```text
data page:
row, row, row ...

index page:
key, key, child pointer ...
```

B+ Tree를 DB에서 구현할 때는 보통 node 하나를 page 하나에 대응시키는 방식으로 생각할 수 있습니다.

```text
B+ Tree internal node = index page
B+ Tree leaf node     = leaf page
```

## 5. B+ Tree와 page가 연결되는 이유

B+ Tree는 한 node에 여러 key를 저장합니다.
이 구조는 디스크 page와 잘 맞습니다.

디스크에서 데이터를 읽을 때는 작은 값 하나만 읽는 것보다, page 단위로 한 번에 읽는 것이 일반적입니다.
그래서 B+ Tree node 하나를 page 크기에 맞춰 설계하면, 한 번의 디스크 읽기로 여러 key를 확인할 수 있습니다.

예를 들어 internal page 하나에 key가 여러 개 들어 있으면:

```text
[10 | 20 | 30 | 40 | 50]
```

이 page 하나를 읽는 것만으로 어느 child page로 내려갈지 판단할 수 있습니다.

```text
id = 35
-> 30 이상 40 미만
-> 해당 child page로 이동
```

이런 방식 덕분에 B+ Tree는 트리 높이를 낮게 유지하고, 디스크 접근 횟수를 줄이는 데 유리합니다.

## 6. page table과 B+ Tree는 어떻게 다른가

두 개념은 둘 다 `page`라는 단어와 연결되지만 목적이 다릅니다.

| 구분 | page table | B+ Tree page |
| --- | --- | --- |
| 영역 | 운영체제 메모리 관리 | DB/파일 인덱스 관리 |
| 목적 | 가상 주소를 물리 주소로 변환 | key로 row 위치나 child page를 찾기 |
| 관리 대상 | 메모리 page | 데이터 page, index page |
| 주요 사건 | page fault | index search, split |
| 사용자 코드와의 거리 | 운영체제가 자동 처리 | DB 엔진이 직접 설계/구현 |

따라서 page table은 B+ Tree의 일부가 아닙니다.
B+ Tree가 page 단위로 저장될 때, 그 page들이 메모리에 올라오거나 내려가는 과정에서 운영체제의 page table과 page fault가 간접적으로 관여할 수 있을 뿐입니다.

## 7. page fault와 B+ Tree는 어떻게 연결될 수 있는가

DB가 B+ Tree index page를 디스크 파일에 저장한다고 가정해보겠습니다.
어떤 SELECT가 들어오면 DB는 root page부터 leaf page까지 읽습니다.

```text
root page 읽기
-> internal page 읽기
-> leaf page 읽기
-> row 위치 확인
```

이 page들이 이미 메모리나 DB buffer cache에 있으면 빠르게 접근할 수 있습니다.
하지만 필요한 page가 메모리에 없다면 디스크에서 읽어와야 합니다.

운영체제 관점에서는 이 과정에서 page fault가 발생할 수 있습니다.

```text
B+ Tree leaf page 접근
-> 해당 파일 page가 메모리에 없음
-> OS page fault 또는 디스크 read 발생
-> page를 메모리에 올림
-> B+ Tree 탐색 계속
```

즉 B+ Tree의 성능은 단순히 비교 횟수만의 문제가 아닙니다.
실제 DB에서는 몇 개의 page를 읽어야 하는지, 그 page가 메모리에 있는지, 디스크에서 읽어야 하는지가 중요합니다.

## 8. 우리 프로젝트와의 차이

우리 프로젝트의 B+ Tree는 실제 DBMS처럼 index page를 디스크에 저장하지 않습니다.

현재 프로젝트의 B+ Tree는 다음과 같은 단순화된 구조입니다.

```text
메모리 기반 B+ Tree
프로그램 실행 중에만 존재
테이블 활성화 시 .data 파일을 스캔해서 다시 생성
index page를 별도 디스크 파일로 저장하지 않음
```

즉 우리 프로젝트에서는 B+ Tree node가 실제 디스크 page로 저장되는 구조는 아닙니다.

하지만 개념적으로는 아래처럼 연결해서 이해할 수 있습니다.

```text
실제 DBMS:
B+ Tree node를 page 단위로 디스크에 저장

우리 프로젝트:
B+ Tree node를 메모리 구조체로 관리
```

우리 코드의 node 구조는 다음과 같습니다.

```c
struct BptNode {
    bool is_leaf;
    size_t size;
    uint64_t keys[BPTREE_MAX_KEYS];
    ...
};
```

이 `BptNode`가 실제 DBMS에서는 page 하나에 저장될 수 있다고 생각하면 됩니다.
다만 우리 프로젝트에서는 이 node가 디스크 page가 아니라 C 구조체로 메모리에 존재합니다.

### 8-1. `BPTREE_MAX_KEYS = 31`과 page 크기의 관계

우리 코드에는 B+ Tree node가 가질 수 있는 key 개수가 상수로 정해져 있습니다.

관련 코드:

- [`src/storage.c`](../../../../src/storage.c#L8)
- [`src/bptree.c`](../../../../src/bptree.c#L6)

```c
#define BPTREE_MAX_KEYS 31
```

이 값 때문에 우리 프로젝트의 B+ Tree node는 최대 key 31개를 가질 수 있습니다.

```c
uint64_t keys[BPTREE_MAX_KEYS];
```

internal node는 key보다 child pointer가 하나 더 많기 때문에 최대 child pointer는 32개입니다.

```c
BptNode *children[BPTREE_MAX_KEYS + 1];
```

즉 우리 코드 기준으로는 다음과 같습니다.

```text
한 node의 최대 key 수      = 31
internal node의 최대 child 수 = 32
```

실제 DBMS에서는 이런 최대 key 수를 보통 page 크기와 연결해서 정합니다.
예를 들어 index page 하나가 4096 byte이고, key와 pointer 하나가 각각 몇 byte인지 알면 한 page 안에 key를 몇 개까지 넣을 수 있는지 계산할 수 있습니다.

단순화해서 보면 이런 식입니다.

```text
page 크기 = 4096 byte
key 크기 = 8 byte
child pointer 크기 = 8 byte
page header = 약간의 metadata

한 page에 들어갈 수 있는 key 수
= page 크기 안에 key 배열과 pointer 배열이 얼마나 들어가는지로 결정
```

그래서 실제 DBMS의 B+ Tree에서는 아래 관계가 중요합니다.

```text
page가 클수록
-> 한 node에 더 많은 key를 넣을 수 있음
-> child 수도 많아짐
-> tree 높이가 낮아짐
-> 디스크 page 접근 횟수가 줄어듦
```

하지만 우리 프로젝트의 `31`은 실제 디스크 page 크기를 계산해서 나온 값은 아닙니다.
우리 프로젝트는 B+ Tree node를 디스크 page로 저장하지 않고, 메모리의 C 구조체로만 관리합니다.
따라서 `BPTREE_MAX_KEYS = 31`은 **page 크기 기반 계산 결과라기보다, 과제 구현을 위해 정한 node 용량 제한**으로 보면 됩니다.

정리하면 다음과 같습니다.

```text
실제 DBMS:
page 크기, key 크기, pointer 크기 등을 기준으로 node당 key 수를 설계한다.

우리 프로젝트:
BPTREE_MAX_KEYS = 31이라는 상수로 node당 key 수를 고정한다.
이 값은 page 크기 계산 결과가 아니라 메모리 기반 구현의 단순화된 제한이다.
```

그래도 개념적으로는 연결해서 이해할 수 있습니다.

```text
실제 DBMS의 page 하나
~ 우리 프로젝트의 BptNode 하나

실제 DBMS의 page에 들어갈 수 있는 key 수
~ 우리 프로젝트의 BPTREE_MAX_KEYS
```

즉 `31`은 우리 코드에서 "한 B+ Tree node가 어느 정도까지 차면 split해야 하는가"를 결정하는 값입니다.
실제 page 기반 B+ Tree에서 page가 꽉 차면 split하듯이, 우리 프로젝트에서는 key가 31개를 넘으려고 하면 split이 발생합니다.

## 9. 이 문서에서 기억할 것

1. page는 큰 저장 공간을 일정한 크기로 나눈 단위이다.
2. page table은 운영체제가 가상 주소를 물리 주소로 바꾸기 위해 쓰는 표이다.
3. page fault는 필요한 page가 메모리에 없을 때 운영체제가 page를 가져오는 사건이다.
4. DB에서 page는 데이터나 인덱스를 저장하고 읽는 단위이다.
5. B+ Tree node는 실제 DBMS에서 page 단위로 저장될 수 있다.
6. B+ Tree가 page와 잘 맞는 이유는 한 page 안에 여러 key를 담아 디스크 접근 횟수를 줄일 수 있기 때문이다.
7. 우리 프로젝트의 B+ Tree는 디스크 page 기반이 아니라 메모리 기반 구조체로 구현되어 있다.
8. 우리 코드의 `BPTREE_MAX_KEYS = 31`은 실제 page 크기 계산 결과가 아니라, 한 node가 최대 몇 개의 key를 가질 수 있는지 정한 구현 상수이다.
