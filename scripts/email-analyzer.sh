#!/bin/bash

echo -n "STANDARD ..... "
cat ../scripts/testdata/ahv-list.txt | ./email-analyzer standard > ea-test-standard.txt
if [ $(diff ../scripts/testdata/ea-test-standard.txt ea-test-standard.txt | wc -l) -gt 0 ]; then
  echo FAIL
else
  echo PASS
fi

echo -n "THOROUGH ..... "
cat ../scripts/testdata/ahv-list.txt | ./email-analyzer thorough > ea-test-thorough.txt
if [ $(diff ../scripts/testdata/ea-test-thorough.txt ea-test-thorough.txt | wc -l) -gt 0 ]; then
  echo FAIL
else
  echo PASS
fi

echo -n "PARANOID ..... "
cat ../scripts/testdata/ahv-list.txt | ./email-analyzer paranoid > ea-test-paranoid.txt
if [ $(diff ../scripts/testdata/ea-test-paranoid.txt ea-test-paranoid.txt | wc -l) -gt 0 ]; then
  echo FAIL
else
  echo PASS
fi

rm -f ea-test-*