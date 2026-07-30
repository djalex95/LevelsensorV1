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
$CC $CFLAGS -DHW_VARIANT=1001 -ICore/Inc -ICore/Inc/variants/1001 \
	-o "$OUT/test_config_store" tests/test_config_store.c
run "config_store" "$OUT/test_config_store"

for hwv in 1000 1001 1003; do
	$CC $CFLAGS -DHW_VARIANT=$hwv -ICore/Inc -ICore/Inc/variants/$hwv \
		-o "$OUT/test_sensor_conv_$hwv" tests/test_sensor_conv.c
	run "sensor_conv HW_VARIANT=$hwv" "$OUT/test_sensor_conv_$hwv"
done

# Gegenprobe: Include-Pfad und HW_VARIANT absichtlich verdreht. Die
# Abfrage in sensor_cfg.h muss das abfangen - sonst entstuende eine
# Firmware, die mit dem Messbereich der einen und der Kennung der
# anderen Variante liefe. Der Uebersetzungslauf MUSS hier scheitern.
printf '\n>>> Verdrehte Variante wird abgefangen\n'
if $CC $CFLAGS -DHW_VARIANT=1003 -ICore/Inc -ICore/Inc/variants/1001 \
	-o "$OUT/test_mismatch" tests/test_sensor_conv.c 2>/dev/null
then
	echo "FEHLER: Pfad 1001 mit HW_VARIANT=1003 wurde uebersetzt." >&2
	rc=1
else
	echo "ok: Pfad 1001 mit HW_VARIANT=1003 wird abgelehnt"
fi

rm -rf "$OUT"

if [ $rc -eq 0 ]; then
	echo "Alle Host-Tests bestanden."
else
	echo "Es sind Host-Tests fehlgeschlagen." >&2
fi

exit $rc
