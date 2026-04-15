FROM gcc:14-bookworm AS build

WORKDIR /app

COPY Makefile ./
COPY include ./include
COPY src ./src
COPY tests ./tests
COPY examples ./examples

RUN make && make test

FROM debian:bookworm-slim

WORKDIR /app

COPY --from=build /app/build/mini_sql ./build/mini_sql
COPY --from=build /app/examples ./examples

ENTRYPOINT ["./build/mini_sql"]
CMD ["examples/db"]
