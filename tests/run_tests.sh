#!/bin/sh
#
# Host-Tests uebersetzen und ausfuehren. Aus dem Wurzelverzeichnis des
# Repos aufrufen:  sh tests/run_tests.sh
#
# Diese Tests laufen auf dem PC, nicht auf dem Controller. Sie pruefen die
# HAL-freien Teile der Firmware - aktuell den Config-Speicher und die
# Umrechnung der Sensor-Rohwerte.

set -e

CC=${CC:-gcc}
CFLAGS="-std=gnu11 -Wall -Wextra -O1"
OUT=$(mktemp -d)
rc=0

run() {
	name=$1
	shift
	printf '\n>>> %s\n' "$name"
	if ! "$@"; then
		rc=1
	fi
}

# Der Config-Store ist variantenneutral; die Variante 1001 wird nur
# eingebunden, weil app_types.h die Werksvorgaben von dort holt.
$CC $CFLAGS -ICore/Inc -ICore/Inc/variants/1001 \
	-o "$OUT/test_config_store" tests/test_config_store.c
run "config_store" "$OUT/test_config_store"

for hwv in 1000 1001 1003; do
	$CC $CFLAGS -ICore/Inc -ICore/Inc/variants/$hwv \
		-o "$OUT/test_sensor_conv_$hwv" tests/test_sensor_conv.c
	run "sensor_conv HW_VARIANT=$hwv" "$OUT/test_sensor_conv_$hwv"
done

rm -rf "$OUT"

if [ $rc -eq 0 ]; then
	echo "Alle Host-Tests bestanden."
else
	echo "Es sind Host-Tests fehlgeschlagen." >&2
fi

exit $rc
