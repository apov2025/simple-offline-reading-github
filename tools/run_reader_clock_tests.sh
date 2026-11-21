#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
cc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -g -fsanitize=address,undefined -Itools/clock_stubs tools/reader_clock_host_test.c -o "$out/clock"
"$out/clock"
python3 - "$out" <<'PYCODE'
from pathlib import Path
import sys
source=Path('main/board/reader_clock.c').read_text()
a=source.index('static bool s_net_ready,s_net_failed;')
b=source.rindex('#endif')
Path(sys.argv[1],'reader_clock_worker_under_test.h').write_text(source[a:b])
PYCODE
cc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -g -fsanitize=address,undefined -Itools/clock_stubs -I"$out" tools/reader_clock_network_host_test.c -o "$out/network"
"$out/network"
python3 - "$out" <<'PYCODE'
from pathlib import Path
import sys
s=Path('components/read_pico_transfer/read_pico_transfer.c').read_text()
functions=['static bool claim_network','static void release_network','static bool claim_config','static void release_config','bool read_pico_transfer_try_stop_if_idle','void read_pico_transfer_stop','esp_err_t read_pico_transfer_clock_acquire','void read_pico_transfer_clock_release']
parts=[]
for name in functions:
 a=s.index(name+'(');b=s.index('{',a);depth=1;i=b+1
 while depth:
  depth+=(s[i]=='{')-(s[i]=='}');i+=1
 parts.append(s[a:i])
# Start and scan must reserve before any driver initialization. / 启动和扫描须先取得租约。
for name in ['esp_err_t read_pico_transfer_start','esp_err_t read_pico_transfer_scan_wifi']:
 a=s.index(name+'(');b=s.index('esp_netif_init()',a)
 assert 'claim_network(false)' in s[a:b]
Path(sys.argv[1],'reader_clock_lease_under_test.h').write_text('\n'.join(parts))
PYCODE
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -I"$out" tools/reader_clock_lease_host_test.c -o "$out/lease"
"$out/lease"
