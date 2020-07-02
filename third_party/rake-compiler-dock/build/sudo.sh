#!/bin/sh
# Emulate the sudo command

SUDO_USER=root
SUDO_GROUP=root

while (( "$#" )); do
  case "$1" in
    # user option
    -u)
      SUDO_USER=$2
      shift 2
      ;;
    # group option
    -g)
      SUDO_GROUP=$2
      shift 2
      ;;
    # skipping arguments without values
    -A|-b|-E|-e|-H|-h|-K|-n|-P|-S|-V|-v)
      shift 1
      ;;
    # skipping arguments with values
    -a|-C|-c|-D|-i|-k|-l|-ll|-p|-r|-s|-t|-U)
      shift 2
      ;;
    # stop processing command line arguments
    --)
      shift 1
      break
      ;;
    *)
      break
      ;;
  esac
done

exec gosu $SUDO_USER:$SUDO_GROUP "$@"
