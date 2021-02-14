#!/bin/bash

set +x

function expect_is() {
  if [ "$1" == "$2" ]; then
    echo "OK"
  else
    echo "FAIL"
  fi
}

RANDOM_AHV=7568336124968

expect_is $(echo ${RANDOM_AHV} | ./cli localhost:12000 lookup) "false"
expect_is $(echo ${RANDOM_AHV} | ./cli localhost:12000 add) "true"
expect_is $(echo ${RANDOM_AHV} | ./cli localhost:12000 add) "false"
expect_is $(echo ${RANDOM_AHV} | ./cli localhost:12000 lookup) "true"
expect_is $(echo ${RANDOM_AHV} | ./cli localhost:12000 remove) "true"
expect_is $(echo ${RANDOM_AHV} | ./cli localhost:12000 remove) "false"
expect_is $(echo ${RANDOM_AHV} | ./cli localhost:12000 lookup) "false"
