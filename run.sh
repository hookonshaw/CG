#!/bin/zsh
set -e
cd "${0:A:h}"
lab_number="${1:-1}"
if (( $# )); then shift; fi
case "$lab_number" in
  1) exec zsh ./lab1/run-macos.command "$@" ;;
  *) print -u2 "Эта лабораторная ещё не добавлена: $lab_number"; exit 1 ;;
esac
