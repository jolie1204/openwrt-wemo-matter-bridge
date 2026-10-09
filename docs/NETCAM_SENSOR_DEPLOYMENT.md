# NetCam 센서 운영 배포 준비

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
2. **기존 endpoint 기준값**: 실제 commissioned 관리자로 Descriptor
   PartsList, endpoint ID/device type/unique identity를 private 기록에 확보해야
   합니다. Google Home만 사용하며 기존 별도 admin reader는 찾지 못했습니다.
   S20+의 기존 기기/브리지 화면은 읽기 전용으로 확인했지만 실제 ID 읽기와는
   다릅니다. host allocator 회귀를 현재 운영 mapping의 증명으로 사용하지
   않습니다. 기존 관리자의 읽기 경로를 조사하고, 기준값 수집 방법이 없으면
   이 조건의 해결 방안을 검토하기 전 배포를 진행하지 않습니다.
3. **복구 준비**: 정확한 운영 장치, 패키지 버전, 아키텍처, mount/공간,
   새/롤백 APK의 payload와 전송 해시, 서비스 enable 상태를 다시 확인합니다.
   아래 사전 백업과 실패 시 paired rollback이 준비돼야 합니다.

위 숫자는 2026-10-09 관측입니다. 다시 접속할 때 갱신하고 새로운 결과를
private 배포 기록에 남깁니다. 현재 문서만으로 조건 통과를 주장하지 않습니다.

## 사전 보존

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
