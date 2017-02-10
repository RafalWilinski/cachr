echo "First" && curl localhost:3001/one_second -v -w %{time_connect}:%{time_starttransfer}:%{time_total} &
echo "First" && curl localhost:3001/one_second -v -w %{time_connect}:%{time_starttransfer}:%{time_total} &
echo "First" && curl localhost:3001/one_second -v -w %{time_connect}:%{time_starttransfer}:%{time_total} &
