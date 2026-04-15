# B-Tree와 B+ Tree 개념 정리

이 문서는 우리 프로젝트에서 쓰는 트리가 B-Tree인지 B+ Tree인지 구분하고, 두 자료구조의 핵심 개념을 이해하기 위한 문서입니다.

결론부터 말하면, 우리 프로젝트에서 의도하고 구현한 자료구조는 **B+ Tree**입니다.

## 1. 왜 이진 탐색 트리가 아니라 B 계열 트리인가

일반 이진 탐색 트리는 한 노드에 key를 하나만 두고, 왼쪽/오른쪽 자식으로 갈라집니다.

```text
        10
       /  \
      5    20
```

하지만 데이터가 많아지면 트리 높이가 커질 수 있습니다.
트리 높이가 커진다는 것은 검색할 때 거쳐야 하는 노드 수가 많아진다는 뜻입니다.

B-Tree와 B+ Tree는 한 노드에 여러 key를 저장합니다.

```text
[ 10 | 20 | 30 ]
```

그래서 한 번 노드를 볼 때 더 많은 범위를 판단할 수 있고, 트리 높이를 낮게 유지할 수 있습니다.
DB 인덱스에서 B 계열 트리를 많이 쓰는 이유가 여기에 있습니다.

## 2. B 계열 트리를 이해하기 위한 기본 용어

B-Tree나 B+ Tree를 설명할 때 `차수`, `k-1`, `k+1`, `연결리스트` 같은 표현이 자주 나옵니다.
이 표현들은 대부분 **한 node가 key와 child를 몇 개 가질 수 있는지**, 그리고 **leaf node가 어떻게 이어지는지**를 설명하기 위한 말입니다.

### 2-1. 차수

트리에서 `차수`는 보통 **한 node가 가질 수 있는 child 수의 기준**입니다.

다만 자료마다 차수를 정의하는 방식이 조금 다를 수 있습니다.

```text
어떤 자료:
차수 m = 한 node가 가질 수 있는 최대 child 수

다른 자료:
차수 k = 한 node가 가질 수 있는 최소 child 수 또는 기준 child 수
```

그래서 설명을 볼 때는 `k`나 `m`이 정확히 무엇을 뜻하는지 먼저 확인해야 합니다.
하지만 B-Tree와 B+ Tree에서 자주 쓰이는 기본 관계는 같습니다.

```text
child 수 = key 수 + 1
key 수   = child 수 - 1
```

### 2-2. k-1

`k-1`은 보통 **child가 k개 있으면 key는 k-1개 필요하다**는 뜻으로 나옵니다.

예를 들어 internal node에 child가 3개 있으면, 그 사이를 나누는 key는 2개입니다.

```text
        [10 | 20]
       /    |    \
    <10   10~20   >20
```

여기서 `10`과 `20`은 데이터를 직접 저장하기 위한 값이라기보다, child들이 맡을 범위를 나누는 경계선입니다.

```text
child 수 = 3
key 수   = 2
key 수   = child 수 - 1
```

그래서 child 수를 `k`라고 하면 key 수는 `k-1`입니다.

### 2-3. k+1

반대로 `k+1`은 **key가 k개 있으면 child는 k+1개 필요하다**는 뜻으로 나옵니다.

예를 들어 key가 3개라면, 그 key들이 나누는 범위는 4개입니다.

```text
        [10 | 20 | 30]
       /    |    |    \
    <10  10~20 20~30  >30
```

```text
key 수   = 3
child 수 = 4
child 수 = key 수 + 1
```

즉 key는 경계선이고, child는 그 경계선으로 나뉜 구간입니다.
경계선이 3개 있으면 구간은 4개가 됩니다.

### 2-4. 연결리스트

B+ Tree에서 `연결리스트`는 보통 **leaf node들이 옆으로 이어진 구조**를 말합니다.

```text
[1 | 5 | 9] -> [12 | 20 | 35] -> [42 | 50 | 60]
```

여기서 화살표는 internal node의 `children` 배열이 아닙니다.
각 leaf node 안에 있는 `next` 포인터가 오른쪽 leaf node의 주소를 기억하는 구조입니다.

```text
leaf1
  keys = [1, 5, 9]
  next = leaf2의 주소

leaf2
  keys = [12, 20, 35]
  next = leaf3의 주소

leaf3
  keys = [42, 50, 60]
  next = NULL
```

즉, 연결리스트는 "child 리스트의 마지막 칸이 다음 리스트를 가리키는 것"이라기보다, **각 leaf node가 자기 오른쪽에 있는 다음 leaf node를 직접 가리키는 것**입니다.

B+ Tree는 실제 row 위치를 leaf node에 저장합니다.
그래서 leaf node들이 옆으로 연결되어 있으면 범위 검색을 할 때 유리합니다.

예를 들어 아래 조건을 찾는다고 가정합니다.

```sql
WHERE id >= 10 AND id <= 50
```

먼저 B+ Tree를 타고 내려가서 `10`이 처음 나올 수 있는 leaf를 찾습니다.

```text
[12 | 20 | 35]
```

그 다음에는 다시 root로 올라가지 않고 leaf의 `next`를 따라갑니다.

```text
[12 | 20 | 35] -> [42 | 50 | 60]
```

그러면 `12`, `20`, `35`, `42`, `50`을 순서대로 읽고, `60`을 만나면 범위를 벗어났으므로 멈출 수 있습니다.

### 2-5. 우리 프로젝트 코드와 연결해서 보기

우리 프로젝트의 B+ Tree node 구조에서도 이 관계를 볼 수 있습니다.

```c
uint64_t keys[BPTREE_MAX_KEYS];
```

internal node는 key보다 child를 하나 더 많이 가질 수 있습니다.

```c
BptNode *children[BPTREE_MAX_KEYS + 1];
```

즉 코드에서도 아래 관계가 드러납니다.

```text
key 최대 개수   = BPTREE_MAX_KEYS
child 최대 개수 = BPTREE_MAX_KEYS + 1
```

leaf node는 child를 가지지 않습니다.
대신 key에 대응되는 실제 row 위치와, 다음 leaf로 이동하기 위한 `next`를 가집니다.

```c
RowRef refs[BPTREE_MAX_KEYS];
BptNode *next;
```

이 둘은 역할이 다릅니다.

```text
children:
root 또는 internal node에서 아래 node로 내려가기 위한 포인터

next:
leaf node에서 오른쪽 leaf node로 이동하기 위한 포인터
```

예를 들어 아래처럼 root가 leaf 세 개를 child로 가진다고 해보겠습니다.

```text
        [30 | 60]
       /    |    \
    leaf1  leaf2  leaf3
```

위에서 아래로 내려가는 포인터는 `children`입니다.

```text
root.children[0] = leaf1
root.children[1] = leaf2
root.children[2] = leaf3
```

반면 leaf끼리 옆으로 이어지는 포인터는 `next`입니다.

```text
leaf1.next = leaf2
leaf2.next = leaf3
leaf3.next = NULL
```

범위 검색은 처음에는 `children`을 따라 시작 leaf를 찾고, 그 다음부터는 `next`를 따라 오른쪽 leaf들을 순서대로 읽습니다.

정리하면 다음과 같습니다.

```text
k-1:
child가 k개일 때 key는 k-1개

k+1:
key가 k개일 때 child는 k+1개

차수:
한 node가 가질 수 있는 child 수의 기준

연결리스트:
B+ Tree의 leaf node들을 next 포인터로 옆으로 이어 놓은 구조
```

한 문장으로 말하면, **B+ Tree에서 internal node의 key는 child 사이를 나누는 경계선이고, leaf node들은 next로 연결되어 범위 검색을 순서대로 빠르게 할 수 있게 해줍니다.**

## 3. B-Tree란

B-Tree는 균형 탐색 트리입니다.

특징은 다음과 같습니다.

- 한 노드가 여러 key를 가질 수 있다.
- key들은 정렬되어 있다.
- 내부 노드가 검색 key와 실제 데이터 위치를 함께 가질 수 있다.
- leaf 노드도 검색 key와 실제 데이터 위치를 함께 가질 수 있다.
- 모든 leaf 노드가 같은 깊이에 오도록 균형을 유지한다.
- 노드가 가득 차면 split해서 부모로 key를 올린다.

간단히 말하면, B-Tree는 **검색 key와 실제 값 또는 실제 값의 위치가 internal node와 leaf node에 모두 놓일 수 있는 다분기 균형 트리**입니다.

여기서 중요한 표현은 **가질 수 있다**입니다.
B-Tree에서 모든 노드가 반드시 실제 값을 가져야 한다는 뜻은 아닙니다.
다만 B+ Tree처럼 "실제 값은 leaf node에만 둔다"는 규칙이 강제되지는 않습니다.

예를 들어 B-Tree에서는 내부 노드에 있는 `20`이라는 key가 실제 row 위치를 직접 가질 수도 있습니다.

```text
          [20]
         /    \
 [5 | 10]    [30 | 40]
```

여기서 `[20]`도 단순 길잡이일 뿐 아니라 실제 데이터 위치를 가질 수 있습니다.

반대로 B+ Tree에서는 이런 방식으로 internal node가 실제 row 위치를 직접 가지지 않습니다.
internal node의 key는 아래 leaf 중 어느 쪽으로 내려갈지 정하는 길잡이 역할만 합니다.

## 4. B+ Tree란

B+ Tree도 B-Tree 계열의 균형 탐색 트리입니다.
하지만 중요한 차이가 있습니다.

**B+ Tree는 실제 데이터 포인터를 leaf 노드에만 둡니다.**

내부 노드는 검색 방향을 결정하는 길잡이 역할만 합니다.

```text
              [20 | 40]
             /    |    \
 [1 | 5 | 10] [20 | 30] [40 | 50]
```

여기서 내부 노드의 `20`, `40`은 검색 경로를 정하기 위한 separator key입니다.
실제 row 위치는 leaf 노드에 있습니다.

B+ Tree의 대표 특징은 다음과 같습니다.

- 내부 노드는 검색 경로 안내용 key를 가진다.
- 실제 값 또는 row 위치는 leaf 노드에 저장한다.
- leaf 노드들이 linked list처럼 옆으로 연결될 수 있다.
- 범위 검색에 유리하다.
- DB 인덱스 구현에서 자주 쓰인다.

## 4-1. split할 때 어떤 key가 부모로 올라가는가

노드가 가득 차면 split이 일어납니다.
이때 어떤 key가 부모 key가 되고, 어떤 key가 자식 node에 남는지는 사용자 마음대로 정하는 것이 아닙니다.
기본 기준은 **정렬된 key를 중간 지점에서 나누는 것**입니다.

다만 B-Tree와 B+ Tree는 부모로 올라가는 key의 의미가 다릅니다.

### B-Tree의 split

B-Tree에서는 보통 가운데 key, 즉 **median key**가 부모로 올라갑니다.

예를 들어 한 노드에 key가 아래처럼 들어 있다고 가정합니다.

```text
[10 | 20 | 30 | 40 | 50]
```

가운데 key는 `30`입니다.
그래서 split 후에는 보통 아래처럼 됩니다.

```text
        [30]
       /    \
[10 | 20]  [40 | 50]
```

여기서 `30`은 부모 node의 key가 되고, 자식 node에서는 빠집니다.
즉 B-Tree에서는 **부모로 올라간 key 자체가 분리 기준**이 됩니다.

### B+ Tree의 split

B+ Tree에서도 key를 정렬한 뒤 중간 지점에서 나눕니다.
하지만 B+ Tree는 실제 row 위치를 leaf node에만 둬야 합니다.

그래서 leaf node가 split될 때는 부모에게 key를 **올린다**기보다는, 오른쪽 leaf의 첫 key를 부모에게 **복사해서 알려준다**고 이해하는 것이 좋습니다.

예를 들어 leaf node가 아래처럼 가득 찼다고 가정합니다.

```text
[10 | 20 | 30 | 40 | 50]
```

split 후 leaf node는 둘로 나뉩니다.

```text
[10 | 20] -> [30 | 40 | 50]
```

이때 부모 node에는 오른쪽 leaf의 첫 key인 `30`이 separator key로 들어갑니다.

```text
        [30]
       /    \
[10 | 20] -> [30 | 40 | 50]
```

여기서 중요한 점은 `30`이 부모에도 있지만, 오른쪽 leaf node에도 그대로 남아 있다는 것입니다.
B+ Tree에서 실제 row 위치는 leaf node에 있어야 하므로, leaf의 `30`을 없애면 안 됩니다.

정리하면 다음과 같습니다.

```text
B-Tree:
median key가 부모로 올라가고, 자식에서는 빠질 수 있다.

B+ Tree:
오른쪽 node의 첫 key가 부모의 separator key로 복사되고,
leaf node에는 실제 key와 row 위치가 그대로 남는다.
```

우리 프로젝트는 B+ Tree입니다.
따라서 split 기준은 사용자가 고르는 것이 아니라, 정렬된 key들을 나눈 뒤 **오른쪽 node의 첫 key를 부모에게 separator key로 전달하는 방식**으로 이해하면 됩니다.

## 4-2. B+ Tree가 범위 검색에 유리한 이유

B+ Tree에서 가장 중요한 특징 중 하나는 **leaf 노드들이 옆으로 연결되어 있다**는 점입니다.

예를 들어 leaf 노드가 아래처럼 이어져 있다고 생각하면 됩니다.

```text
[1 | 5 | 9] -> [12 | 20 | 35] -> [42 | 50 | 60] -> [70 | 80]
```

여기서 `WHERE id >= 10 AND id <= 50` 같은 범위 조건을 찾는다고 가정해 보겠습니다.

B+ Tree는 먼저 내부 노드를 타고 내려가서 `10`이 들어갈 수 있는 첫 leaf 위치를 찾습니다.
그 다음부터는 트리를 다시 위아래로 오르내리지 않고, leaf의 `next` 포인터를 따라 오른쪽으로 이동하면서 범위에 포함되는 key를 읽으면 됩니다.

```text
1. 10 이상이 처음 나올 수 있는 leaf 찾기
   -> [12 | 20 | 35]

2. 현재 leaf에서 범위에 맞는 key 읽기
   -> 12, 20, 35

3. next 포인터로 오른쪽 leaf 이동
   -> [42 | 50 | 60]

4. 계속 읽다가 범위를 벗어나면 중단
   -> 42, 50까지 읽고 60에서 중단
```

즉, 범위 조회는 아래처럼 진행됩니다.

```text
root/internal 탐색으로 시작 위치 찾기
-> leaf 안에서 key 읽기
-> leaf.next로 오른쪽 leaf 이동
-> 범위를 벗어나면 중단
```

B-Tree도 범위 검색을 할 수는 있지만, 실제 데이터가 internal 노드와 leaf 노드에 흩어질 수 있습니다.
반면 B+ Tree는 실제 row 위치가 leaf에 모여 있고 leaf끼리 연결되어 있기 때문에 범위 검색 흐름이 더 단순합니다.

## 5. B-Tree와 B+ Tree 차이

| 구분 | B-Tree | B+ Tree |
| --- | --- | --- |
| 실제 데이터 위치 | 내부 노드와 leaf 노드 모두 가능 | leaf 노드에만 저장 |
| 내부 노드 역할 | key와 데이터를 둘 다 가질 수 있음 | 검색 방향을 정하는 guide 역할 |
| leaf 연결 | 필수 아님 | 보통 leaf끼리 linked list로 연결 |
| split 후 부모 key | median key가 부모로 이동 | 오른쪽 node의 첫 key가 부모 separator로 복사 |
| 범위 검색 | 가능하지만 상대적으로 불편 | leaf 연결 덕분에 유리 |
| DB 인덱스 사용 | 사용 가능 | DB 인덱스에서 더 흔함 |

## 6. 우리 프로젝트는 왜 B+ Tree인가

우리 프로젝트의 노드 구조를 보면 leaf 노드와 internal 노드를 구분합니다.

관련 코드:

- [`BptNode`](../../../../src/storage.c#L11)

구조적으로 보면 다음과 같습니다.

```c
struct BptNode {
    bool is_leaf;
    size_t size;
    uint64_t keys[BPTREE_MAX_KEYS];
    union {
        struct {
            RowRef refs[BPTREE_MAX_KEYS];
            BptNode *next;
        } leaf;
        struct {
            BptNode *children[BPTREE_MAX_KEYS + 1];
        } internal;
    } as;
};
```

여기서 핵심은 이 부분입니다.

```text
leaf node:
  keys
  refs
  next

internal node:
  keys
  children
```

leaf 노드는 `refs`를 가집니다.
이 `refs`가 실제 row 위치인 `row_ref`입니다.

반면 internal 노드는 `children`만 가지고, 실제 row 위치인 `refs`는 없습니다.
즉, 실제 값은 leaf에만 있습니다.

그래서 우리 프로젝트의 인덱스는 B-Tree가 아니라 B+ Tree 방식입니다.

## 7. 우리 프로젝트 B+ Tree의 key와 value

우리 프로젝트에서 B+ Tree는 아래 매핑을 저장합니다.

```text
key   = id
value = row_ref
```

예를 들어 학생 데이터가 바이너리 파일에 이렇게 저장되어 있다고 가정합니다.

```text
id=1 row_ref=0
id=2 row_ref=37
id=3 row_ref=78
```

B+ Tree에는 이런 정보가 들어갑니다.

```text
1 -> 0
2 -> 37
3 -> 78
```

`SELECT ... WHERE id = 2`가 들어오면 B+ Tree에서 `2`를 찾고, 결과로 `37`이라는 파일 offset을 받습니다.
그 다음 바이너리 파일의 37번째 byte 위치로 이동해 row 하나만 읽습니다.

## 8. 이 프로젝트에서 B+ Tree가 단순화된 부분

실제 DBMS의 B+ Tree는 보통 디스크 페이지 단위로 저장됩니다.
하지만 이 프로젝트의 B+ Tree는 단순화되어 있습니다.

- 메모리 기반이다.
- 프로그램이 실행될 때마다 인덱스를 다시 만든다.
- 인덱스 자체를 디스크에 저장하지 않는다.
- 삭제 연산은 없다.
- `id` 하나만 인덱싱한다.
- `id` 단건 검색과 `id` 범위 검색을 지원한다.
- 범위 조건은 `id > ?`, `id >= ?`, `id < ?`, `id <= ?` 형태로 나뉘어 구현되어 있다.

즉, 이 프로젝트의 B+ Tree는 실제 DBMS 수준의 완전한 인덱스라기보다는, 과제 목적에 맞춘 **메모리 기반 id 전용 B+ Tree 인덱스**입니다.

## 9. 이 문서에서 기억할 것

우리 프로젝트에서 쓰는 것은 B+ Tree입니다.

그 이유는 다음 세 가지입니다.

1. internal node는 길잡이 key와 child pointer만 가진다.
2. 실제 row 위치인 `row_ref`는 leaf node에만 저장된다.
3. leaf node에 `next` 포인터가 있어 B+ Tree 구조를 따른다.

한 문장으로 요약하면:

```text
우리 프로젝트의 B+ Tree는 id를 key로 받고, leaf node에서 row_ref를 찾아 바이너리 파일의 특정 row로 바로 이동하거나, leaf의 next 연결을 따라 id 범위 row들을 순서대로 읽기 위한 인덱스이다.
```
