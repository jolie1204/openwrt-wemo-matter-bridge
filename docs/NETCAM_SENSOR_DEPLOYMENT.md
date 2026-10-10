# NetCam 센서 운영 배포 기록

2026-10-09 기준 개발자용 기록입니다. 기존 플러그·조명의 commissioning과
fabric을 유지해야 합니다. 일반 사용자용 릴리스 안내에 추가하지 않습니다.

## 작업과 자료 위치

- 작업 checkout: 기존 `openwrt-wemo-matter-bridge`, branch `main`.
- 최초 개발 기준: `8f16ca0916`, 최초 개발 branch `codex/netcam-sensor`.
- 새/롤백 APK 로컬 보관: `private/netcam-deployment-2026-10-09/packages/`.
- 같은 자료 폴더의 `SHA256SUMS`, `artifacts.json`: 파일별 크기·해시·용도.
- `evidence/`: 소스 해시, SDK 설정, 변경 파일 목록, downgrade fixture 및 ABI 검증.
- 기존 상세 검토: 부모 workspace의
  `.build/netcam-sensor-evidence/integration-and-deployment.md`.
- 새 APK 빌드 SDK: 부모 workspace의 `.build/netcam-sensor-sdk/`.
- 기존 SDK와 롤백 후보 원본: 부모 workspace의
  `.build/openwrt-sdk-25.12.2-ramips-mt76x8_gcc-14.3.0_musl.Linux-x86_64/`.

`private/`는 Git에서 제외하며 디렉터리 0700, 파일 0600으로 보관합니다.
작업 트리를 다른 PC로 옮길 때 이 자료는 별도로 안전하게 복사해야 합니다.
이번 보관에는 실제 fabric/인증 키/DB/사용자 화면을 복사하지 않았습니다.
최초 증거 JSON의 옛 checkout 경로는 검증 이력이며 삭제된 checkout을
현재 소스 경로로 사용하지 마세요.

## 현재 검증 상태

새 core r22 / Matter r24 full firmware의 clean build와 실제 운영 장치
설치를 완료했습니다. first-boot hold가 실제로 적용됐고, service 시작 전
기존 data 파일 전체·onboarding·보호 분할의 hash 일치를 확인했습니다.
빌드/이미지와 UART 복구·설치 절차는
[펌웨어 빌드](NETCAM_BRIDGE_FIRMWARE_BUILD.md) 및
[펌웨어 설치](NETCAM_BRIDGE_FIRMWARE_UPGRADE.md)에 기록했습니다.

원래 장기 실행 UniqueID 14개를 담은 endpoint_identity.db를 root 0600으로
설치해 해시와 SQLite integrity를 검증하고 원래 service enable 상태를
복원했습니다. 새 실행 ELF와 24 allocated sections를 대조한 read-only
메모리 캡처 두 번이 같았으며 원래 14개 UID/ID/type/revision/parent와 exact
canonical UDN 소유권을 모두 보존했습니다. 전체 registered endpoint 17개도
같았습니다. fabric이 재개됐으며 재commissioning은 하지 않았습니다.

NetCam 신규 발견과 Google Home 실제 제어, 재시작 후 identity 유지는
별도로 검증 중입니다. 최초 실행 시에는 기존 14개만 발견돼 camera UPnP
경로를 확인하고 있습니다. 아래 APK와 미설치 서술은 이전 단계의 이력입니다.

### 이전 SDK package 검증 이력

controller r22와 Matter r23은 IPCv3로 함께 빌드됐습니다. host suite 5개,
paired target clean build, copied-source 비교 및 실제 musl loader 검증은
통과했습니다. 실행 중인 운영 패키지 교체와 Google Home 새 센서 수락은
아직 수행하지 않았습니다.

| 용도 | APK | bytes | SHA-256 |
| --- | --- | ---: | --- |
| 새 controller | `openwemo-bridge-core-0.1-r22.apk` | 84101 | `eeebb612bf8bbd502784f0824feda63d6cd141142123bcbd868f59118e387799` |
| 새 Matter | `wemo-matter-bridge-2025.12.01~8effa808-r23.apk` | 535997 | `20e4c4daa0755b0c2a2a9cd4ffab5115257c1774e3cf11fc77e5b8dfa607206f` |
| 롤백 controller 후보 | `openwemo-bridge-core-0.1-r21.apk` | 1167255 | `cc9a8c29f1bb244f2325682fe4e187ae537a324016157b8adc67feb093b379a4` |
| 롤백 Matter 후보 | `wemo-matter-bridge-2025.12.01~8effa808-r22.apk` | 531695 | `79796fa76ed8813ebbde2c0650ed10e21dbf5329a29d2cc2035c58b71efdb941` |

전부 `mipsel_24kc`의 local 개발 APK입니다. 롤백 파일은 기존 SDK에서
복사·해시 대조했지만, 실제 설치된 바이너리와의 동일성은 배포 전에
추가로 확인해야 합니다. 같은 버전 이름만으로 동일하다고 판단하지 않습니다.

### 운영 재확인 및 펌웨어 경로 선택

2026-10-09 후속 작업에서 strict SSH로 현재 운영 r21/r22와 overlay 여유
748 KiB를 다시 확인했습니다. host suite 5개도 현재 main에서 재실행해
통과했습니다. 두 롤백 APK를 host에서 추출하고, `wemo_ctrl`, `wemo_client`,
`libwemoengine.so`, `wemo-bridge-app`의 SHA-256을 실제 운영 파일과
대조해 **4개 모두 일치**했습니다. SDK staging 파일은 strip 이전이므로
롤백 APK payload를 비교 대상으로 사용해야 합니다. package 단위 rollback
실행 자체는 여전히 미시험입니다.

사용자가 새 브리지 펌웨어 이미지 준비와 로컬 운영 장치 설치를 승인했습니다.
새 controller/Matter를 squashfs에 포함하는 16M 이미지로 설치 공간 문제를
해결하는 경로를 준비합니다. 빌드 seed는
`configs/wemo-matter-bridge-16m.config`, feed revision은
`configs/wemo-matter-bridge.feeds.conf`에 저장합니다. `make defconfig`에서
16M 단일 target, 별도 initramfs, 센서 패키지, WireGuard 및 Wi-Fi 제외를
확인했습니다. 전체 이미지 빌드/flash/Google Home 수락은 아직 수행 전입니다.

기본 sysupgrade keep 목록이 `/data/wemo-matter/`까지 포함하므로,
별도 data 파티션을 유지하는 이번 경로에서는 복원 archive에서 `/data`를
제외해야 합니다. `/etc`의 필수 설정 및 키는 별도로 보존하고, Matter 상태와
DB의 일관성 있는 비공개 복구 백업은 host에 보관합니다. 기본 백업으로
data 내용을 overlay에 중복 복원하지 않습니다. 정확한 파티션/이미지 검증과
복구 경로 확인 후 설치합니다.

```sh
cd private/netcam-deployment-2026-10-09
sha256sum -c SHA256SUMS
```

## 아직 충족하지 못한 운영 배포 조건

1. **설치 공간**: 당시 overlay 여유 765952 bytes(748 KiB).
   최종 두 패키지 논리 payload는 1557283 bytes이며 4 KiB별 zlib+inode
   추정은 748042 bytes, 차이는 17910 bytes뿐입니다. 이는 JFFS2 실제
   할당/GC/transaction peak의 보장이 아닙니다. 기존 upper Matter 바이너리
   교체 시 회수되는 공간과 DB 증가·롤백 여유까지 실제로 검토해야 합니다.
   설치 공간 대책이 없으면 `apk add`를 실행하지 않습니다.
   `/data`로 바이너리 이동, 파일/패키지 삭제, 브리지 FW flash를 자동 선택하지
   말고 구체적인 대책과 복구 방법을 별도로 검토합니다.
2. **기존 endpoint 기준값**: 2026-10-09 운영 서버의 실제 runtime 기준을
   `private/netcam-deployment-2026-10-09/evidence/runtime-endpoints/baseline.json`에
   확보했습니다. 운영 ELF와 기존 unstripped ELF의 24개 SHF_ALLOC section
   바이트를 대조하고, 해당 ELF의 symbol/DWARF layout과 ASLR base를 사용해
   root의 `/proc/PID/mem`을 읽었습니다. ptrace attach, 서비스 정지/재시작,
   commissioning 또는 전원 제어를 하지 않았습니다. 두 연속 snapshot은
   같았고 PID도 유지됐습니다. 고정 endpoint는 0/1/2, 기존 동적 endpoint는
   3~16의 14개입니다. 각 device object의 ID/parent와 실제 등록 table이
   일치하며 UniqueID도 모두 확보했습니다. 기기 이름/UniqueID는 private
   파일에만 보관합니다.

   이것은 **commissioned 관리자의 Descriptor PartsList 읽기가 아닙니다**.
   Google Home의 캐시/표시/사용 가능성을 증명하지 않으며, 실행을 정지하지
   않은 반복 memory snapshot입니다. 별도 admin reader는 여전히 없습니다.
   배포 gate는 이 실제 서버 기준과 flash 후 동일한 방법으로 읽은 등록 상태를
   비교하고, Google Home에서 기존 기기의 표시와 실제 기능을 확인하는 것으로
   구체화합니다. 기존 14개의 endpoint ID, device type 목록/revision,
   UniqueID, parent가 모두 같아야 합니다. 새로운 NetCam endpoint가 추가되어도
   기존 mapping 변경을 허용하지 않습니다. 값이 다르면 증거를 보존하고
   원인을 확인하거나 복구하며, 재commissioning으로 통과시키지 않습니다.

   private helper는 다음과 같이 기존 운영 ELF에 한해 재실행할 수 있습니다.

   ```sh
   python3 private/netcam-deployment-2026-10-09/evidence/runtime-endpoints/read_baseline.py
   ```

   **현재 helper는 기존 운영 build 전용**입니다. `EXPECTED`, `DEBUG`,
   gDevices/emAfEndpoints symbol 주소, table 개수 및 object offset을 고정했고,
   출력은 `baseline.json`을 덮어씁니다. 최초 기준을 별도 private 파일로
   보존한 뒤 재실행합니다. 새 FW에는 그대로 실행하지 않습니다. 새 운영
   ELF를 읽기 전용으로 가져와 새 image의 unstripped ELF와 loadable 바이트,
   program-header 주소/크기, symbols 및 DWARF layout을 다시 검증하고,
   helper의 상수와 별도 결과 경로를 갱신해야 합니다. 주소가 같다고
   가정하거나 기존 debug ELF로 새 프로세스를 해석하지 않습니다. `/proc`
   접근이 거부되면 attach/권한 우회하지 않고 다른 읽기 방법을 검토합니다.

   새 build에는 일반화된 `tools/read_matter_endpoint_baseline.py`를 사용합니다.
   `manifest` 명령에 정확한 unstripped ELF, cross-GDB, 실제 운영 ELF SHA-256과
   새 private 출력 경로를 지정합니다. `capture`에는 같은 ELF/manifest,
   strict SSH host와 별도 private 결과 경로를 지정합니다. 도구는 실행 중
   ELF의 program headers/loadable 바이트, DWARF 주소/크기, ASLR mapping을
   검증하고 두 번의 최소 구조 snapshot과 PID가 동일한지 확인합니다.
   이름/fabric/전체 heap을 읽지 않으며 기존 출력 파일을 덮어쓰지 않습니다.
   `--help`, `manifest --help`, `capture --help`에서 각 인자를 확인합니다.
   기존 private 기준 파일은 비교용으로 그대로 보존합니다.
3. **복구 준비**: 정확한 운영 장치, 패키지 버전, 아키텍처, mount/공간,
   새/롤백 APK의 payload와 전송 해시, 서비스 enable 상태를 다시 확인합니다.
   아래 사전 백업과 실패 시 paired rollback이 준비돼야 합니다.

위 숫자는 2026-10-09 관측입니다. 다시 접속할 때 갱신하고 새로운 결과를
private 배포 기록에 남깁니다. 현재 문서만으로 조건 통과를 주장하지 않습니다.

## 사전 보존

### 기존 UniqueID 보존

2026-10-09 기존 FW를 재부팅한 뒤 실제 서버 등록 상태를 다시 읽었습니다.
기존 동적 endpoint 14개의 ID/type/revision/parent는 모두 같았으나 UniqueID는
모두 바뀌었습니다. 기존 upstream `Device::GenerateUniqueId()`의 무작위 생성
동작입니다. 새 FW 설치로 인한 퇴행으로 해석하지 않습니다.

Matter r24는 모든 WeMo endpoint에 canonical UDN 기반 identity DB
`/data/wemo-matter/endpoint_identity.db`를 사용합니다. UUID scheme만 소문자로
정규화하고 UDN 본문은 보존합니다. 기존 UID를 우선 사용하며, 신규 UID는
SQLite transaction의 COMMIT 성공 후에만 publication에 사용합니다. schema,
UDN/UID 유일성, UID 길이, private 파일 권한, 조회/쓰기 오류는 fail closed로
처리합니다. identity 저장 실패 시 해당 endpoint를 게시하지 않습니다.
운영 daemon은 기존 DB를 READWRITE로만 열고 파일 크기/SQLite header와
모든 seed 행의 TEXT type/canonical UDN/유효 UID를 검사합니다. DB가 삭제되거나
0-byte/손상 상태이면 새 DB를 만들거나 새 UID를 발급하지 않습니다. SQLite
`synchronous=EXTRA`로 COMMIT을 보존하고, 이미 게시한 canonical UDN 또는
UniqueID와 중복되는 새 endpoint도 거부합니다.
기존 endpoint allocator와 fabric은 변경하지 않습니다. DB 경로는 개발용
`WEMO_ENDPOINT_IDENTITY_DB`로 재지정할 수 있으나 실제 운영에서는 `/data`를
마운트한 기본 경로를 사용합니다.

새로운 uncommissioned 설치만 명시적인 offline provisioning을 사용합니다.
다음 도구는 새 private 경로에 strict 빈 DB를 만들어 최초 서비스 시작 전에
복사할 수 있도록 합니다. 기존 파일을 덮어쓰지 않으며 boot 시 자동 호출하지
않습니다. C++의 `EndpointIdentityRegistry::InitializeEmpty`도 최초 provisioning
전용이고 runtime publication 경로에서는 호출하지 않습니다.

```sh
python3 tools/initialize_matter_endpoint_identity_db.py \
  --output private/fresh-provisioning/endpoint_identity.db
```

이미 commissioned된 브리지에서 DB 손실을 이 초기화로 숨기지 않습니다.
보존 DB/검증된 seed를 복구해야 합니다. 이번 운영 배포에는 빈 DB 대신 아래
원래 14개 UID가 들어 있는 migration seed를 사용합니다.

첫 설치에서는 새 daemon을 시작하기 전에 정확한 기존 UID seed를 넣습니다.
private migration 자료는
`private/netcam-firmware-upgrade-2026-10-09/endpoint-identity-migration/`의
`original-identity-seed.json`, `endpoint_identity.db`입니다. 실행 중인
`gBridgedWemoLights.device`와 `gDevices`를 포인터로 정확히 연결한 14개 UDN이
원래 SQLite DB의 canonical UDN과 일치하며, 각 집합에서 유일한 원래/현재/DB의
bounded name, endpoint ID/type/revision/parent도 모두 일치했습니다. 원래
재부팅 전 캡처에 직접 UDN 포인터를 읽지 않았던 한계는 seed JSON에 기록했습니다.
이름 또는 순서만으로 UID를 배정하지 않습니다. 23일 uptime 당시의 원래 UID를
보존했으며, 재부팅 후 새 무작위 UID를 seed로 사용하지 않습니다.

이 seed는 로컬 파일로만 만들었고 이 절을 작성할 때 운영 DB에는 설치하지
않았습니다. 실제 설치 시 모든 관련 서비스를 정지한 상태에서 원본 seed 해시,
DB integrity/schema/14개 binding, 기존 DB 존재 여부 및 백업을 확인한 뒤
0600으로 설치합니다. 기존 identity DB가 있으면 무조건 덮어쓰지 않습니다.
이후 새 app 시작과 추가 재시작에서 원래 14개의 UDN/UID 및 endpoint mapping을
비교하고, Google Home 기존 기기 기능도 검증해야 합니다.

읽기 도구는 `tools/read_matter_endpoint_baseline.py`입니다. `manifest`에
matching unstripped ELF, cross GDB, 운영 ELF SHA256을 제공하고 `capture`에
같은 ELF/manifest, strict SSH host, 새 private 출력 경로를 제공합니다.
`--include-bindings`를 두 단계 모두 지정하면 DWARF vector/string layout을
해석하여 정확한 UDN/device-pointer binding과 bounded name을 private에만
기록합니다. 새로운 FW는 새로운 matching ELF로 manifest를 재생성합니다.
출력은 개수만 표시하며 0700 부모/0600 파일과 no-overwrite를 적용합니다.

새 identity registry host test가 기존 `run_netcam_tests.py`에 추가되어
현재 여섯 suite를 실행합니다. 앞의 다섯 suite 검증은 당시 이력입니다.
새 registry의 재시작/순서 독립성, legacy seed, UID 충돌, corrupt schema/value,
COMMIT lock 오류, private 파일 권한 및 publication 전 실패 처리와 읽기 도구의
경계/포인터 binding 회귀를 검증합니다. target image 빌드와 실제 seed 설치 후
UID 유지/Google Home 수락은 별도 실기 검증입니다.

strict SSH host-key 검증을 유지합니다. legacy SCP가 필요하면 `scp -O`를
사용하고 전송 후 size/SHA를 대조합니다. SSH 알고리즘 예외는 해당 WeMo
host에만 적용합니다. 별도 controller commissioning을 추가하지 않습니다.

배포별 private 디렉터리에 다음 자료를 보관합니다.

- 현재/새 APK 해시와 설치 버전, 기존 endpoint mapping 및 Google Home 비교 기준.
- `/etc/wemo_ctrl.conf`, init/watchdog 스크립트, enable 상태,
  `/etc/wemo-matter-bridge/matter-onboarding.json`의 비공개 백업.
- `/data/wemo-matter/wemo_device.db`, `wemo_state.db`, 기존
  `sensor_endpoints.db`의 SQLite 일관성 있는 백업. 서비스가 실행 중이면
  SQLite backup API를 사용하고, 정지 후 파일 복사 시 WAL/SHM 상태도 확인합니다.
- sensor DB가 원래 없었는지 기록합니다.

백업·로그에 onboarding 값, 키, UDN/MAC, fabric/개인 화면을 출력하거나
커밋하지 않습니다. `/data/wemo-matter/chip`과 commissioning 파일은 그대로
유지하며 삭제·초기화·다른 fabric으로 덮어쓰지 않습니다.

## 조건 통과 후 수행할 설치 순서

아래 명령은 절차 설명이며 이 기록을 만들면서 실행하지 않았습니다.
먼저 두 새 APK를 장치 `/tmp`로 보내고 host/target 해시를 비교합니다.
서명 예외는 `docs/TROUBLESHOOTING.md`의 local APK 개발 절차에 한정합니다.
global trusted key나 repository 검증 설정을 변경하지 않습니다.

```sh
apk add --network=no --scripts=no --allow-untrusted --simulate \
  /tmp/openwemo-bridge-core-0.1-r22.apk \
  /tmp/wemo-matter-bridge-2025.12.01~8effa808-r23.apk
```

simulate는 실제 JFFS2 설치 peak 보장이 아닙니다. 예상하지 않은 dependency
변경이나 다른 패키지 교체가 나오면 중단합니다. 실제 변경 순서는 다음과 같습니다.

1. watchdog → Matter app → controller 순서로 stop하고 해당 process 종료 확인.
2. DB의 정지 상태 백업 확정. `/data`는 mount 상태 유지.
3. 두 새 APK를 **한 transaction**에서 설치. `--scripts=no`로 중간 IPCv2/v3
   조합의 자동 시작을 방지합니다. 현재 apk3.0.5 지원과 실제 APK의
   pre/post script를 다시 확인하며, 필요한 작업을 수동 계획에 포함합니다.
4. installed version 확인, controller → Matter app → watchdog 순서로 시작.
   controller IPCv3 readiness와 기존 fabric 재개 확인 후 다음 서비스를 시작.

```sh
/etc/init.d/wemo-matter-bridge-watchdog stop
/etc/init.d/wemo-matter-bridge stop
/etc/init.d/wemo_ctrl stop
# 종료/백업 확인 후에만 실행
apk add --network=no --scripts=no --allow-untrusted \
  /tmp/openwemo-bridge-core-0.1-r22.apk \
  /tmp/wemo-matter-bridge-2025.12.01~8effa808-r23.apk
# 버전/공간/IPC readiness를 단계별 확인
/etc/init.d/wemo_ctrl start
/etc/init.d/wemo-matter-bridge start
/etc/init.d/wemo-matter-bridge-watchdog start
```

controller/engine/Matter는 paired update가 필수입니다. 단독 패키지 설치,
reset, factory reset, commissioning 재시작, CHIP 파일 삭제를 사용하지 않습니다.

## 수락과 paired rollback

기존 endpoint ID/type/unique identity가 기준과 같아야 합니다. 기존 플러그·
조명 기능을 먼저 검증한 뒤 NetCam을 확인하며 Mini 008 전원은 NetCam
세션과 조율합니다. 전체 기기 전원 시험에 무심코 포함하지 않습니다.

| 상황 | Occupancy | Reachable |
| --- | --- | --- |
| 최초 유효 관측 전 | 0은 초기 placeholder | false |
| 준비된 idle | 0 | true |
| 실제 motion | 1 | true |
| HD paused / unavailable / setup / unknown / legacy / offline | 마지막 유효값 유지 | false |
| HD 종료 후 준비된 idle | 0 | true |

정확한 NetCamSensor urn, Occupancy 0x0406/device type 0x0107, high-range 센서
endpoint 유지, mandatory Identify, legacy plug/light allocator 보존을 확인합니다.
일반 사용자용 공지·onboarding에 retired NetCam 지원 안내를 추가하지 않습니다.
Google Home 표시/실제 움직임 검증 전에는 수락 완료로 기록하지 않습니다.
센서 통합이며 Matter camera/video/live view 수락은 별개입니다.

설치/서비스 시작 실패, 기존 endpoint 변경·기능 퇴행이면 같은 stop 순서 후
아래 **두 기존 APK를 함께** simulate/reinstall합니다. 실제 파일 전송 해시와
기존 설치 payload 확인이 선행돼야 합니다.

```sh
apk add --network=no --scripts=no --allow-untrusted --simulate \
  /tmp/openwemo-bridge-core-0.1-r21.apk \
  /tmp/wemo-matter-bridge-2025.12.01~8effa808-r22.apk
# 검토 후 --simulate를 빼고 같은 두 파일을 함께 설치
```

필요한 runtime config/SQLite 백업만 서비스 정지 상태에서 복원합니다.
CHIP/fabric은 건드리지 않고 새 sensor DB도 삭제하지 않습니다(구 app은 무시).
controller → app → watchdog을 재개하고 기존 mapping과 기능을 다시 확인합니다.
실패를 재commissioning으로 숨기지 말고 증거를 보존해 원인을 조사합니다.
APK downgrade는 disposable fixture에서 확인됐으며 실제 운영 rollback은 미시험입니다.

## 개발 재현

```sh
python3 package/network/services/openwemo-bridge-core/src/tests/run_netcam_tests.py
git diff --check
```

이 helper는 임시 디렉터리에서 실제 producer/IPC/registry/adapter의 다섯 suite를
실행하며 운영 daemon이나 카메라에 접속하지 않습니다. target 재빌드에는 기존
`.build/netcam-sensor-sdk`를 사용합니다. 그 SDK의 `package/wemo-custom/` 아래
core/Matter 링크가 현재 checkout의 `package/network/services/`를 가리키는지
확인한 뒤, core clean/compile → Matter clean/compile을 순서대로 수행합니다.
새 빌드 시 ABIv3 헤더/engine archive 및 copied-source hashes를 다시 대조하고
APK manifest를 새로 기록합니다. APK가 바뀌면 위 고정 해시의 새 검토가 필요합니다.

## r24 실행 중 발견한 정체와 후속 수정

첫 실행의 14개 legacy identity는 보존됐지만 이후 Google Home 조명 시험은
오프라인으로 표시됐고 physical SOAP state는 원래 상태 그대로였습니다.
실제 CASE 인증/성공 ACK는 있었으므로 fabric을 초기화하지 않았습니다.
controller IPC timeout, state DB locked, Matter UDP receive queue 증가와
watchdog의 자동 재시작을 관측했습니다. 해당 시점의 메모리 여유는 약
18 MiB였고 OOM/panic/hung-task 진단은 없었습니다.

지속 연결 discovery 관측에서는 controller cache 15개 중 NetCam 1개를
확인했습니다. 재시작된 r24 프로세스의 15 binding/15 active device/18 registered
endpoint와 exact canonical UDN을 대조했습니다. 새 프로세스에서 정상 등록을
관측한 사실이 이전 프로세스의 정체를 반증하지는 않습니다.

Add/RemoveDeviceEndpoint는 startup뿐 아니라 stack mutex를 이미 보유한
Matter scheduled reconciliation에서도 호출됩니다. POSIX nonrecursive mutex를
무조건 다시 잡는 경로를 현재 owner일 때 재획득하지 않는 RAII guard로
수정했습니다. 실제 SDK의 owner tracking을 요구하며 tracking이 꺼진 빌드는
명시적으로 거부합니다. startup, already-owned add/remove/error-return host
회귀 시험 및 전체 7개 host suite를 통과했습니다. Claude 독립 검토도 guard
로직의 구체적인 결함을 찾지 못했습니다. 테스트에서 NDEBUG가 assertion의
부작용을 제거하는 문제는 assertion 유지 후 `-DNDEBUG` 실행으로 확인했습니다.

이 source 수정은 Matter r25에 포함합니다. 실제 controller 캐시/SQLite 잠금
순환에 대한 별도 수정과 최종 image/Google Home 수락은 아직 진행 중입니다.
CLI discovery는 asynchronous send 후 바로 EOF를 주면 controller가 이미
받은 frame을 버릴 수 있으므로 검증 때 stdin/socket을 유지했습니다.
`wemo_client`는 argv command를 실행하지 않으며 stdin 명령을 사용합니다.
