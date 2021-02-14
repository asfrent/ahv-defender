#!/bin/bash

set +x

function expect_is() {
  if [ "$1" == "$2" ]; then
    echo "PASS"
  else
    echo "FAIL"
  fi
}

RANDOM_AHV=7567034091969

echo -n "LOOKUP   ..... "
expect_is $(echo ${RANDOM_AHV} | ./cli localhost:12000 lookup) "false"
echo -n "ADD      ..... "
expect_is $(echo ${RANDOM_AHV} | ./cli localhost:12000 add) "true"
echo -n "ADD      ..... "
expect_is $(echo ${RANDOM_AHV} | ./cli localhost:12000 add) "false"
echo -n "LOOKUP   ..... "
expect_is $(echo ${RANDOM_AHV} | ./cli localhost:12000 lookup) "true"
echo -n "REMOVE   ..... "
expect_is $(echo ${RANDOM_AHV} | ./cli localhost:12000 remove) "true"
echo -n "REMOVE   ..... "
expect_is $(echo ${RANDOM_AHV} | ./cli localhost:12000 remove) "false"
echo -n "LOOKUP   ..... "
expect_is $(echo ${RANDOM_AHV} | ./cli localhost:12000 lookup) "false"
