#!/bin/bash
# The probe kit's tests (utilities/probe/test) as an otherwise unused uid in its own pid namespace, on a copy of
# utilities/probe.  busybox ash calls its own applets ahead of PATH: run_tests.sh puts its stubs in front of them
# as shell functions and checks that none is bypassed, and refuses to run as root (so does mutants.sh); this
# script is the way to run them from a root shell, and a stub that were bypassed anyway could reach neither the
# host's devices nor any other process (docs/review-final.md, "Test-harness hazard").  Run as root.
#   utilities/dev/run_probe.sh run|mutants   (env: SHELLS, ONLY, QUICK, ROOTFS are passed on)
set -u
what=${1:?usage: $0 run|mutants}
WT=$(cd "$(dirname "$0")/../.." && pwd)
U=64999
O=${TEST_OUT:-/root/hdz/test-out}/probe
mkdir -p "$O"
P=/tmp/probe-test-$what
rm -rf "$P"
mkdir -p "$P"
cp -r "$WT/utilities/probe" "$P/"
chown -R $U:$U "$P"
chmod 755 "$P"
case $what in
  run) S=$P/probe/test/run_tests.sh ;;
  mutants) S=$P/probe/test/mutants.sh ;;
  *) echo "usage: $0 run|mutants"; exit 2 ;;
esac
start=$(date +%s)
unshare -p -f --mount-proc setpriv --reuid=$U --regid=$U --clear-groups \
    env -i PATH=/usr/local/bin:/usr/bin:/bin HOME="$P" TMPDIR="$P" LANG=C \
    ${SHELLS:+SHELLS="$SHELLS"} ${ONLY:+ONLY="$ONLY"} ${QUICK:+QUICK="$QUICK"} ${ROOTFS:+ROOTFS="$ROOTFS"} \
    sh "$S" > "$O/$what.log" 2>&1
rc=$?
echo "exit $rc after $(( $(date +%s) - start )) s" >> "$O/$what.log"
rm -rf "$P"
echo "rc $rc log $O/$what.log"
exit $rc
