#!/bin/sh

set -e

SCRIPT_NAME=${0##*/}
FACTORY_OFFSET=0x28

usage() {
	echo "Usage: $SCRIPT_NAME <colon-separated-mac>" >&2
	exit 1
}

fail() {
	echo "$SCRIPT_NAME: $*" >&2
	exit 1
}

validate_mac() {
	case "$1" in
		[0-9A-Fa-f][0-9A-Fa-f]:[0-9A-Fa-f][0-9A-Fa-f]:[0-9A-Fa-f][0-9A-Fa-f]:[0-9A-Fa-f][0-9A-Fa-f]:[0-9A-Fa-f][0-9A-Fa-f]:[0-9A-Fa-f][0-9A-Fa-f])
			return 0
			;;
		*)
			return 1
			;;
	esac
}

[ $# -eq 1 ] || usage
MAC_RAW=$1
validate_mac "$MAC_RAW" || fail "invalid MAC address: $MAC_RAW"
MAC=$(echo "$MAC_RAW" | tr 'A-F' 'a-f')
case "$MAC" in
	00:00:00:00:00:00|ff:ff:ff:ff:ff:ff)
		fail "refusing to program reserved MAC address: $MAC"
		;;
	esac
OLDIFS=$IFS
IFS=:
set -- $MAC
IFS=$OLDIFS
[ $((0x$1 & 1)) -eq 0 ] || fail "refusing to program multicast MAC address: $MAC"
MAC_HEX=$(echo "$MAC" | tr -d ':')

MTD_INDEX=$(awk -F'[:"]' '/"factory"/ { sub(/^mtd/, "", $1); print $1; exit }' /proc/mtd)
[ -n "$MTD_INDEX" ] || fail "factory partition not found"

MTD_CHAR=/dev/mtd$MTD_INDEX
MTD_BLOCK=/dev/mtdblock$MTD_INDEX
[ -c "$MTD_CHAR" ] || fail "$MTD_CHAR is missing"
[ -b "$MTD_BLOCK" ] || fail "$MTD_BLOCK is missing"

if [ -r "/sys/class/mtd/mtd$MTD_INDEX/flags" ]; then
	MTD_FLAGS=$(cat "/sys/class/mtd/mtd$MTD_INDEX/flags")
	[ $((MTD_FLAGS & 0x400)) -ne 0 ] || fail "factory partition is not writable from Linux"
fi

PART_SIZE=$(cat "/sys/class/mtd/mtd$MTD_INDEX/size")
[ "$PART_SIZE" -ge 46 ] || fail "factory partition is unexpectedly small"

TMPDIR=$(mktemp -d /tmp/program-eth-mac.XXXXXX) || fail "unable to create temporary directory"
trap 'rm -rf "$TMPDIR"' EXIT INT TERM

ORIG=$TMPDIR/factory.bin
NEW=$TMPDIR/factory-new.bin
MAC_BIN=$TMPDIR/mac.bin

dd if="$MTD_BLOCK" of="$ORIG" bs="$PART_SIZE" count=1
cp "$ORIG" "$NEW"

printf "\x$1\x$2\x$3\x$4\x$5\x$6" > "$MAC_BIN"
dd if="$MAC_BIN" of="$NEW" bs=1 seek=$((FACTORY_OFFSET)) conv=notrunc

mtd unlock factory >/dev/null 2>&1 || true
mtd erase factory >/dev/null
mtd write "$NEW" factory >/dev/null

ACTUAL_HEX=$(hexdump -v -s $((FACTORY_OFFSET)) -n 6 -e '6/1 "%02x"' "$MTD_BLOCK")
[ "$ACTUAL_HEX" = "$MAC_HEX" ] || fail "verification failed: read back $ACTUAL_HEX"

echo "Programmed $MAC into factory+$FACTORY_OFFSET on $MTD_CHAR"
echo "Reboot the device to apply the new MAC to Linux network interfaces."
