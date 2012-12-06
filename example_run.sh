## VALIDATE AND HANDLE ARGUMENTS
test $# -lt 5 && echo "sh run.sh period event1 event2 event3 event4" >& 2 && exit 1
test -z "$1" && echo "Cannot proceed; period is an empty string." >&2 && exit 1
isnum=$( echo "$1" | sed -e "s/[[:digit:]]//g" | wc -c )
test "$isnum" -ne 1 && echo "Numeric period needed, please." >&2 && exit 1
test "$1" -lt 10000 && echo "Period must be at least 10,000." >&2 && exit 1

## Initialize module
modprobe -r pmu_sync_sample
modprobe pmu_sync_sample
if [ ! -c /dev/pmu_samples ]
then
	rm -f /dev/pmu_samples
	mknod /dev/pmu_samples c 222 0
fi
echo 0 > /sys/sync_pmu/status

## Set parameters
echo "Setting period to $1"
echo "$1" > /sys/sync_pmu/period
echo "$2" > /sys/sync_pmu/0
echo "$3" > /sys/sync_pmu/1
echo "$4" > /sys/sync_pmu/2
echo "$5" > /sys/sync_pmu/3

echo "Setting status to 1"
echo 1 > /sys/sync_pmu/status
echo "Started sampling..."

echo "Sleeping for a second..."
sleep 1

echo "Sending data to client"
./sender/sender simlab00.cs.columbia.edu 3141
# if [ $? -ne 0 ]
# then
# 	echo "Sender returned non-zero status.  Stopping." >&2
# 	sh stop.sh
# 	exit 1
# fi

echo 1 > /sys/sync_pmu/status
echo "Started sampling..."
