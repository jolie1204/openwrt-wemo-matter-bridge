# NetCam 센서용 16M Matter 브리지 펌웨어 업데이트

2026-10-09 개발자용 절차입니다. 사용자는 새 이미지 준비와 local 운영 장치
설치를 승인했습니다. 기존 Google Home fabric과 플러그·조명 identity를
유지합니다. 이 문서의 사전 조사 단계에서는 서비스 정지, flash, 전원 변경,
commissioning을 실행하지 않았습니다.

## 확인한 쓰기 범위와 보존 대상

strict SSH alias `wemo-matter-bridge`에서 읽은 board는
`belkin,wemo-matter-bridge`이고 flash는 16 MiB입니다. 8M profile 또는
기존 vendor dual-image `belkin_wemo` profile을 사용하지 않습니다.

| MTD | 이름 | offset | size | 이번 업데이트 |
| --- | --- | --- | --- | --- |
| 0 | u-boot | `0x000000` | `0x030000` | 보존 |
| 4 | u-boot-env | `0x030000` | `0x010000` | 보존 |
| 5 | factory | `0x040000` | `0x010000` | 보존 |
| 1 | kernel | `0x050000` | `0x200000` | firmware 영역 |
| 2 | rootfs | `0x250000` | `0x7d0000` | firmware 영역 |
| 6 | data | `0xa20000` | `0x5d0000` | 보존 |
| 7 | config | `0xff0000` | `0x010000` | 보존 |
| 8 | firmware | `0x050000` | `0x9d0000` | 유일한 쓰기 대상 |

`rootfs_data`는 rootfs 안에서 현재 이미지로부터 생성되는 동적 분할이며
새 이미지에서 크기가 달라집니다. `/data`는 `/dev/mtdblock6`의 별도 JFFS2입니다.
새 DTS도 위 이름·offset·size를 유지해야 합니다. 위 MTD 번호는 당시 관측값이며
첫 부팅과 복구에서 `/proc/mtd`의 이름으로 번호를 다시 찾습니다. 실행 장치의
`/lib/upgrade/platform.sh`는 `PART_NAME=firmware`와 default upgrade 경로를
사용합니다. `/lib/upgrade/common.sh`의 `default_do_upgrade`는 해당 이름의
MTD에만 image를 씁니다. bootloader/env/factory/data/config 쓰기는 필요하지 않습니다.

소스 기준은 `target/linux/ramips/dts/mt7628an_wemo-matter-bridge.dts`와
`target/linux/ramips/image/mt76x8.mk`의 `Device/wemo-matter-bridge`입니다.
kernel slot은 2048 KiB, image budget은 10048 KiB입니다. 새 kernel과 DTB,
rootfs와 metadata를 분리해 확인하고 target에서 `sysupgrade -T`를 통과해야
합니다. `-F`, `-n`, 다른 profile 또는 partition resize로 검증 실패를 넘기지 않습니다.

## 기본 sysupgrade 백업의 /data 중복 문제

운영 `keep.d/openwemo-bridge-core`와 `keep.d/wemo-matter-bridge` 모두
`/data/wemo-matter/`를 포함합니다. 당시 `sysupgrade -l`은 data 항목 226개,
그 외 항목 26개를 열거했습니다. 일부 data 파일은 오래된 임시 KVS입니다.

실제 `/lib/preinit/80_mount_root`는 root overlay를 준비한 뒤 config archive를
추출합니다. 별도 `/data`는 나중에 `S12wemo_mtd_data`에서 mount됩니다.
따라서 기본 백업을 그대로 복원하면 fabric/DB가 새 root overlay에도 복사되고,
나중에 실제 data mount로 가려집니다. 작은 overlay에 불필요한 private 자료와
오래된 상태가 생기므로 이번에는 **/data가 없는 검토된 config archive를
`sysupgrade -f`로 지정**합니다. 별도 data 분할의 내용을 그대로 유지하고
host에는 별도 복구 백업을 보관합니다.

config archive는 다음 절차로 만듭니다.

1. `sysupgrade -b` 백업을 host private 폴더로 non-PTY SSH 전송합니다.
   archive에 private 값을 출력하지 않습니다.
2. host에서 archive member 이름을 정규화해 `data`와 `data/` 하위 항목만
   제외합니다. 절대 경로, `..`, data로 향하는 위험한 link가 없는지 검사합니다.
   tar member를 stream으로 복사해 원본 numeric uid/gid와 mode를 유지합니다.
   일반 host 사용자로 extract/repack해 root-owned SSH key를 사용자 소유로
   바꾸지 않습니다. 추가 파일도 target capture의 원래 owner/mode를 사용하고,
   BusyBox 호환 tar format과 full gzip/tar 읽기를 확인합니다.
3. 기존 `/etc/config`(network/firewall/WireGuard 포함), Dropbear host keys와
   authorized keys, 계정 파일, `/etc/wemo_ctrl.conf`,
   `/etc/wemo-matter-bridge/matter-onboarding.json`을 보존합니다. 기본 archive에
   없는 필수 파일은 explicit allowlist로 추가합니다. onboarding JSON은
   새로 생성하지 않습니다.
   실제 운영 장치에는 `/etc/dropbear/authorized_keys`와
   `/root/.ssh/authorized_keys`가 모두 없었습니다. 기존 image의 root 인증
   설정을 유지하며 이번 센서 업데이트 때문에 인증 방식을 변경하지 않습니다.
   따라서 authorized keys는 있을 때만 보존하는 항목이고 필수 파일은 아닙니다.
4. `sysupgrade -b`가 생성하는 `etc/uci-defaults/10_disable_services`를
   보존해 기존 disabled 서비스 상태를 유지합니다. archive에는 옛 실행
   바이너리나 옛 init/watchdog script를 추가하지 않습니다. 별도로 보관한
   전체 etc tar는 복구 자료이며 upgrade config archive가 아닙니다.
   최초 boot에서 app이 보존 상태를 쓰기 전에 검사할 수 있도록 새 일회성
   `etc/uci-defaults/09-netcam-hold-services`를 archive에 추가합니다.
   내용은 아래 세 서비스의 `disable`만 수행하고 모두 성공했을 때 0을
   반환합니다. archive mode는 0755로 하고 `10_disable_services`보다 먼저
   실행됨을 확인합니다. 이는 새 서비스의 자동 시작을 잠시 보류합니다.

   ```sh
   #!/bin/sh
   /etc/init.d/wemo_ctrl disable || exit 1
   /etc/init.d/wemo-matter-bridge disable || exit 1
   /etc/init.d/wemo-matter-bridge-watchdog disable || exit 1
   exit 0
   ```

   다른 기존 disabled 서비스는 `10_disable_services`가 그대로 처리합니다.
   이 세 서비스의 기존 enable 상태도 private manifest에 기록해, 첫 부팅
   검사 후 원래 enabled였던 서비스만 다시 enable합니다. 일회성 script는
   성공 시 OpenWrt uci-defaults 처리에 의해 제거되지만 service disable
   상태는 수동 복구 전까지 지속됩니다. image의 boot 경로가 defaults 실행
   전에 이 세 daemon을 실행하지 않는지도 확인해야 합니다.
   현재 장치에서는 `/etc/init.d/boot`가 START10에서 정렬된 defaults를 실행하고
   `/etc/rc.common disable`이 해당 S/K symlink를 제거하는 것을 읽어 확인했습니다.
   새 image에서도 같은 순서인지 확인하며, 실제 first-boot hold의 성공은
   설치 뒤 process와 enable 상태 확인으로 기록합니다.
5. 결과 archive를 host/target에서 SHA-256 대조하고 `/tmp`에 둡니다.
   target `sysupgrade -T -f /tmp/bridge-config-curated.tar.gz /tmp/bridge.bin`을
   실행합니다. 현재 sysupgrade는 test 때 custom archive의 gzip magic을
   검사하지만 내부 member 안전성까지 보증하지 않으므로 host 검토가 필요합니다.

`/data`가 mount되지 않으면 새 controller/app을 시작하지 않습니다.
현재 `wemo_mtd_data`는 data partition이 없을 때 writable `/data`로 fallback하므로
service 시작 성공만으로 실제 data mount를 보증할 수 없습니다.

host helper는 위 archive 필터링과 hold script 삽입을 재현합니다. 입력과 출력은
private 폴더에 두고 출력 파일은 매번 새 이름을 사용합니다.

```sh
python3 scripts/curate_bridge_sysupgrade_backup.py \
  private/<deployment>/sysupgrade-original.tgz \
  private/<deployment>/bridge-config-curated.tar.gz \
  --additional-etc private/<deployment>/etc-private.tar.gz
python3 -m unittest discover -s scripts/tests \
  -p test_curate_bridge_sysupgrade_backup.py
```

`--additional-etc`는 기본 backup에 없는 `etc/wemo_ctrl.conf`,
`etc/dropbear/authorized_keys`, `etc/fw_env.config`만 추가합니다. 원본의 기존
파일을 덮어쓰지 않습니다. 필수 설정·보호된 root 소유 SSH host key·기존
disable-services script가 없거나 위험한 tar path/link/node가 있으면 실패합니다.
source의 numeric owner/mode를 유지하고 USTAR를 생성하며, 출력은 private 0700
디렉터리에 exclusive·atomic 방식으로 0600을 사용합니다. 출력은 member 수만
표시하며 설정 내용을 출력하지 않습니다. fixture 테스트는 실제 key/fabric을
사용하지 않습니다. 이 helper 성공만으로 target flash 또는 first boot를
검증했다고 판단하지 않습니다.
named owner가 numeric owner를 덮어쓰지 않도록 출력 tar의 uname/gname은
비웁니다. 모든 포함된 SSH host key와 directory의 안전한 owner/mode를
검사하며, archived link를 경유해 `..` 의미가 달라지는 symlink chain도
차단합니다. authorized keys는 존재할 때 protected root-owned regular file인지
검사합니다.

실제 사전 `sysupgrade-original.tar.gz`와 `etc-private.tar.gz`에 helper를
적용한 host dryrun은 data 226 members를 제외하고 etc 29 members와 hold
script 1개를 보존했습니다. 모든 출력 member의 numeric owner가 root이며
hold mode 0755, archive mode 0600과 두 defaults script 포함을 확인했습니다.
host BusyBox의 archive 읽기도 통과했습니다. 이 사전 archive는 target에
전송하지 않았으며 flash 직전 final archive를 별도로 생성해야 합니다.

## 보관한 사전 백업과 flash 직전 백업

읽기 전용 사전 보관 위치:

`private/netcam-firmware-upgrade-2026-10-09/preflight/`

디렉터리 0700, 파일 0600, Git 제외입니다. 다음 자료를 실제로 보관했습니다.

- `mtd0-u-boot.bin`, `mtd4-u-boot-env.bin`, `mtd5-factory.bin`,
  `mtd7-config.bin`, `mtd8-firmware.bin`: non-PTY SSH로 `/dev/mtdNro`를 읽어
  정확한 partition size와 host/target SHA-256 일치를 확인했습니다.
  firmware dump 크기는 10289152 bytes입니다.
- `etc-private.tar.gz`: 설정, host keys, onboarding, init/enable 상태의 복구 자료.
- `data-live-provisional.tar.gz`: 실행 중 data 전체 tar, 229 members.
  **실행 중 여러 파일 간 일관성을 보증하지 않는 임시 백업**입니다.
  설치/rollback에서 이 오래된 fabric snapshot을 자동 복원하지 않습니다.
  data 복구가 필요하면 final stopped-state 백업을 사용합니다.
- `sqlite-wemo_device.db`, `sqlite-wemo_state.db`, `sqlite-wemo.db`:
  target SQLite `.backup` API로 생성하고 `PRAGMA quick_check`의 `ok`를 확인했습니다.
  크기는 각각 16384, 24576, 4096 bytes입니다. target `/tmp` 임시 복사본은
  전송 후 제거했습니다. `sensor_endpoints.db`는 당시 없었습니다.
- `mtd-backup-manifest.json`, `backup-manifest.json`: private size/SHA 기록.

실제 flash 직전에 운영 담당 세션이 다음을 추가해야 합니다.

1. 정확한 장치, partition/mount, 기존 package versions, host keys, 이미지 해시,
   현재 free RAM/tmp 공간과 복구 경로를 재확인합니다. 이미지+archive+RAMFS
   필요 공간을 확보해야 합니다. 사전 관측 `/tmp` 여유는 28860 KiB였습니다.
2. 기존 endpoint ID/type/unique identity 기준을 private 기록에 확보합니다.
   Google Home 화면만으로 actual endpoint ID를 확인했다고 쓰지 않습니다.
   DB/allocator로 대체 기준을 사용할 경우 실제 운영 mapping과의 근거를 기록합니다.
3. watchdog → Matter app → controller 순으로 stop하고 종료를 확인합니다.
   전원이 연결된 Mini 008은 조작하지 않습니다.
   먼저 실행 app의 실제 KVS 경로와 config/counter/factory 파일 위치를
   private 기록에서 확인합니다. 현재 source는 `--KVS /data/wemo-matter/chip/chip_kvs`,
   compile-time state dir도 같은 경로이며 `/tmp/chip_*.ini`는 data 파일로
   symlink합니다. 실제 process/build도 이 설정과 같은지 확인해야 합니다.
   procd/cron/hotplug에 의한 writer 재시작 가능성이 없어야 하고, image 검사
   실패로 flash가 시작되지 않으면 기존 서비스 순서로 재개할 수 있어야 합니다.
4. 모든 data writer가 정지했음을 확인한 뒤 final data tar를 host에 전송합니다.
   SQLite WAL/SHM 존재를 확인하고 `.backup` API와 quick_check를 다시 수행합니다.
   기존 sensor DB가 존재하면 함께 보관합니다. CHIP/fabric/onboarding 경로의
   파일 목록·hash와 config/enable 상태를 private manifest에 기록합니다.
   파일 내용을 로그에 출력하지 않습니다.
   SQLite를 열면서 WAL checkpoint 또는 sidecar 삭제가 발생할 수 있으므로
   file-level snapshot을 먼저 보관하고, SQLite backup/check 완료 후 baseline
   hash를 다시 기록합니다. pre-start 비교는 그 최종 baseline을 사용합니다.
   curated config archive도 final 시점에서 다시 만들어 preflight 이후 변경된
   UCI/enable 상태를 잃지 않도록 합니다.
5. raw data flash backup이 필요하면 stopped-state snapshot으로 보관합니다.
   mounted JFFS2를 읽은 live raw dump를 일관성 있는 복구 snapshot으로
   간주하지 않습니다. unmount가 필요한 경우 별도 정지 순서를 확인합니다.
6. 새 image와 curated archive를 최종 확인한 뒤 운영 담당 세션에서
   `sysupgrade -f /tmp/bridge-config-curated.tar.gz /tmp/bridge.bin`을 실행합니다.
   이 문서는 해당 명령 실행의 증거가 아닙니다.

## 복구 준비와 현재 확인 한계

운영 kernel command line과 보관한 U-Boot env에서 serial console
`ttyS0,57600n8`, `baudrate=57600`, `bootdelay=1`, stdin/stdout/stderr serial을
확인했습니다. 8N1, flow control 없음으로 사용합니다. 기존 NetCam의
Kermit 57600 절차를 이 MT7628 bridge에 그대로 적용하지 않습니다.

뒤이어 운영 담당 main 세션이 사용자가 확인한 USB2를 57600으로 접속해
실제 bridge U-Boot 응답을 확인했습니다. 현재 OS의 `/dev/ttyUSB2`를
사용했으며 다른 세션의 옛 USB 번호와 혼동하지 않습니다. 재접속 시에는
USB 번호와 console 대상의 board/boot 증거를 다시 확인합니다.

boot menu 4에서 U-Boot CLI와 `help`를 확인했고 `bootm`, `tftpboot`,
`erase`, `cp`, `spi`, `reset` 명령이 존재했습니다. 실제 `printenv`에서
`boot_A_adr=0xBC050000`, boot A arguments는 `console=ttyS0,57600n8
root=/dev/mtdblock2`, `bootstate=0`으로 확인했습니다. main 세션은 `reset`으로
기존 kernel의 부팅과 서비스/SSH 복귀 및 data mount를 확인했습니다.
serial 증거는 private에 보관합니다. 이 단계는 transfer나 flash 명령을
실행해 복구 전체 경로를 검증한 시험이 아닙니다.

실제 transfer protocol, RAM load 주소, 크기·명령 syntax에 따른 erase/write
순서는 아직 시험하지 않았습니다. flash 전에 이 bootloader의 명령 사용법과
안전한 범위를 다시 확인하며 NetCam Kermit 절차를 적용하지 않습니다.
vendor dual-slot fallback을 복구 수단으로 선언하지 않습니다. 새 DTS는
vendor dual-slot layout과 다르므로 임의 bootB fallback은 금지합니다.

### 실제 bootloader 코드와 RAM boot의 명령 주의

host의 별도 `../uboot-snsv2/u-boot` ELF 및 `System.map`을 읽어 보관한
`private/netcam-firmware-upgrade-2026-10-09/preflight/mtd0-u-boot.bin`과 대조했습니다.
`.text` 76320 bytes, `.data` 752 bytes, `.data.rel.local` 1964 bytes,
`.got` 1056 bytes, command table 456 bytes는 모두 일치했습니다. 두 rodata
section에는 각각 9 bytes 차이가 있어 ELF 전체 동일성은 주장하지 않습니다.
아래 분기에 쓰이는 `run_command` GOT 주소와 `save`,
`setenv bootargs ${boot_A_args}` literal은 actual dump에서도 확인했습니다.
별도 U-Boot repo의 소스·빌드 산출물은 수정하지 않았습니다.

`do_bootm`은 `0xBC00A5F4`에 있고, `argc < 2` 검사는 `0xBC00A678`의
`slti`와 `0xBC00A67C`의 branch입니다. 인자 없는 경로는 global `load_addr`를
읽고 `0xBC00A688`에서 `0xBC00A974`로 이동합니다. 반면 **인자가 있는
`bootm ADDRESS`는 bootstate 0/1일 때 ADDRESS를 boot A flash 주소로
덮어쓰며**, bootargs가 다르면 boot A args로 교체한 뒤 `save`도 실행합니다.
일반 U-Boot의 `bootm RAM_ADDRESS` 예제를 이 장치에 그대로 사용하지 않습니다.

인자 없는 분기는 네 번의 `run_command` 호출이 있는 인자 처리 영역을
우회합니다. 뒤에 호출되는 `check_boot_process`의 실제 opcode도 확인했습니다.
`0xBC00A1A4/0xBC00A1AC`에서 bootstate 문자 `1`/`3`만 변경 경로로 들어가며
문자 `0`은 `0xBC00A228` return으로 이동합니다. `do_bootm`에 별도 direct
`saveenv` 호출은 없습니다. 따라서 **bootstate 0 확인 + 인자 없는 bootm**은
코드 분석상 이 env 쓰기 경로를 우회합니다. 이는 아직 실제 RAM 부팅을
성공시킨 검증이 아닙니다.

`tftpboot ADDRESS FILE`은 global `load_addr`를 ADDRESS로 지정합니다.
그러나 TFTP 함수에는 autostart=yes이고 modifies 조건이 참이면 `saveenv`를
호출하는 코드가 있습니다(actual GOT call `0xBC00BFB0`). 따라서 다운로드
전에 `autostart=no`, `autoscript=no`를 **RAM에서만** 설정합니다.
`save`, `saveenv`, menu의 flash update 항목, `erase`, `cp`, `spi write`는
RAM/transfer 검증에서 실행하지 않습니다. 테스트 후 정상 reset으로 기존
flash 부팅을 재개하고 host에 보관한 protected env hash를 반드시 대조합니다.

### full initramfs의 64 MiB RAM 예산 검사

actual serial은 DRAM 64 MB를 보고했습니다. KSEG0 RAM 범위는 이 장치에서
`0x80000000..0x83ffffff`이며 RAM top은 exclusive `0x84000000`입니다.
OpenWrt ramips pipeline은 kernel+DTB를 LZMA로 압축한 legacy uImage,
load address `0x80000000`을 생성합니다. 현재 full build는 embedded initramfs도
LZMA를 사용하므로 outer kernel 압축과 inner cpio 압축을 구분해야 합니다.
**압축 파일 크기만으로 RAM 부팅 가능하다고 판단하지 않습니다.**

완성된 image에 대해 다음 자료를 host에서 읽어 기록합니다.

1. legacy uImage header magic, header CRC, payload CRC, arch/type/compression,
   payload size, load `L`, entry address를 확인합니다. `mkimage -l` 또는 host
   header parser를 사용하며 `sysupgrade.bin`을 initramfs kernel로 혼동하지 않습니다.
2. payload를 host에서만 decompress해 실제 outer 출력 길이 `U`를 구하고
   LZMA header의 알려진 출력 길이와 비교합니다. 이 vendor decoder는 unknown
   length `0xffffffffffffffff`를 거부합니다. 기본 Python `FORMAT_ALONE` 압축
   산출물을 검토 없이 대신 쓰지 않습니다. OpenWrt의 `staging_dir/host/bin/lzma`
   pipeline과 실제 header를 사용합니다.
3. 대응하는 `vmlinux-initramfs`의 `readelf -lW` LOAD memory 범위 및
   `_end`/BSS 범위를 읽습니다. `E`는 `L+U`, ELF LOAD의 memory end, kernel
   end 중 가장 큰 RAM 주소로 잡아, decompressed bytes 외의 BSS 초기화가
   staging buffer를 덮는 것도 차단합니다.
4. UART에서 `bdinfo`가 제공하면 memstart/memsize/boot_params를 private에
   읽어 U-Boot relocation, malloc, boot parameters와 stack의 상단 점유를
   확인합니다. 소스는 RAM top에서 code, malloc, 128 KiB boot parameters,
   stack을 예약합니다. RAM top 전체를 TFTP용 free memory로 사용하지 않습니다.
5. `S`를 TFTP staging 시작, `C`를 header 포함 image 총 길이,
   `T`를 검토한 U-Boot 상단 예약보다 낮은 안전한 exclusive limit으로 잡습니다.
   `L < E < S`, `S+C <= T < 0x84000000`을 충분한 alignment/guard와 함께
   만족해야 합니다. `0x82000000`은 검토 후보이지 무조건 안전한 주소가 아닙니다.
   기본 vendor load `0x80100000`은 outer decompression과 겹칠 수 있으므로
   RAM kernel에 사용하지 않습니다.
6. inner cpio를 host에서만 decompress해 regular-file bytes와 각 파일의
   page rounding, rootfs metadata, kernel runtime/allocator/page tables 여유를
   추가 산정합니다. peak에는 embedded compressed cpio가 든 kernel과
   새로 풀린 rootfs가 동시에 존재합니다. 실제 shell boot 뒤 `/proc/meminfo`의
   확인 전에는 계산만으로 운영 안정성을 주장하지 않습니다.

OpenWrt generic `/init`은 root 전체를 새 tmpfs로 복사한 후 procd를 실행합니다.
이 시험에서는 **`rdinit=/bin/sh init=/bin/sh root=/dev/ram0`**를 temporary
bootargs에 사용해 그 copy와 daemon 시작을 우회합니다. kernel 6.12.74
`init/main.c`를 읽어 명시적 `init` 실패는 panic으로 끝나는 것을 확인했습니다.
rdinit만 지정하면 shell 파일은 존재하지만 dynamic loader가 실패하는 경우
`/sbin/init` fallback이 생길 수 있으므로 두 shell 옵션을 함께 사용합니다.
기존 `root=/dev/mtdblock2`를 그대로 남기지 않습니다. host에서 `/bin/sh`,
BusyBox와 실제 ELF interpreter/shared libraries, cpio console node를 확인하고
kernel의 CMDLINE override가 temporary args를 무시하지 않는지도 확인합니다.

검토가 통과한 뒤 사용할 UART 순서는 다음입니다. 아래 주소는 위 예산 조건을
통과한 경우의 예시이며 이 문서를 쓰면서 실행하지 않았습니다.

```text
printenv bootstate
setenv autostart no
setenv autoscript no
setenv bootargs console=ttyS0,57600n8 rdinit=/bin/sh init=/bin/sh root=/dev/ram0
tftpboot 0x82000000 recovery-initramfs.bin
# 전송 크기와 image/header/CRC를 확인한 뒤에만, ADDRESS 인자 없이:
bootm
```

`bootstate`가 0이 아니면 진행하지 않습니다. `ipaddr`/`serverip`/netmask 설정도
필요하면 volatile 값만 사용하고 private 기록에만 둡니다. PID 1 shell에서는
proc/sysfs의 필요한 읽기 경로만 마련하고 `/data`나 flash root를 mount하지
않습니다. `/init`, `/sbin/init`, controller/app, provisioning, sysupgrade를
실행하지 않습니다. 실제 `/proc/mounts`와 process 상태로 이 조건을 확인해야 합니다.

full image가 overlap/peak RAM 조건을 통과하지 못하면 현재 full-build config,
staging/build directory를 변경하지 않습니다. kernel 빌드가 끝난 뒤 별도
kernel source/output 복사본과 minimal cpio(BusyBox shell, 필요한 loader/libraries,
console node만 포함)를 사용해 별도 image를 만들 수 있습니다. 같은 DTS를
사용하고 위 image/ELF 검사를 다시 수행합니다. 현재 kernel tree를 복사하는
동안 full build가 해당 파일을 쓰는 방식이나 공유 output에 두 make를 동시에
실행하는 방식은 사용하지 않습니다. minimal image도 아직 만들지 않았습니다.

### 먼저 가능한 transfer-only 확인

RAM image를 boot하지 않고 작은 공개 fixture를 TFTP로 RAM에 받는 시험이
가능합니다. 별도 TFTP root에는 그 fixture만 두고 private 백업·keys·DB는
포함하지 않습니다. PC dnsmasq는 별도 process에서 `--conf-file=/dev/null`,
`--port=0`, `--enable-tftp`, eth0의 확인한 LAN 주소/전용 TFTP root만 지정하고
DHCP range는 설정하지 않습니다. 기존 DNS/DHCP 서비스는 변경하지 않습니다.

U-Boot에서 bootstate 0, autostart/autoscript no를 먼저 확인하고, bounds 내의
명시적 RAM 주소로 fixture를 받습니다. 실제 `crc32`가 지원되면 host CRC32와
비교하고 아니면 알려진 짧은 marker를 `md.b`로 확인합니다. 이 명령은 RAM만
읽습니다. **이 시험에서는 bootm도 실행하지 않습니다.** 정상 reset 복귀와
protected partition/env hashes를 확인한 뒤 fixture server를 종료합니다.
성공 시에는 TFTP 전송+기존 flash 재부팅을 검증했다고 기록하며, kernel RAM
부팅이나 flash write 복구를 검증했다고 확대하지 않습니다.

보관한 raw `mtd8-firmware.bin`은 전송 size/SHA 일치를 확인한 dump입니다.
immutable kernel/squashfs는 보존됐으나 mounted JFFS2 overlay의 파일 간
일관성은 보증하지 않습니다. 일관된 raw restore point가 필요하면 stopped-state
덤프를 추가합니다. **표준 metadata가 있는 sysupgrade 이미지라고 간주하지 않습니다.**
이를 `sysupgrade -F`로 강제 설치하지 않습니다. bootloader/SPI 복구 시에는
확인된 명령과 geometry로 `0x050000..0xa1ffff` 범위만 복원하는 방법을
검증해야 합니다. bootloader/env/factory/data/config 영역은 보존합니다.
SSH가 살아 있는 rollback에는 검증한 이전 버전의 matching sysupgrade 이미지와
curated config archive를 우선 준비합니다. 이전/새 image의 board metadata와
compat version을 대조해 새 sysupgrade에서도 force 없이 이전 image를
수락하는지 확인합니다. rollback config는 설치 전 final curated archive를
사용하고, 새 image가 변경한 설정과 data를 다시 기본 백업하지 않습니다.
fabric 재commissioning, factory reset,
CHIP 파일 삭제로 복구하지 않습니다.

## 첫 부팅과 수락

첫 부팅 후 이름으로 찾은 data MTD의 실제 JFFS2 mount, protected partition hashes,
onboarding/fabric 초기화가 없었음, 기존 network/SSH identity, installed paired
core/Matter versions와 IPCv3를 확인합니다. overlay free 공간과 OOM/restart도
확인합니다. 최초 SSH 연결에서는 세 daemon이 실제로 정지돼 있고 자동 시작
보류가 적용됐는지 먼저 확인합니다. app이 시작됐다면 예상한 pre-start hash
검증이 성립했다고 쓰지 않습니다. 초기 mount/파일 hash 확인은 app이 persistent state를 쓰기 전
시점의 기준과 비교해야 하며, 정상적인 runtime DB/KVS counter 변경을
fabric 교체로 오판하지 않습니다.

검사가 모두 통과한 뒤 기존 manifest에서 enabled였던 세 서비스만 enable하고
controller → Matter app → watchdog 순서로 수동 시작합니다. 기존 disabled
서비스를 새 기능 때문에 임의로 켜지 않습니다. controller IPCv3 readiness,
app 기존 fabric resume를 각각 확인한 뒤 다음 서비스를 시작합니다.

기존 플러그·조명 endpoint와 Google Home 연결/작동을 먼저 확인하고 새 NetCam
Occupancy/Reachable/HD pause/resume를 검증합니다. 상세 센서 기대값은
[NETCAM_SENSOR_DEPLOYMENT.md](NETCAM_SENSOR_DEPLOYMENT.md)에 있습니다.
Google Home에서 실제 센서 수락 전에는 end-to-end 완료를 주장하지 않습니다.
일반 사용자 대상 릴리스 안내에는 retired NetCam 기능을 노출하지 않습니다.

완료된 config/이미지/설치 검증 milestone마다 source·재현 설정·문서를
commit/push하고 remote 반영을 확인합니다. private 백업, 이미지, keys, 실제
device identity/fabric/화면은 커밋하지 않습니다.

## 독립 검토

Claude CLI에 이 문서 초안을 stdin으로 제공하고 tools를 비활성화한 검토를
수행했습니다. archive ownership, 최초 app 자동 시작, MTD 번호 변동, SQLite
checkpoint와 live raw overlay 일관성, rollback metadata 준비에 관한 지적을
절차에 반영했습니다. 이 검토는 문서 기반 추론이며 별도 hardware 검증이나
flash 성공의 증거가 아닙니다. 상세 결과는 private preflight 폴더에 보관합니다.
별도로 helper 소스·fixture를 같은 방식으로 검토했고 symlink chain,
numeric/name owner 차이, directory permissions, 모든 host key 검증과
손상 gzip 오류 처리를 보완했습니다. 실제 fixture suite 결과는 commit
milestone 기록과 함께 보고합니다.
