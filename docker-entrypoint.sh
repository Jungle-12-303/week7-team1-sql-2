#!/bin/sh
set -eu

if [ "$#" -eq 0 ]; then
  exec /app/build/mini_sql /app/examples/db /app/examples/sql/demo_workflow.sql
fi

exec /app/build/mini_sql "$@"
