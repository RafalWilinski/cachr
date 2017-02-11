#!/usr/bin/env bash
echo "First" && curl localhost:3001/one_second -v -w %{time_connect}:%{time_starttransfer}:%{time_total} &
echo "Second" && curl localhost:3001/one_second -v -w %{time_connect}:%{time_starttransfer}:%{time_total} &
